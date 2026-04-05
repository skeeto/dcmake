#include "dcmake.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// --- DAP wire protocol ---

static void dap_send(Debugger *dbg, const json &msg)
{
    std::string body = msg.dump();
    char header[64];
    int hlen = snprintf(header, sizeof(header),
                        "Content-Length: %zu\r\n\r\n", body.size());
    if (dbg->pipe_write) {
        dbg->pipe_write(dbg->platform, header, hlen);
        dbg->pipe_write(dbg->platform, body.data(), (int)body.size());
    }
}

static void dap_request(Debugger *dbg, const char *command,
                        json arguments = json::object())
{
    json msg = {
        {"seq", dbg->next_seq++},
        {"type", "request"},
        {"command", command},
        {"arguments", arguments},
    };
    dap_send(dbg, msg);
}

// Reader thread: reads from pipe, parses Content-Length framing,
// pushes complete JSON strings into dbg->inbox.
static void reader_thread_func(Debugger *dbg)
{
    std::string buf;
    char tmp[4096];

    while (dbg->reader_running.load()) {
        int n = dbg->pipe_read(dbg->platform, tmp, sizeof(tmp));
        if (n <= 0) {
            dbg->reader_running.store(false);
            break;
        }
        buf.append(tmp, n);

        // Process as many complete messages as possible
        for (;;) {
            auto sep = buf.find("\r\n\r\n");
            if (sep == std::string::npos) break;

            std::string_view headers(buf.data(), sep);
            auto cl = headers.find("Content-Length:");
            if (cl == std::string_view::npos) {
                buf.erase(0, sep + 4);
                continue;
            }
            int content_length = 0;
            const char *first = buf.data() + cl + 15;
            const char *last = buf.data() + sep;
            while (first < last && *first == ' ') first++;
            auto [ptr, ec] = std::from_chars(first, last, content_length);
            if (ec != std::errc{} || content_length < 0) {
                buf.erase(0, sep + 4);
                continue;
            }

            size_t msg_start = sep + 4;
            size_t msg_end = msg_start + (size_t)content_length;
            if (buf.size() < msg_end) break;

            std::string message = buf.substr(msg_start, content_length);
            buf.erase(0, msg_end);

            std::lock_guard<std::mutex> lock(dbg->queue_mutex);
            dbg->inbox.push_back(std::move(message));
        }
    }
}

// --- Source file cache ---

static SourceFile *get_source(Debugger *dbg, const std::string &path)
{
    for (auto &sf : dbg->sources) {
        if (sf.path == path) return &sf;
    }

    std::ifstream f(path);
    if (!f.is_open()) return nullptr;

    SourceFile sf;
    sf.path = path;
    std::string line;
    while (std::getline(f, line)) {
        sf.lines.push_back(std::move(line));
    }
    dbg->sources.push_back(std::move(sf));
    return &dbg->sources.back();
}

// --- Breakpoint helpers ---

static void send_breakpoints_for_file(Debugger *dbg, const std::string &path)
{
    json bp_array = json::array();
    for (auto &bp : dbg->breakpoints) {
        if (bp.path == path) {
            bp_array.push_back({{"line", bp.line}});
        }
    }
    dap_request(dbg, "setBreakpoints", {
        {"source", {{"path", path}}},
        {"breakpoints", bp_array},
    });
}

static void send_exception_breakpoints(Debugger *dbg)
{
    json filters = json::array();
    for (auto &ef : dbg->exception_filters) {
        if (ef.enabled) filters.push_back(ef.filter);
    }
    dap_request(dbg, "setExceptionBreakpoints", {{"filters", filters}});
}

static void toggle_breakpoint(Debugger *dbg, const std::string &path, int line)
{
    // Remove if exists
    for (auto it = dbg->breakpoints.begin(); it != dbg->breakpoints.end(); ++it) {
        if (it->path == path && it->line == line) {
            dbg->breakpoints.erase(it);
            if (dbg->state != DapState::IDLE && dbg->state != DapState::TERMINATED) {
                send_breakpoints_for_file(dbg, path);
            }
            return;
        }
    }
    // Add new
    LineBreakpoint bp;
    bp.path = path;
    bp.line = line;
    dbg->breakpoints.push_back(bp);
    if (dbg->state != DapState::IDLE && dbg->state != DapState::TERMINATED) {
        send_breakpoints_for_file(dbg, path);
    }
}

static bool has_breakpoint(Debugger *dbg, const std::string &path, int line)
{
    for (auto &bp : dbg->breakpoints) {
        if (bp.path == path && bp.line == line) return true;
    }
    return false;
}

// --- Source tab helpers ---

static void open_source(Debugger *dbg, const std::string &path)
{
    for (auto &os : dbg->open_sources) {
        if (os.path == path) {
            os.focus = true;
            return;
        }
    }
    OpenSource os;
    os.path = path;
    os.focus = true;
    dbg->open_sources.push_back(std::move(os));
}

// --- Variable fetch helpers ---

static void fetch_variables(Debugger *dbg, int64_t ref)
{
    if (ref > 0) {
        int seq = dbg->next_seq;
        dbg->pending_vars[seq] = ref;
        dap_request(dbg, "variables", {{"variablesReference", ref}});
    }
}

// Find a DapVariable by variablesReference in the scope/variable tree.
static DapVariable *find_variable_by_ref(std::vector<DapVariable> &vars,
                                         int64_t ref)
{
    for (auto &v : vars) {
        if (v.variables_ref == ref) return &v;
        DapVariable *found = find_variable_by_ref(v.children, ref);
        if (found) return found;
    }
    return nullptr;
}

// --- DAP message handlers ---

static void handle_response(Debugger *dbg, const json &msg)
{
    std::string command = msg.value("command", "");
    bool success = msg.value("success", false);

    if (!success) {
        dbg->status = "Error: " + msg.value("message", "unknown error");
        return;
    }

    if (command == "initialize") {
        // Parse exception breakpoint filters from capabilities
        if (msg.contains("body") &&
            msg["body"].contains("exceptionBreakpointFilters")) {
            dbg->exception_filters.clear();
            for (auto &f : msg["body"]["exceptionBreakpointFilters"]) {
                ExceptionFilter ef;
                ef.filter = f.value("filter", "");
                ef.label = f.value("label", ef.filter);
                ef.enabled = f.value("default", false);
                dbg->exception_filters.push_back(std::move(ef));
            }
        }
    } else if (command == "configurationDone") {
        // Wait for stopped event
    } else if (command == "stackTrace") {
        auto &body = msg["body"];
        std::vector<StackFrame> new_stack;
        for (auto &frame : body["stackFrames"]) {
            StackFrame sf;
            sf.id = frame.value("id", 0);
            sf.name = frame.value("name", "");
            if (frame.contains("source") && frame["source"].contains("path")) {
                sf.source_path = frame["source"]["path"];
            }
            sf.line = frame.value("line", 0);
            new_stack.push_back(std::move(sf));
        }
        dbg->stack = std::move(new_stack);
        if (!dbg->stack.empty()) {
            auto &top = dbg->stack[0];
            if (!top.source_path.empty()) {
                dbg->current_source = get_source(dbg, top.source_path);
                dbg->current_line = top.line;
                dbg->scroll_to_line = true;
                open_source(dbg, top.source_path);
                dbg->status = "Paused";
            }
            // Request scopes for top frame
            dap_request(dbg, "scopes", {{"frameId", top.id}});
        }
        dbg->state = DapState::STOPPED;
    } else if (command == "scopes") {
        std::vector<DapScope> new_scopes;
        if (msg.contains("body") && msg["body"].contains("scopes")) {
            for (auto &s : msg["body"]["scopes"]) {
                DapScope scope;
                scope.name = s.value("name", "");
                scope.variables_ref = s.value("variablesReference", (int64_t)0);
                new_scopes.push_back(std::move(scope));
            }
        }
        dbg->scopes = std::move(new_scopes);
        for (auto &scope : dbg->scopes) {
            fetch_variables(dbg, scope.variables_ref);
        }
    } else if (command == "variables") {
        // Look up which variablesReference this response is for via request_seq.
        int req_seq = msg.value("request_seq", 0);
        auto it = dbg->pending_vars.find(req_seq);
        if (it != dbg->pending_vars.end() &&
            msg.contains("body") && msg["body"].contains("variables")) {
            int64_t ref = it->second;
            dbg->pending_vars.erase(it);

            std::vector<DapVariable> parsed;
            for (auto &v : msg["body"]["variables"]) {
                DapVariable dv;
                dv.name = v.value("name", "");
                dv.value = v.value("value", "");
                dv.type = v.value("type", "");
                dv.variables_ref = v.value("variablesReference", (int64_t)0);
                parsed.push_back(std::move(dv));
            }

            // Match to scope or nested variable by variablesReference
            for (auto &scope : dbg->scopes) {
                if (scope.variables_ref == ref) {
                    scope.variables = std::move(parsed);
                    scope.fetched = true;
                    break;
                }
                DapVariable *target = find_variable_by_ref(
                    scope.variables, ref);
                if (target) {
                    target->children = std::move(parsed);
                    target->fetched = true;
                    break;
                }
            }
        }
    } else if (command == "setBreakpoints") {
        // Update breakpoint IDs and verified status
        if (msg.contains("body") && msg["body"].contains("breakpoints")) {
            // The response breakpoints correspond to the breakpoints we sent,
            // matched by source path from the original request.
            // Since we can't easily correlate, update all breakpoints with
            // matching source from the response.
            for (auto &rbp : msg["body"]["breakpoints"]) {
                std::string path;
                if (rbp.contains("source") && rbp["source"].contains("path")) {
                    path = rbp["source"]["path"];
                }
                int line = rbp.value("line", 0);
                int id = rbp.value("id", 0);
                bool verified = rbp.value("verified", false);
                for (auto &bp : dbg->breakpoints) {
                    if (bp.path == path && bp.line == line) {
                        bp.id = id;
                        bp.verified = verified;
                    }
                }
            }
        }
    } else if (command == "continue" || command == "next" ||
               command == "stepIn" || command == "stepOut") {
        // Acknowledgement; wait for stopped/terminated event
    } else if (command == "disconnect") {
        // Clean disconnect acknowledged
    }
}

static void handle_event(Debugger *dbg, const json &msg)
{
    std::string event = msg.value("event", "");

    if (event == "initialized") {
        // Send exception breakpoints, then any line breakpoints, then pause+configurationDone
        send_exception_breakpoints(dbg);

        // Send breakpoints for each unique file
        std::vector<std::string> files;
        for (auto &bp : dbg->breakpoints) {
            bool found = false;
            for (auto &f : files) {
                if (f == bp.path) { found = true; break; }
            }
            if (!found) files.push_back(bp.path);
        }
        for (auto &f : files) {
            send_breakpoints_for_file(dbg, f);
        }

        if (dbg->pause_at_entry) {
            dap_request(dbg, "pause", {{"threadId", 0}});
        }
        dap_request(dbg, "configurationDone");
    } else if (event == "stopped") {
        auto &body = msg["body"];
        dbg->thread_id = body.value("threadId", dbg->thread_id);
        dbg->status = "Paused";

        dap_request(dbg, "stackTrace", {{"threadId", dbg->thread_id}});
    } else if (event == "terminated") {
        dbg->state = DapState::TERMINATED;
        dbg->status = "Stopped";
        dap_request(dbg, "disconnect");
    } else if (event == "exited") {
        auto &body = msg["body"];
        int code = body.value("exitCode", -1);
        dbg->status = "Stopped (exit " + std::to_string(code) + ")";
    } else if (event == "breakpoint") {
        // Breakpoint verified/changed
        if (msg.contains("body") && msg["body"].contains("breakpoint")) {
            auto &rbp = msg["body"]["breakpoint"];
            int id = rbp.value("id", 0);
            bool verified = rbp.value("verified", false);
            int line = rbp.value("line", 0);
            for (auto &bp : dbg->breakpoints) {
                if (bp.id == id) {
                    bp.verified = verified;
                    if (line > 0) bp.line = line;
                }
            }
        }
    } else if (event == "thread") {
        // Thread started/exited -- informational only
    } else if (event == "output") {
        // CMake output -- could log somewhere eventually
    }
}

static void process_messages(Debugger *dbg)
{
    std::vector<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(dbg->queue_mutex);
        messages.swap(dbg->inbox);
    }

    for (auto &raw : messages) {
        json msg;
        try {
            msg = json::parse(raw);
        } catch (const json::parse_error &) {
            dbg->status = "JSON parse error";
            continue;
        }

        std::string type = msg.value("type", "");
        if (type == "response") {
            handle_response(dbg, msg);
        } else if (type == "event") {
            handle_event(dbg, msg);
        }
    }

    // Detect dead reader thread (pipe closed)
    if (!dbg->reader_running.load() &&
        dbg->state != DapState::IDLE &&
        dbg->state != DapState::TERMINATED) {
        dbg->state = DapState::TERMINATED;
        if (dbg->status.find("Exited") == std::string::npos &&
            dbg->status.find("Terminated") == std::string::npos) {
            dbg->status = "Stopped";
        }
    }
}

// --- ImGui UI ---

static void render_toolbar(Debugger *dbg)
{
    bool idle = dbg->state == DapState::IDLE;
    bool terminated = dbg->state == DapState::TERMINATED;
    bool stopped = dbg->state == DapState::STOPPED;
    bool editable = idle || terminated;

    // Keyboard shortcuts (skip when typing in text box)
    ImGuiIO &io = ImGui::GetIO();
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
            if (io.KeyShift) {
                if (!idle) dcmake_stop(dbg);
            } else if (editable) {
                dbg->pause_at_entry = false;
                dcmake_start(dbg);
            } else if (stopped) {
                dap_request(dbg, "continue", {{"threadId", dbg->thread_id}});
                dbg->state = DapState::RUNNING;
                dbg->status = "Running";
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F10)) {
            if (editable) {
                dbg->pause_at_entry = true;
                dcmake_start(dbg);
            } else if (stopped) {
                dap_request(dbg, "next", {{"threadId", dbg->thread_id}});
                dbg->state = DapState::RUNNING;
                dbg->status = "Running";
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
            if (io.KeyShift) {
                if (stopped) {
                    dap_request(dbg, "stepOut", {{"threadId", dbg->thread_id}});
                    dbg->state = DapState::RUNNING;
                    dbg->status = "Running";
                }
            } else if (editable) {
                dbg->pause_at_entry = true;
                dcmake_start(dbg);
            } else if (stopped) {
                dap_request(dbg, "stepIn", {{"threadId", dbg->thread_id}});
                dbg->state = DapState::RUNNING;
                dbg->status = "Running";
            }
        }
    }

    // Command line text box
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(avail_w * 0.5f);
    if (!editable) {
        ImGui::BeginDisabled();
    }
    ImGui::InputText("##cmdline", dbg->cmdline, sizeof(dbg->cmdline));
    if (!editable) {
        ImGui::EndDisabled();
    }

    // Start/Continue button (F5)
    ImGui::SameLine();
    ImGui::BeginDisabled(!editable && !stopped);
    if (editable) {
        if (ImGui::Button("Start")) {
            dbg->pause_at_entry = false;
            dcmake_start(dbg);
        }
    } else {
        if (ImGui::Button("Continue")) {
            dap_request(dbg, "continue", {{"threadId", dbg->thread_id}});
            dbg->state = DapState::RUNNING;
            dbg->status = "Running";
        }
    }
    ImGui::EndDisabled();

    // Stop button (Shift+F5)
    ImGui::SameLine();
    ImGui::BeginDisabled(idle);
    if (ImGui::Button("Stop")) {
        dcmake_stop(dbg);
    }
    ImGui::EndDisabled();

    // Step buttons — also start cmake from idle (with pause at entry)
    ImGui::SameLine();
    ImGui::BeginDisabled(!stopped && !editable);
    if (ImGui::Button("Step Over")) {
        if (editable) {
            dbg->pause_at_entry = true;
            dcmake_start(dbg);
        } else {
            dap_request(dbg, "next", {{"threadId", dbg->thread_id}});
            dbg->state = DapState::RUNNING;
            dbg->status = "Running";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Step In")) {
        if (editable) {
            dbg->pause_at_entry = true;
            dcmake_start(dbg);
        } else {
            dap_request(dbg, "stepIn", {{"threadId", dbg->thread_id}});
            dbg->state = DapState::RUNNING;
            dbg->status = "Running";
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!stopped);
    if (ImGui::Button("Step Out")) {
        dap_request(dbg, "stepOut", {{"threadId", dbg->thread_id}});
        dbg->state = DapState::RUNNING;
        dbg->status = "Running";
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Text(" | ");
    ImGui::SameLine();
    switch (dbg->state) {
    case DapState::CONNECTING:
    case DapState::INITIALIZING:
    case DapState::RUNNING:
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "%s",
                           dbg->status.c_str());
        break;
    case DapState::STOPPED:
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "%s",
                           dbg->status.c_str());
        break;
    case DapState::TERMINATED:
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
                           dbg->status.c_str());
        break;
    default:
        ImGui::TextUnformatted(dbg->status.c_str());
        break;
    }
}

// --- CMake syntax highlighting ---

enum struct TokenType {
    DEFAULT,
    COMMENT,
    STRING,
    VARIABLE,
    GENERATOR_EXPR,
    KEYWORD,
    BOOLEAN,
};

struct Token {
    std::string_view text;
    TokenType type;
};

static ImU32 token_color(TokenType type)
{
    switch (type) {
    case TokenType::COMMENT:        return IM_COL32(106, 153, 85,  255);
    case TokenType::STRING:         return IM_COL32(206, 145, 120, 255);
    case TokenType::VARIABLE:       return IM_COL32(86,  206, 209, 255);
    case TokenType::GENERATOR_EXPR: return IM_COL32(190, 132, 209, 255);
    case TokenType::KEYWORD:        return IM_COL32(86,  156, 214, 255);
    case TokenType::BOOLEAN:        return IM_COL32(181, 206, 168, 255);
    default:                        return IM_COL32(212, 212, 212, 255);
    }
}

static bool is_cmake_keyword(std::string_view word)
{
    static const char *keywords[] = {
        "add_compile_definitions", "add_compile_options",
        "add_custom_command", "add_custom_target",
        "add_dependencies", "add_executable", "add_library",
        "add_subdirectory", "add_test",
        "break", "cmake_minimum_required", "cmake_parse_arguments",
        "cmake_path", "configure_file", "continue",
        "else", "elseif", "enable_language", "enable_testing",
        "endforeach", "endfunction", "endif", "endmacro", "endwhile",
        "execute_process", "export",
        "fetchcontent_declare", "fetchcontent_getproperties",
        "fetchcontent_makeavailable", "fetchcontent_populate",
        "file", "find_package", "find_library", "find_path",
        "find_program", "foreach", "function",
        "get_cmake_property", "get_directory_property",
        "get_filename_component", "get_property",
        "get_target_property",
        "if", "include", "include_guard", "install",
        "list",
        "macro", "mark_as_advanced", "math", "message",
        "option",
        "project",
        "return",
        "set", "set_directory_properties", "set_property",
        "set_target_properties", "string",
        "target_compile_definitions", "target_compile_features",
        "target_compile_options", "target_include_directories",
        "target_link_directories", "target_link_libraries",
        "target_link_options", "target_sources",
        "unset",
        "while",
    };
    constexpr int n = sizeof(keywords) / sizeof(keywords[0]);

    // Case-insensitive comparison via lowercase copy
    char lower[64];
    if (word.size() >= sizeof(lower)) return false;
    for (size_t i = 0; i < word.size(); i++)
        lower[i] = (char)tolower((unsigned char)word[i]);
    lower[word.size()] = 0;

    auto it = std::lower_bound(keywords, keywords + n, lower,
        [](const char *a, const char *b) { return strcmp(a, b) < 0; });
    return it != keywords + n && strcmp(*it, lower) == 0;
}

static bool is_cmake_boolean(std::string_view word)
{
    static const char *booleans[] = {
        "FALSE", "NO", "OFF", "ON", "TRUE", "YES",
    };
    constexpr int n = sizeof(booleans) / sizeof(booleans[0]);

    char upper[8];
    if (word.size() >= sizeof(upper)) return false;
    for (size_t i = 0; i < word.size(); i++)
        upper[i] = (char)toupper((unsigned char)word[i]);
    upper[word.size()] = 0;

    auto it = std::lower_bound(booleans, booleans + n, upper,
        [](const char *a, const char *b) { return strcmp(a, b) < 0; });
    return it != booleans + n && strcmp(*it, upper) == 0;
}

static std::vector<Token> tokenize_cmake(std::string_view line)
{
    std::vector<Token> tokens;
    size_t i = 0;

    auto emit = [&](size_t start, size_t end, TokenType type) {
        if (end > start)
            tokens.push_back({line.substr(start, end - start), type});
    };

    // Scan for variable reference starting at i (pointing at '$').
    // Returns position after the closing '}', or i if not a variable ref.
    auto scan_variable = [&]() -> size_t {
        size_t s = i;
        size_t j = i + 1;
        // $ENV{ or $CACHE{
        if (j < line.size() && line[j] != '{') {
            size_t k = j;
            while (k < line.size() && isalpha((unsigned char)line[k])) k++;
            if (k >= line.size() || line[k] != '{') return i;
            j = k;
        }
        if (j >= line.size() || line[j] != '{') return i;
        // Scan to matching }
        int depth = 1;
        j++;
        while (j < line.size() && depth > 0) {
            if (line[j] == '{') depth++;
            else if (line[j] == '}') depth--;
            j++;
        }
        emit(s, j, TokenType::VARIABLE);
        return j;
    };

    while (i < line.size()) {
        // Comment
        if (line[i] == '#') {
            emit(i, line.size(), TokenType::COMMENT);
            i = line.size();
            break;
        }

        // String
        if (line[i] == '"') {
            size_t start = i;
            i++;
            while (i < line.size() && line[i] != '"') {
                // Variable reference inside string
                if (line[i] == '$' && i + 1 < line.size() &&
                    (line[i + 1] == '{' || isalpha((unsigned char)line[i + 1]))) {
                    emit(start, i, TokenType::STRING);
                    size_t after = scan_variable();
                    if (after > i) {
                        i = after;
                        start = i;
                        continue;
                    }
                }
                if (line[i] == '\\' && i + 1 < line.size()) i++;
                i++;
            }
            if (i < line.size()) i++; // closing quote
            emit(start, i, TokenType::STRING);
            continue;
        }

        // Variable reference outside string
        if (line[i] == '$' && i + 1 < line.size()) {
            // Generator expression
            if (line[i + 1] == '<') {
                size_t start = i;
                int depth = 1;
                i += 2;
                while (i < line.size() && depth > 0) {
                    if (line[i] == '<') depth++;
                    else if (line[i] == '>') depth--;
                    i++;
                }
                emit(start, i, TokenType::GENERATOR_EXPR);
                continue;
            }
            // Variable reference
            if (line[i + 1] == '{' || isalpha((unsigned char)line[i + 1])) {
                size_t after = scan_variable();
                if (after > i) {
                    i = after;
                    continue;
                }
            }
        }

        // Word (identifier)
        if (isalpha((unsigned char)line[i]) || line[i] == '_') {
            size_t start = i;
            while (i < line.size() &&
                   (isalnum((unsigned char)line[i]) || line[i] == '_'))
                i++;
            std::string_view word = line.substr(start, i - start);
            if (is_cmake_keyword(word))
                emit(start, i, TokenType::KEYWORD);
            else if (is_cmake_boolean(word))
                emit(start, i, TokenType::BOOLEAN);
            else
                emit(start, i, TokenType::DEFAULT);
            continue;
        }

        // Default: accumulate non-special characters
        {
            size_t start = i;
            while (i < line.size() && line[i] != '#' && line[i] != '"' &&
                   line[i] != '$' && !isalpha((unsigned char)line[i]) &&
                   line[i] != '_')
                i++;
            emit(start, i, TokenType::DEFAULT);
        }
    }

    return tokens;
}

static void render_source_content(Debugger *dbg, SourceFile *sf,
                                   int highlight_line, bool scroll_to)
{
    int line_count = (int)sf->lines.size();
    float line_height = ImGui::GetTextLineHeightWithSpacing();

    int gutter_digits = 1;
    for (int n = line_count; n >= 10; n /= 10) gutter_digits++;

    char gutter_buf[16];
    snprintf(gutter_buf, sizeof(gutter_buf), "%*d", gutter_digits, line_count);
    float gutter_width = ImGui::CalcTextSize(gutter_buf).x;

    ImGuiListClipper clipper;
    clipper.Begin(line_count);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            int line_num = i + 1;
            bool is_current = (line_num == highlight_line);
            bool is_bp = has_breakpoint(dbg, sf->path, line_num);

            ImVec2 line_pos = ImGui::GetCursorScreenPos();

            if (is_current) {
                float width = ImGui::GetContentRegionAvail().x +
                              ImGui::GetScrollX();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    line_pos,
                    ImVec2(line_pos.x + width, line_pos.y + line_height),
                    IM_COL32(80, 80, 30, 255));
            }

            if (is_bp) {
                float radius = line_height * 0.3f;
                ImVec2 center(line_pos.x + gutter_width * 0.5f,
                              line_pos.y + line_height * 0.5f);
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    center, radius, IM_COL32(220, 50, 50, 255));
            }

            ImGui::TextDisabled("%*d", gutter_digits, line_num);

            ImVec2 gutter_min = line_pos;
            ImVec2 gutter_max(line_pos.x + gutter_width,
                              line_pos.y + line_height);
            if (ImGui::IsMouseClicked(0) &&
                ImGui::IsMouseHoveringRect(gutter_min, gutter_max)) {
                toggle_breakpoint(dbg, sf->path, line_num);
            }

            ImGui::SameLine();

            if (is_current) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "->");
            } else {
                ImGui::TextUnformatted("  ");
            }
            ImGui::SameLine();

            auto tokens = tokenize_cmake(sf->lines[i]);
            if (tokens.empty()) {
                ImGui::TextUnformatted("");
            } else {
                for (size_t t = 0; t < tokens.size(); t++) {
                    ImU32 col = token_color(tokens[t].type);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::TextUnformatted(tokens[t].text.data(),
                                           tokens[t].text.data() +
                                           tokens[t].text.size());
                    ImGui::PopStyleColor();
                    if (t + 1 < tokens.size())
                        ImGui::SameLine(0, 0);
                }
            }
        }
    }

    if (scroll_to && highlight_line > 0) {
        float target_y = (highlight_line - 1) * line_height;
        float window_h = ImGui::GetWindowHeight();
        ImGui::SetScrollY(target_y - window_h / 2.0f);
    }
}

static void render_sources(Debugger *dbg)
{
    for (auto &os : dbg->open_sources) {
        if (!os.open) continue;

        SourceFile *sf = get_source(dbg, os.path);
        if (!sf) continue;

        const char *filename = os.path.c_str();
        const char *slash = strrchr(filename, '/');
        if (!slash) slash = strrchr(filename, '\\');
        if (slash) filename = slash + 1;

        char win_id[1024];
        snprintf(win_id, sizeof(win_id), "%s###src_%s",
                 filename, os.path.c_str());

        if (os.needs_dock) {
            ImGuiID target = dbg->source_dock_id ? dbg->source_dock_id
                                                 : dbg->dockspace_id;
            ImGui::SetNextWindowDockID(target, ImGuiCond_Always);
            os.needs_dock = false;
        }

        if (os.focus) {
            ImGui::SetNextWindowFocus();
            os.focus = false;
        }

        if (!ImGui::Begin(win_id, &os.open,
                          ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::End();
            continue;
        }

        ImGuiID dock_id = ImGui::GetWindowDockID();
        if (dock_id) dbg->source_dock_id = dock_id;

        bool is_current_file = (dbg->current_source == sf);
        int highlight_line = is_current_file ? dbg->current_line : 0;
        bool scroll = is_current_file && dbg->scroll_to_line;

        render_source_content(dbg, sf, highlight_line, scroll);

        if (scroll) dbg->scroll_to_line = false;

        ImGui::End();
    }

    std::erase_if(dbg->open_sources,
                  [](const OpenSource &os) { return !os.open; });
}

static void render_stack(Debugger *dbg)
{
    if (!ImGui::Begin("Call Stack", &dbg->show_stack)) {
        ImGui::End();
        return;
    }

    for (int i = 0; i < (int)dbg->stack.size(); i++) {
        auto &f = dbg->stack[i];
        char label[512];
        snprintf(label, sizeof(label), "%s%s  %s:%d",
                 i == 0 ? "> " : "  ",
                 f.name.c_str(),
                 f.source_path.c_str(),
                 f.line);
        if (ImGui::Selectable(label, i == 0)) {
            if (!f.source_path.empty()) {
                dbg->current_source = get_source(dbg, f.source_path);
                dbg->current_line = f.line;
                dbg->scroll_to_line = true;
                open_source(dbg, f.source_path);
            }
        }
    }

    ImGui::End();
}

// Render a variable tree (recursive for expandable variables)
static void render_variable_tree(Debugger *dbg, std::vector<DapVariable> &vars)
{
    for (auto &v : vars) {
        if (v.variables_ref > 0) {
            bool open = ImGui::TreeNode(v.name.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f);
            ImGui::TextDisabled("%s", v.value.empty() ? v.type.c_str()
                                                      : v.value.c_str());
            if (open) {
                if (!v.fetched && v.children.empty()) {
                    fetch_variables(dbg, v.variables_ref);
                    v.fetched = true;
                    ImGui::TextDisabled("Loading...");
                } else {
                    render_variable_tree(dbg, v.children);
                }
                ImGui::TreePop();
            }
        } else {
            ImGui::BulletText("%s", v.name.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f);
            ImGui::TextUnformatted(v.value.c_str());
        }
    }
}

// Find a named child in the top-level scope variables (e.g. "Locals", "CacheVariables")
static DapVariable *find_scope_child(Debugger *dbg, const char *name)
{
    for (auto &scope : dbg->scopes) {
        for (auto &v : scope.variables) {
            if (v.name == name) return &v;
        }
    }
    return nullptr;
}

static void render_locals(Debugger *dbg)
{
    if (!ImGui::Begin("Locals", &dbg->show_locals)) {
        ImGui::End();
        return;
    }

    if (dbg->state == DapState::STOPPED || dbg->state == DapState::RUNNING) {
        DapVariable *locals = find_scope_child(dbg, "Locals");
        if (locals) {
            if (!locals->fetched && locals->children.empty()) {
                fetch_variables(dbg, locals->variables_ref);
                locals->fetched = true;
            }
            render_variable_tree(dbg, locals->children);
        } else if (dbg->scopes.empty()) {
            ImGui::TextDisabled("No scope data.");
        } else {
            // Scopes loaded but "Locals" child not found yet — show top scope vars
            for (auto &scope : dbg->scopes) {
                render_variable_tree(dbg, scope.variables);
            }
        }
    } else {
        ImGui::TextDisabled("Not stopped.");
    }

    ImGui::End();
}

static void render_cache(Debugger *dbg)
{
    if (!ImGui::Begin("Cache Variables", &dbg->show_cache)) {
        ImGui::End();
        return;
    }

    if (dbg->state == DapState::STOPPED || dbg->state == DapState::RUNNING) {
        DapVariable *cache = find_scope_child(dbg, "CacheVariables");
        if (cache) {
            if (!cache->fetched && cache->children.empty()) {
                fetch_variables(dbg, cache->variables_ref);
                cache->fetched = true;
            }
            render_variable_tree(dbg, cache->children);
        } else {
            ImGui::TextDisabled("No cache data.");
        }
    } else {
        ImGui::TextDisabled("Not stopped.");
    }

    ImGui::End();
}

static void render_targets(Debugger *dbg)
{
    if (!ImGui::Begin("Targets", &dbg->show_targets)) {
        ImGui::End();
        return;
    }

    if (dbg->state == DapState::STOPPED || dbg->state == DapState::RUNNING) {
        DapVariable *targets = find_scope_child(dbg, "Targets");
        if (targets) {
            if (!targets->fetched && targets->children.empty()) {
                fetch_variables(dbg, targets->variables_ref);
                targets->fetched = true;
            }
            render_variable_tree(dbg, targets->children);
        } else {
            ImGui::TextDisabled("No target data.");
        }
    } else {
        ImGui::TextDisabled("Not stopped.");
    }

    ImGui::End();
}

static void render_tests(Debugger *dbg)
{
    if (!ImGui::Begin("Tests", &dbg->show_tests)) {
        ImGui::End();
        return;
    }

    if (dbg->state == DapState::STOPPED || dbg->state == DapState::RUNNING) {
        DapVariable *tests = find_scope_child(dbg, "Tests");
        if (tests) {
            if (!tests->fetched && tests->children.empty()) {
                fetch_variables(dbg, tests->variables_ref);
                tests->fetched = true;
            }
            render_variable_tree(dbg, tests->children);
        } else {
            ImGui::TextDisabled("No test data.");
        }
    } else {
        ImGui::TextDisabled("Not stopped.");
    }

    ImGui::End();
}

static void render_breakpoints_panel(Debugger *dbg)
{
    if (!ImGui::Begin("Breakpoints", &dbg->show_breakpoints)) {
        ImGui::End();
        return;
    }

    // Exception filters
    ImGui::TextDisabled("Exception Filters");
    for (auto &ef : dbg->exception_filters) {
        if (ImGui::Checkbox(ef.label.c_str(), &ef.enabled)) {
            if (dbg->state != DapState::IDLE &&
                dbg->state != DapState::TERMINATED) {
                send_exception_breakpoints(dbg);
            }
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Line Breakpoints");

    int remove_idx = -1;
    for (int i = 0; i < (int)dbg->breakpoints.size(); i++) {
        auto &bp = dbg->breakpoints[i];
        ImGui::PushID(i);

        // Extract filename from path
        const char *filename = bp.path.c_str();
        const char *slash = strrchr(filename, '/');
        if (!slash) slash = strrchr(filename, '\\');
        if (slash) filename = slash + 1;

        ImGui::Text("%s %s:%d",
                    bp.verified ? "*" : "?",
                    filename, bp.line);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            remove_idx = i;
        }
        ImGui::PopID();
    }

    if (remove_idx >= 0) {
        std::string path = dbg->breakpoints[remove_idx].path;
        dbg->breakpoints.erase(dbg->breakpoints.begin() + remove_idx);
        if (dbg->state != DapState::IDLE &&
            dbg->state != DapState::TERMINATED) {
            send_breakpoints_for_file(dbg, path);
        }
    }

    ImGui::End();
}

static void render_ui(Debugger *dbg)
{
    // Keyboard shortcuts (global, skip when typing)
    ImGuiIO &menu_io = ImGui::GetIO();
    if (!menu_io.WantTextInput) {
        bool ctrl = menu_io.KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
            std::string path = platform_open_file_dialog();
            if (!path.empty()) open_source(dbg, path);
        }
    }

    // Main menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open File...", "Ctrl+O")) {
                std::string path = platform_open_file_dialog();
                if (!path.empty()) open_source(dbg, path);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Call Stack", nullptr, &dbg->show_stack);
            ImGui::MenuItem("Locals", nullptr, &dbg->show_locals);
            ImGui::MenuItem("Cache Variables", nullptr, &dbg->show_cache);
            ImGui::MenuItem("Targets", nullptr, &dbg->show_targets);
            ImGui::MenuItem("Tests", nullptr, &dbg->show_tests);
            ImGui::MenuItem("Breakpoints", nullptr, &dbg->show_breakpoints);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Toolbar window (fixed at top, not dockable)
    ImGuiViewport *vp = ImGui::GetMainViewport();
    float menu_h = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 0));
    ImGui::Begin("##Toolbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoDocking |
                 ImGuiWindowFlags_NoSavedSettings);
    render_toolbar(dbg);
    float toolbar_h = ImGui::GetWindowHeight();
    ImGui::End();

    // DockSpace below the toolbar
    ImVec2 dock_pos(vp->WorkPos.x, vp->WorkPos.y + toolbar_h);
    ImVec2 dock_size(vp->WorkSize.x, vp->WorkSize.y - toolbar_h);
    ImGui::SetNextWindowPos(dock_pos);
    ImGui::SetNextWindowSize(dock_size);
    ImGui::Begin("##DockHost", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoDocking |
                 ImGuiWindowFlags_NoBackground);
    dbg->dockspace_id = ImGui::GetID("DockSpace");
    ImGuiID dockspace_id = dbg->dockspace_id;

    // Set up default layout only if no saved layout exists
    if (dbg->first_layout && !ImGui::DockBuilderGetNode(dockspace_id)) {
        dbg->first_layout = false;
        ImGui::DockBuilderAddNode(dockspace_id,
                                  ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, dock_size);

        ImGuiID center = dockspace_id;
        ImGuiID bottom = 0;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.3f,
                                    &bottom, &center);
        ImGuiID bottom_right = 0;
        ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.5f,
                                    &bottom_right, &bottom);

        dbg->source_dock_id = center;
        ImGui::DockBuilderDockWindow("Call Stack", bottom);
        ImGui::DockBuilderDockWindow("Locals", bottom_right);
        ImGui::DockBuilderDockWindow("Breakpoints", bottom_right);
        ImGui::DockBuilderDockWindow("Cache Variables", bottom_right);
        ImGui::DockBuilderDockWindow("Targets", bottom_right);
        ImGui::DockBuilderDockWindow("Tests", bottom_right);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0, 0),
                     ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    // Render all panels
    render_sources(dbg);
    render_stack(dbg);
    render_locals(dbg);
    render_cache(dbg);
    render_targets(dbg);
    render_tests(dbg);
    render_breakpoints_panel(dbg);
}

// --- Lifecycle ---

void dcmake_init(Debugger *dbg)
{
    dbg->state = DapState::IDLE;
    dbg->status = "Ready";
}

void dcmake_start(Debugger *dbg)
{
    // Clean up any previous session
    if (dbg->state != DapState::IDLE) {
        dcmake_stop(dbg);
    }

    dbg->next_seq = 1;
    dbg->thread_id = 0;
    dbg->stack.clear();
    dbg->sources.clear();
    dbg->scopes.clear();
    dbg->pending_vars.clear();
    dbg->current_source = nullptr;
    dbg->current_line = 0;
    dbg->scroll_to_line = false;
    dbg->inbox.clear();

    dbg->state = DapState::CONNECTING;
    dbg->status = "Running";

    if (!platform_launch(dbg, dbg->cmdline)) {
        dbg->state = DapState::TERMINATED;
        return;
    }

    // Start reader thread
    dbg->reader_running.store(true);
    dbg->reader_thread = std::thread(reader_thread_func, dbg);

    // Begin DAP handshake
    dbg->state = DapState::INITIALIZING;
    dbg->status = "Running";
    dap_request(dbg, "initialize", {
        {"adapterID", "dcmake"},
        {"clientID", "dcmake"},
        {"clientName", "dcmake"},
        {"linesStartAt1", true},
        {"columnsStartAt1", true},
        {"pathFormat", "path"},
        {"supportsVariableType", true},
    });
}

void dcmake_stop(Debugger *dbg)
{
    if (dbg->state == DapState::IDLE) return;

    dbg->reader_running.store(false);
    if (dbg->pipe_shutdown) {
        dbg->pipe_shutdown(dbg->platform);
    }
    if (dbg->reader_thread.joinable()) {
        dbg->reader_thread.join();
    }

    platform_cleanup(dbg);

    dbg->pipe_read = nullptr;
    dbg->pipe_write = nullptr;
    dbg->pipe_shutdown = nullptr;

    dbg->state = DapState::IDLE;
    dbg->status = "Stopped";
}

void dcmake_frame(Debugger *dbg)
{
    process_messages(dbg);
    render_ui(dbg);
}

void dcmake_shutdown(Debugger *dbg)
{
    // Send disconnect if still connected
    if (dbg->pipe_write && dbg->state != DapState::TERMINATED &&
        dbg->state != DapState::IDLE) {
        dap_request(dbg, "disconnect");
    }

    dcmake_stop(dbg);
}
