#include "dcmake.hpp"
#include "icon_font.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#define ICON_CODICON_MIN       0xEACF
#define ICON_CODICON_MAX       0xEAD7
#define ICON_DEBUG_CONTINUE    "\xee\xab\x8f"  // U+EACF
#define ICON_DEBUG_START       "\xee\xab\x93"  // U+EAD3
#define ICON_DEBUG_STEP_INTO   "\xee\xab\x94"  // U+EAD4
#define ICON_DEBUG_STEP_OUT    "\xee\xab\x95"  // U+EAD5
#define ICON_DEBUG_STEP_OVER   "\xee\xab\x96"  // U+EAD6
#define ICON_DEBUG_STOP        "\xee\xab\x97"  // U+EAD7

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
        buf.append(tmp, (size_t)n);

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

            std::string message = buf.substr(msg_start, (size_t)content_length);
            buf.erase(0, msg_end);

            std::lock_guard<std::mutex> lock(dbg->queue_mutex);
            dbg->inbox.push_back(std::move(message));
        }
    }
}

static void stdout_thread_func(Debugger *dbg)
{
    char tmp[4096];
    int skip_lines = 3;  // cmake debugger preamble
    while (dbg->stdout_running.load()) {
        int n = dbg->stdout_read(dbg->platform, tmp, sizeof(tmp));
        if (n <= 0) break;
        char *start = tmp;
        char *end = tmp + n;
        while (skip_lines > 0 && start < end) {
            char *nl = (char *)memchr(start, '\n', (size_t)(end - start));
            if (!nl) { start = end; break; }
            skip_lines--;
            start = nl + 1;
        }
        if (start < end) {
            std::lock_guard<std::mutex> lock(dbg->queue_mutex);
            dbg->stdout_pending.append(start, (size_t)(end - start));
        }
    }
    dbg->stdout_running.store(false);
}

// --- Source file cache ---

static SourceFile *get_source(Debugger *dbg, const std::string &path)
{
    for (auto &sf : dbg->sources) {
        if (sf.path == path) return &sf;
    }

    std::string content = platform_read_file(path.c_str());
    if (content.empty()) return nullptr;

    SourceFile sf;
    sf.path = path;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t nl = content.find('\n', pos);
        if (nl == std::string::npos) {
            sf.lines.push_back(content.substr(pos));
            break;
        }
        size_t end = (nl > pos && content[nl - 1] == '\r') ? nl - 1 : nl;
        sf.lines.push_back(content.substr(pos, end - pos));
        pos = nl + 1;
    }
    dbg->sources.push_back(std::move(sf));
    return &dbg->sources.back();
}

// --- Breakpoint helpers ---

static void send_breakpoints_for_file(Debugger *dbg, const std::string &path)
{
    // Clear id/verified on disabled breakpoints so stale IDs can't match events
    json bp_array = json::array();
    for (auto &bp : dbg->breakpoints) {
        if (bp.path != path) continue;
        if (bp.enabled) {
            bp_array.push_back({{"line", bp.line}});
        } else {
            bp.id = 0;
            bp.verified = false;
        }
    }
    if (dbg->run_to_path == path && dbg->run_to_line > 0) {
        bp_array.push_back({{"line", dbg->run_to_line}});
    }
    int seq = dbg->next_seq;
    dap_request(dbg, "setBreakpoints", {
        {"source", {{"path", path}}},
        {"breakpoints", bp_array},
    });
    dbg->pending_bps[seq] = path;
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
    SourceFile *sf = get_source(dbg, path);
    if (sf && line >= 1 && line <= (int)sf->lines.size()) {
        bp.line_text = sf->lines[(size_t)(line - 1)];
    }
    dbg->breakpoints.push_back(bp);
    if (dbg->state != DapState::IDLE && dbg->state != DapState::TERMINATED) {
        send_breakpoints_for_file(dbg, path);
    }
}

// Returns: 0 = no breakpoint, 1 = enabled, 2 = disabled
static int has_breakpoint(Debugger *dbg, const std::string &path, int line)
{
    for (auto &bp : dbg->breakpoints) {
        if (bp.path == path && bp.line == line)
            return bp.enabled ? 1 : 2;
    }
    return 0;
}

// Strip leading and trailing whitespace from a string_view
static std::string_view strip(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// Relocate breakpoints for a file by matching stored line text against
// current file contents.  Preserves breakpoint ordering within the file.
static void relocate_breakpoints(Debugger *dbg, const std::string &path)
{
    SourceFile *sf = get_source(dbg, path);
    if (!sf) return;

    // Collect breakpoints for this file, sorted by line
    std::vector<LineBreakpoint *> bps;
    for (auto &bp : dbg->breakpoints) {
        if (bp.path == path && !bp.line_text.empty()) bps.push_back(&bp);
    }
    std::sort(bps.begin(), bps.end(),
              [](auto *a, auto *b) { return a->line < b->line; });

    int num_lines = (int)sf->lines.size();
    int min_line = 1;  // order constraint: next match must be >= min_line
    constexpr int MAX_SEARCH = 100;

    for (auto *bp : bps) {
        std::string_view target = strip(bp->line_text);
        int best = -1;
        for (int delta = 0; delta <= MAX_SEARCH; delta++) {
            int candidates[2] = {bp->line + delta, bp->line - delta};
            for (int c : candidates) {
                if (c < min_line || c > num_lines) continue;
                if (strip(sf->lines[(size_t)(c - 1)]) == target) {
                    best = c;
                    goto found;
                }
            }
        }
    found:
        if (best > 0) {
            bp->line = best;
            min_line = best + 1;
        } else {
            // No match — leave in place, clamp to order constraint
            if (bp->line < min_line) bp->line = min_line;
            min_line = bp->line + 1;
        }
    }
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
        dbg->pending_scopes = std::move(new_scopes);
        dbg->pending_scope_reqs = 0;
        for (auto &scope : dbg->pending_scopes) {
            if (scope.variables_ref > 0) {
                fetch_variables(dbg, scope.variables_ref);
                dbg->pending_scope_reqs++;
            } else {
                scope.fetched = true;
            }
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

            // Pending scopes: populate and swap when all levels are ready
            if (!dbg->pending_scopes.empty()) {
                for (auto &scope : dbg->pending_scopes) {
                    if (scope.variables_ref == ref) {
                        scope.variables = std::move(parsed);
                        scope.fetched = true;
                        dbg->pending_scope_reqs--;
                        for (auto &v : scope.variables) {
                            if (v.variables_ref > 0) {
                                fetch_variables(dbg, v.variables_ref);
                                dbg->pending_scope_reqs++;
                            }
                        }
                        if (dbg->pending_scope_reqs == 0)
                            dbg->scopes = std::move(dbg->pending_scopes);
                        goto matched;
                    }
                    DapVariable *target = find_variable_by_ref(
                        scope.variables, ref);
                    if (target) {
                        target->children = std::move(parsed);
                        target->fetched = true;
                        dbg->pending_scope_reqs--;
                        if (dbg->pending_scope_reqs == 0)
                            dbg->scopes = std::move(dbg->pending_scopes);
                        goto matched;
                    }
                }
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
            matched:;
        }
    } else if (command == "setBreakpoints") {
        // Response breakpoints are positional: response[i] corresponds to the
        // i-th enabled breakpoint we sent for that file.
        int req_seq = msg.value("request_seq", 0);
        auto it = dbg->pending_bps.find(req_seq);
        if (it != dbg->pending_bps.end() &&
            msg.contains("body") && msg["body"].contains("breakpoints")) {
            std::string path = it->second;
            dbg->pending_bps.erase(it);

            // Collect pointers to enabled breakpoints for this file (same
            // order as they were sent in send_breakpoints_for_file).
            std::vector<LineBreakpoint *> sent;
            for (auto &bp : dbg->breakpoints) {
                if (bp.path == path && bp.enabled) sent.push_back(&bp);
            }
            auto &resp = msg["body"]["breakpoints"];
            for (size_t i = 0; i < resp.size() && i < sent.size(); i++) {
                sent[i]->id = resp[i].value("id", 0);
                sent[i]->verified = resp[i].value("verified", false);
                int line = resp[i].value("line", 0);
                if (line > 0) sent[i]->line = line;
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

        // Collect unique files, relocate breakpoints, then send
        std::vector<std::string> files;
        for (auto &bp : dbg->breakpoints) {
            bool found = false;
            for (auto &f : files) {
                if (f == bp.path) { found = true; break; }
            }
            if (!found) files.push_back(bp.path);
        }
        if (dbg->run_to_line && !dbg->run_to_path.empty()) {
            bool found = false;
            for (auto &f : files) {
                if (f == dbg->run_to_path) { found = true; break; }
            }
            if (!found) files.push_back(dbg->run_to_path);
        }
        for (auto &f : files) {
            relocate_breakpoints(dbg, f);
            send_breakpoints_for_file(dbg, f);
        }

        if (dbg->pause_at_entry) {
            dap_request(dbg, "pause", {{"threadId", 0}});
        }
        dap_request(dbg, "configurationDone");
    } else if (event == "stopped") {
        auto &body = msg["body"];
        dbg->thread_id = body.value("threadId", dbg->thread_id);
        std::string reason = body.value("reason", "");
        if (reason.empty())
            dbg->status = "Paused";
        else
            dbg->status = "Paused (" + reason + ")";

        dap_request(dbg, "stackTrace", {{"threadId", dbg->thread_id}});

        if (dbg->run_to_line) {
            std::string path = std::move(dbg->run_to_path);
            dbg->run_to_line = 0;
            send_breakpoints_for_file(dbg, path);
        }
    } else if (event == "terminated") {
        dbg->state = DapState::TERMINATED;
        dbg->current_source = nullptr;
        dbg->current_line = 0;
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
                if (id != 0 && bp.id == id) {
                    bp.verified = verified;
                    if (line > 0) bp.line = line;
                }
            }
        }
    } else if (event == "thread") {
        // Thread started/exited -- informational only
    } else if (event == "output") {
        if (msg.contains("body")) {
            dbg->output += msg["body"].value("output", "");
        }
    }
}

static void process_messages(Debugger *dbg)
{
    std::vector<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(dbg->queue_mutex);
        messages.swap(dbg->inbox);
        if (!dbg->stdout_pending.empty()) {
            dbg->output += dbg->stdout_pending;
            dbg->stdout_pending.clear();
        }
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

    // Global keyboard shortcuts (F-keys work regardless of focus, like VS)
    ImGuiIO &io = ImGui::GetIO();
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
    if (ImGui::IsKeyPressed(ImGuiKey_F9)) {
        if (dbg->current_source && dbg->current_line) {
            toggle_breakpoint(dbg, dbg->current_source->path, dbg->current_line);
        }
    }

    // Command line text box
    ImVec2 default_padding = ImGui::GetStyle().FramePadding;
    ImVec2 toolbar_padding(default_padding.x + 2, default_padding.y + 4);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, toolbar_padding);
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(avail_w * 0.5f);
    if (!editable) {
        ImGui::BeginDisabled();
    }
    ImGui::InputTextWithHint("##cmdline", "(CMake arguments)", dbg->cmdline,
                              sizeof(dbg->cmdline));
    if (!editable) {
        ImGui::EndDisabled();
    }

    // Start/Continue button (F5)
    ImGui::SameLine();
    ImGui::BeginDisabled(!editable && !stopped);
    if (editable) {
        if (ImGui::Button(ICON_DEBUG_START)) {
            dbg->pause_at_entry = false;
            dcmake_start(dbg);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                                 ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Start (F5)");
    } else {
        if (ImGui::Button(ICON_DEBUG_CONTINUE)) {
            dap_request(dbg, "continue", {{"threadId", dbg->thread_id}});
            dbg->state = DapState::RUNNING;
            dbg->status = "Running";
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                                 ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Continue (F5)");
    }
    ImGui::EndDisabled();

    // Stop button (Shift+F5)
    ImGui::SameLine();
    ImGui::BeginDisabled(idle);
    if (ImGui::Button(ICON_DEBUG_STOP)) {
        dcmake_stop(dbg);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                             ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Stop (Shift+F5)");
    ImGui::EndDisabled();

    // Step buttons — also start cmake from idle (with pause at entry)
    ImGui::SameLine();
    ImGui::BeginDisabled(!stopped && !editable);
    if (ImGui::Button(ICON_DEBUG_STEP_OVER)) {
        if (editable) {
            dbg->pause_at_entry = true;
            dcmake_start(dbg);
        } else {
            dap_request(dbg, "next", {{"threadId", dbg->thread_id}});
            dbg->state = DapState::RUNNING;
            dbg->status = "Running";
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                             ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Step Over (F10)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_DEBUG_STEP_INTO)) {
        if (editable) {
            dbg->pause_at_entry = true;
            dcmake_start(dbg);
        } else {
            dap_request(dbg, "stepIn", {{"threadId", dbg->thread_id}});
            dbg->state = DapState::RUNNING;
            dbg->status = "Running";
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                             ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Step In (F11)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!stopped);
    if (ImGui::Button(ICON_DEBUG_STEP_OUT)) {
        dap_request(dbg, "stepOut", {{"threadId", dbg->thread_id}});
        dbg->state = DapState::RUNNING;
        dbg->status = "Running";
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                             ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Step Out (Shift+F11)");
    ImGui::EndDisabled();
    ImGui::PopStyleVar();

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

static DapVariable *find_variable_by_name(Debugger *dbg, std::string_view name);

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

    static int context_menu_line = 0;

    ImGuiListClipper clipper;
    clipper.Begin(line_count);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            int line_num = i + 1;
            bool is_current = (line_num == highlight_line);
            int bp_state = has_breakpoint(dbg, sf->path, line_num);

            ImVec2 line_pos = ImGui::GetCursorScreenPos();

            if (is_current) {
                float width = ImGui::GetContentRegionAvail().x +
                              ImGui::GetScrollX();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    line_pos,
                    ImVec2(line_pos.x + width, line_pos.y + line_height),
                    IM_COL32(80, 80, 30, 255));
            }

            if (bp_state == 1) {
                float radius = line_height * 0.3f;
                ImVec2 center(line_pos.x + gutter_width * 0.5f,
                              line_pos.y + line_height * 0.5f);
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    center, radius, IM_COL32(220, 50, 50, 255));
            } else if (bp_state == 2) {
                float radius = line_height * 0.3f;
                ImVec2 center(line_pos.x + gutter_width * 0.5f,
                              line_pos.y + line_height * 0.5f);
                ImGui::GetWindowDrawList()->AddCircle(
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
            float full_width = ImGui::GetContentRegionAvail().x +
                               ImGui::GetScrollX();
            ImVec2 line_max(line_pos.x + full_width,
                            line_pos.y + line_height);
            if (ImGui::IsMouseClicked(1) &&
                ImGui::IsMouseHoveringRect(line_pos, line_max)) {
                context_menu_line = line_num;
                ImGui::OpenPopup("source_context");
            }

            ImGui::SameLine();

            if (is_current) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "->");
            } else {
                ImGui::TextUnformatted("  ");
            }
            ImGui::SameLine();

            bool stopped = dbg->state == DapState::STOPPED;
            auto tokens = tokenize_cmake(sf->lines[(size_t)i]);
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
                    // Variable hover tooltip
                    if (stopped && tokens[t].type == TokenType::DEFAULT &&
                        ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                        std::string_view word = tokens[t].text;
                        DapVariable *var = find_variable_by_name(dbg, word);
                        if (var) {
                            ImGui::BeginTooltip();
                            if (var->value.empty())
                                ImGui::Text("%s : %s",
                                    var->name.c_str(), var->type.c_str());
                            else
                                ImGui::Text("%s = %s",
                                    var->name.c_str(), var->value.c_str());
                            ImGui::EndTooltip();
                        }
                    }
                    if (t + 1 < tokens.size())
                        ImGui::SameLine(0, 0);
                }
            }
        }
    }

    if (ImGui::BeginPopup("source_context")) {
        bool stopped = dbg->state == DapState::STOPPED;
        bool editable = dbg->state == DapState::IDLE ||
                        dbg->state == DapState::TERMINATED;
        if (ImGui::MenuItem("Run to line", nullptr, false,
                            stopped || editable)) {
            dbg->run_to_path = sf->path;
            dbg->run_to_line = context_menu_line;
            if (editable) {
                dbg->pause_at_entry = false;
                dcmake_start(dbg);
            } else {
                send_breakpoints_for_file(dbg, sf->path);
                dap_request(dbg, "continue", {{"threadId", dbg->thread_id}});
                dbg->state = DapState::RUNNING;
                dbg->status = "Running";
            }
        }
        ImGui::EndPopup();
    }

    if (scroll_to && highlight_line > 0) {
        float target_y = (float)(highlight_line - 1) * line_height;
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
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", os.path.c_str());
            ImGui::End();
            continue;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", os.path.c_str());

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
    if (!dbg->show_stack) return;
    if (!ImGui::Begin("Call Stack", &dbg->show_stack)) {
        ImGui::End();
        return;
    }

    for (int i = 0; i < (int)dbg->stack.size(); i++) {
        auto &f = dbg->stack[(size_t)i];
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

// Case-insensitive substring search
static bool icontains(const std::string &haystack, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0) return true;
    if (haystack.size() < nlen) return false;
    for (size_t i = 0; i <= haystack.size() - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// Emit variable rows into an already-open table (recursive)
// Read-only InputText that fills the column width.
// Safe to const_cast: ImGui never writes back in ReadOnly mode.
static void selectable_text(const char *label, const char *text, size_t len)
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText(label, const_cast<char *>(text), len + 1,
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor(2);
}

static void render_variable_rows(Debugger *dbg, std::vector<DapVariable> &vars,
                                  const char *filter = "", int depth = 0)
{
    for (auto &v : vars) {
        if (depth == 0 && filter[0]) {
            if (!icontains(v.name, filter) && !icontains(v.value, filter))
                continue;
        }
        ImGui::PushID(v.name.c_str());
        if (v.variables_ref > 0) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool open = ImGui::TreeNode(v.name.c_str());
            ImGui::TableNextColumn();
            const char *val = v.value.empty() ? v.type.c_str()
                                              : v.value.c_str();
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            selectable_text("##val", val, strlen(val));
            ImGui::PopStyleColor();
            if (open) {
                if (!v.fetched && v.children.empty()) {
                    fetch_variables(dbg, v.variables_ref);
                    v.fetched = true;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("Loading...");
                    ImGui::TableNextColumn();
                } else {
                    render_variable_rows(dbg, v.children, filter, depth + 1);
                }
                ImGui::TreePop();
            }
        } else if (v.value.find(';') != std::string::npos) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool open = ImGui::TreeNode(v.name.c_str());
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            selectable_text("##val", v.value.c_str(), v.value.size());
            ImGui::PopStyleColor();
            if (open) {
                size_t idx = 0;
                size_t start = 0;
                for (;;) {
                    size_t semi = v.value.find(';', start);
                    size_t end = (semi != std::string::npos) ? semi
                                                             : v.value.size();
                    std::string item(v.value.data() + start, end - start);
                    ImGui::PushID((int)idx);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::BulletText("[%zu]", idx++);
                    ImGui::TableNextColumn();
                    selectable_text("##val", item.c_str(), item.size());
                    ImGui::PopID();
                    if (semi == std::string::npos) break;
                    start = semi + 1;
                }
                ImGui::TreePop();
            }
        } else {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Bullet();
            ImGui::SameLine();
            selectable_text("##name", v.name.c_str(), v.name.size());
            ImGui::TableNextColumn();
            selectable_text("##val", v.value.c_str(), v.value.size());
        }
        ImGui::PopID();
    }
}

// Render a variable tree with resizable name/value columns
static void render_variable_tree(Debugger *dbg, std::vector<DapVariable> &vars,
                                  const char *filter = "")
{
    if (ImGui::BeginTable("##vars", 2,
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Value");
        render_variable_rows(dbg, vars, filter);
        ImGui::EndTable();
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

// Look up a variable by name in locals then cache (for hover tooltips)
static DapVariable *find_variable_by_name(Debugger *dbg, std::string_view name)
{
    static const char *scope_names[] = {"Locals", "CacheVariables"};
    for (auto *sn : scope_names) {
        DapVariable *scope = find_scope_child(dbg, sn);
        if (!scope) continue;
        for (auto &v : scope->children) {
            if (v.name == name) return &v;
        }
    }
    return nullptr;
}

static void render_locals(Debugger *dbg)
{
    if (!dbg->show_locals) return;
    if (!ImGui::Begin("Locals", &dbg->show_locals)) {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter...",
                             dbg->filter_locals, sizeof(dbg->filter_locals));

    if (dbg->state == DapState::STOPPED || dbg->state == DapState::RUNNING) {
        DapVariable *locals = find_scope_child(dbg, "Locals");
        if (locals) {
            if (!locals->fetched && locals->children.empty()) {
                fetch_variables(dbg, locals->variables_ref);
                locals->fetched = true;
            }
            render_variable_tree(dbg, locals->children, dbg->filter_locals);
        } else if (dbg->scopes.empty()) {
            ImGui::TextDisabled("No scope data.");
        } else {
            for (auto &scope : dbg->scopes) {
                render_variable_tree(dbg, scope.variables, dbg->filter_locals);
            }
        }
    } else {
        ImGui::TextDisabled("Not stopped.");
    }

    ImGui::End();
}

static void render_cache(Debugger *dbg)
{
    if (!dbg->show_cache) return;
    if (!ImGui::Begin("Cache Variables", &dbg->show_cache)) {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter...",
                             dbg->filter_cache, sizeof(dbg->filter_cache));

    if (dbg->state == DapState::STOPPED || dbg->state == DapState::RUNNING) {
        DapVariable *cache = find_scope_child(dbg, "CacheVariables");
        if (cache) {
            if (!cache->fetched && cache->children.empty()) {
                fetch_variables(dbg, cache->variables_ref);
                cache->fetched = true;
            }
            render_variable_tree(dbg, cache->children, dbg->filter_cache);
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

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter...",
                             dbg->filter_targets, sizeof(dbg->filter_targets));

    if (dbg->state == DapState::STOPPED || dbg->state == DapState::RUNNING) {
        DapVariable *targets = find_scope_child(dbg, "Targets");
        if (targets) {
            if (!targets->fetched && targets->children.empty()) {
                fetch_variables(dbg, targets->variables_ref);
                targets->fetched = true;
            }
            render_variable_tree(dbg, targets->children, dbg->filter_targets);
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
    if (!dbg->show_tests) return;
    if (!ImGui::Begin("Tests", &dbg->show_tests)) {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter...",
                             dbg->filter_tests, sizeof(dbg->filter_tests));

    if (dbg->state == DapState::STOPPED || dbg->state == DapState::RUNNING) {
        DapVariable *tests = find_scope_child(dbg, "Tests");
        if (tests) {
            if (!tests->fetched && tests->children.empty()) {
                fetch_variables(dbg, tests->variables_ref);
                tests->fetched = true;
            }
            render_variable_tree(dbg, tests->children, dbg->filter_tests);
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
    if (!dbg->show_breakpoints) return;
    if (!ImGui::Begin("Breakpoints", &dbg->show_breakpoints)) {
        ImGui::End();
        return;
    }

    int remove_idx = -1;
    for (int i = 0; i < (int)dbg->breakpoints.size(); i++) {
        auto &bp = dbg->breakpoints[(size_t)i];
        ImGui::PushID(i);

        // Extract filename from path
        const char *filename = bp.path.c_str();
        const char *slash = strrchr(filename, '/');
        if (!slash) slash = strrchr(filename, '\\');
        if (slash) filename = slash + 1;

        char label[512];
        snprintf(label, sizeof(label), "%s %s:%d",
                 bp.verified ? "*" : "?", filename, bp.line);
        if (ImGui::Checkbox(label, &bp.enabled)) {
            if (dbg->state != DapState::IDLE &&
                dbg->state != DapState::TERMINATED) {
                send_breakpoints_for_file(dbg, bp.path);
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s:%d", bp.path.c_str(), bp.line);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            remove_idx = i;
        }
        ImGui::PopID();
    }

    if (remove_idx >= 0) {
        std::string path = dbg->breakpoints[(size_t)remove_idx].path;
        dbg->breakpoints.erase(dbg->breakpoints.begin() + remove_idx);
        if (dbg->state != DapState::IDLE &&
            dbg->state != DapState::TERMINATED) {
            send_breakpoints_for_file(dbg, path);
        }
    }

    ImGui::End();
}

static void render_filters_panel(Debugger *dbg)
{
    if (!dbg->show_filters) return;
    if (!ImGui::Begin("Exception Filters", &dbg->show_filters)) {
        ImGui::End();
        return;
    }

    for (auto &ef : dbg->exception_filters) {
        if (ImGui::Checkbox(ef.label.c_str(), &ef.enabled)) {
            if (dbg->state != DapState::IDLE &&
                dbg->state != DapState::TERMINATED) {
                send_exception_breakpoints(dbg);
            }
        }
    }

    ImGui::End();
}

static void render_output(Debugger *dbg)
{
    if (!dbg->show_output) return;
    if (!ImGui::Begin("Output", &dbg->show_output)) {
        ImGui::End();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::InputTextMultiline("##output", dbg->output.data(),
                              dbg->output.size() + 1,
                              ImVec2(-1, -1),
                              ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor();

    ImGui::End();
}

static void render_ui(Debugger *dbg)
{
    static bool show_about = false;

    // Keyboard shortcuts (global, skip when typing)
    ImGuiIO &menu_io = ImGui::GetIO();
    bool ctrl = menu_io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
        std::string path = platform_open_file_dialog();
        if (!path.empty()) open_source(dbg, path);
        ImGui::ClearActiveID();
        menu_io.ClearInputKeys();
    }

    // Main menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open File...", "Ctrl+O")) {
                std::string path = platform_open_file_dialog();
                if (!path.empty()) open_source(dbg, path);
            }
            if (ImGui::MenuItem("Set Working Directory...")) {
                std::string dir = platform_open_directory_dialog();
                if (!dir.empty()) {
                    platform_chdir(dir.c_str());
                    dbg->title_dirty = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4"))
                dbg->want_quit = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Call Stack", nullptr, &dbg->show_stack);
            ImGui::MenuItem("Locals", nullptr, &dbg->show_locals);
            ImGui::MenuItem("Cache Variables", nullptr, &dbg->show_cache);
            ImGui::MenuItem("Targets", nullptr, &dbg->show_targets);
            ImGui::MenuItem("Tests", nullptr, &dbg->show_tests);
            ImGui::MenuItem("Breakpoints", nullptr, &dbg->show_breakpoints);
            ImGui::MenuItem("Exception Filters", nullptr, &dbg->show_filters);
            ImGui::MenuItem("Output", nullptr, &dbg->show_output);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About"))
                show_about = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (show_about) {
        ImGui::OpenPopup("About dcmake");
        show_about = false;
    }
    if (ImGui::BeginPopupModal("About dcmake", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("dcmake: CMake Debugger");
        ImGui::Text("Version %s", DCMAKE_VERSION);
        ImGui::TextLinkOpenURL("https://github.com/skeeto/dcmake");
        ImGui::Spacing();
        ImGui::SeparatorText("Third-party licenses");
        ImGui::BulletText(
            "Dear ImGui -- Copyright (c) 2014-2026 Omar Cornut (MIT)");
#ifdef DCMAKE_GLFW
        ImGui::BulletText(
            "GLFW -- Copyright (c) 2006-2019 Camilla Loewy (zlib/libpng)");
#endif
        ImGui::BulletText(
            "nlohmann/json -- Copyright (c) 2013-2023 Niels Lohmann (MIT)");
        ImGui::BulletText(
            "Codicons -- Copyright (c) Microsoft Corporation (MIT)");
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Toolbar window (fixed at top, not dockable)
    ImGuiViewport *vp = ImGui::GetMainViewport();
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

        ImGuiID left = 0, right = 0;
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.5f,
                                    &right, &left);
        ImGuiID left_top = 0, left_bottom = 0;
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.33f,
                                    &left_bottom, &left_top);
        ImGuiID right_top = 0, right_bottom = 0;
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.33f,
                                    &right_bottom, &right_top);

        dbg->source_dock_id = left_top;
        ImGui::DockBuilderDockWindow("Output", left_bottom);
        ImGui::DockBuilderDockWindow("Locals", right_top);
        ImGui::DockBuilderDockWindow("Cache Variables", right_top);
        ImGui::DockBuilderDockWindow("Targets", right_top);
        ImGui::DockBuilderDockWindow("Tests", right_top);
        ImGui::DockBuilderDockWindow("Breakpoints", right_bottom);
        ImGui::DockBuilderDockWindow("Call Stack", right_bottom);
        ImGui::DockBuilderDockWindow("Exception Filters", right_bottom);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0, 0),
                     ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    // Render all panels
    render_sources(dbg);
    render_locals(dbg);
    render_cache(dbg);
    render_targets(dbg);
    render_tests(dbg);
    render_breakpoints_panel(dbg);
    render_stack(dbg);
    render_filters_panel(dbg);
    render_output(dbg);
}

// --- Config persistence ---

static std::string config_path(Debugger *dbg)
{
    // Derive from ini_path: replace "imgui.ini" with "dcmake.ini"
    std::string path = dbg->ini_path;
    auto pos = path.rfind("imgui.ini");
    if (pos != std::string::npos) {
        path.replace(pos, 9, "dcmake.ini");
    }
    return path;
}

static void save_config(Debugger *dbg)
{
    std::string out;
    out += "show_stack=";       out += dbg->show_stack ? '1' : '0';       out += '\n';
    out += "show_locals=";      out += dbg->show_locals ? '1' : '0';      out += '\n';
    out += "show_cache=";       out += dbg->show_cache ? '1' : '0';       out += '\n';
    out += "show_targets=";     out += dbg->show_targets ? '1' : '0';     out += '\n';
    out += "show_tests=";       out += dbg->show_tests ? '1' : '0';       out += '\n';
    out += "show_breakpoints="; out += dbg->show_breakpoints ? '1' : '0'; out += '\n';
    out += "show_filters=";     out += dbg->show_filters ? '1' : '0';     out += '\n';
    out += "show_output=";      out += dbg->show_output ? '1' : '0';      out += '\n';

    for (auto &bp : dbg->breakpoints) {
        out += "bp\t";
        out += bp.path;      out += '\t';
        out += std::to_string(bp.line); out += '\t';
        out += bp.enabled ? '1' : '0';  out += '\t';
        out += bp.line_text; out += '\n';
    }

    platform_write_file(config_path(dbg).c_str(), out.data(), out.size());
}

void dcmake_load_config(Debugger *dbg)
{
    std::string content = platform_read_file(config_path(dbg).c_str());
    if (content.empty()) return;

    size_t pos = 0;
    while (pos < content.size()) {
        size_t nl = content.find('\n', pos);
        size_t end = (nl == std::string::npos) ? content.size() : nl;
        if (end > pos && content[end - 1] == '\r') end--;
        std::string line = content.substr(pos, end - pos);
        pos = (nl == std::string::npos) ? content.size() : nl + 1;

        if (line.starts_with("bp\t")) {
            size_t p1 = 3;
            size_t p2 = line.find('\t', p1);
            if (p2 == std::string::npos) continue;
            size_t p3 = line.find('\t', p2 + 1);
            if (p3 == std::string::npos) continue;
            size_t p4 = line.find('\t', p3 + 1);
            if (p4 == std::string::npos) continue;

            LineBreakpoint bp;
            bp.path = line.substr(p1, p2 - p1);
            bp.line = std::atoi(line.c_str() + p2 + 1);
            bp.enabled = line[p3 + 1] == '1';
            bp.line_text = line.substr(p4 + 1);
            if (!bp.path.empty() && bp.line > 0) {
                dbg->breakpoints.push_back(std::move(bp));
            }
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        bool val = line.substr(eq + 1) == "1";
        if (key == "show_stack") dbg->show_stack = val;
        else if (key == "show_locals") dbg->show_locals = val;
        else if (key == "show_cache") dbg->show_cache = val;
        else if (key == "show_targets") dbg->show_targets = val;
        else if (key == "show_tests") dbg->show_tests = val;
        else if (key == "show_breakpoints") dbg->show_breakpoints = val;
        else if (key == "show_filters") dbg->show_filters = val;
        else if (key == "show_output") dbg->show_output = val;
    }
}

// --- Lifecycle ---

void dcmake_init(Debugger *dbg)
{
    // Merge codicon icons into default font
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.GlyphOffset = ImVec2(0, 4);
    static const ImWchar icon_ranges[] = { ICON_CODICON_MIN, ICON_CODICON_MAX, 0 };
    io.Fonts->AddFontFromMemoryCompressedTTF(
        icon_compressed_data, sizeof(icon_compressed_data),
        14.0f, &cfg, icon_ranges);

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
    dbg->output.clear();
    dbg->stdout_pending.clear();
    dbg->filter_locals[0] = '\0';
    dbg->filter_cache[0] = '\0';
    dbg->filter_targets[0] = '\0';
    dbg->filter_tests[0] = '\0';

    dbg->state = DapState::CONNECTING;
    dbg->status = "Running";

    if (!platform_launch(dbg, dbg->cmdline)) {
        dbg->state = DapState::TERMINATED;
        return;
    }

    // Start reader threads
    dbg->reader_running.store(true);
    dbg->reader_thread = std::thread(reader_thread_func, dbg);
    if (dbg->stdout_read) {
        dbg->stdout_running.store(true);
        dbg->stdout_thread = std::thread(stdout_thread_func, dbg);
    }

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

    dbg->stdout_running.store(false);
    if (dbg->stdout_shutdown) {
        dbg->stdout_shutdown(dbg->platform);
    }
    if (dbg->stdout_thread.joinable()) {
        dbg->stdout_thread.join();
    }

    platform_cleanup(dbg);

    dbg->pipe_read = nullptr;
    dbg->pipe_write = nullptr;
    dbg->pipe_shutdown = nullptr;
    dbg->stdout_read = nullptr;
    dbg->stdout_shutdown = nullptr;

    dbg->state = DapState::IDLE;
    dbg->current_source = nullptr;
    dbg->current_line = 0;
    dbg->pending_scopes.clear();
    dbg->pending_scope_reqs = 0;
    dbg->status = "Stopped";
}

void dcmake_frame(Debugger *dbg)
{
    process_messages(dbg);
    render_ui(dbg);

    ImGuiIO &io = ImGui::GetIO();
    if (io.WantSaveIniSettings) {
        io.WantSaveIniSettings = false;
        size_t ini_size = 0;
        const char *ini_data = ImGui::SaveIniSettingsToMemory(&ini_size);
        platform_write_file(dbg->ini_path.c_str(), ini_data, ini_size);
    }
}

void dcmake_shutdown(Debugger *dbg)
{
    save_config(dbg);

    // Force-save imgui.ini since IniFilename is null
    {
        size_t ini_size = 0;
        const char *ini_data = ImGui::SaveIniSettingsToMemory(&ini_size);
        platform_write_file(dbg->ini_path.c_str(), ini_data, ini_size);
    }

    // Send disconnect if still connected
    if (dbg->pipe_write && dbg->state != DapState::TERMINATED &&
        dbg->state != DapState::IDLE) {
        dap_request(dbg, "disconnect");
    }

    dcmake_stop(dbg);
}
