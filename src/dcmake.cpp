#include "dcmake.hpp"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>

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

static void render_source(Debugger *dbg)
{
    if (!ImGui::Begin("Source", &dbg->show_source)) {
        ImGui::End();
        return;
    }

    if (dbg->current_source && !dbg->current_source->lines.empty()) {
        int line_count = (int)dbg->current_source->lines.size();
        float line_height = ImGui::GetTextLineHeightWithSpacing();

        int gutter_digits = 1;
        for (int n = line_count; n >= 10; n /= 10) gutter_digits++;

        // Measure gutter width for click detection
        char gutter_buf[16];
        snprintf(gutter_buf, sizeof(gutter_buf), "%*d", gutter_digits, line_count);
        float gutter_width = ImGui::CalcTextSize(gutter_buf).x;

        ImGuiListClipper clipper;
        clipper.Begin(line_count);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                int line_num = i + 1;
                bool is_current = (line_num == dbg->current_line);
                bool is_bp = dbg->current_source &&
                    has_breakpoint(dbg, dbg->current_source->path, line_num);

                ImVec2 line_pos = ImGui::GetCursorScreenPos();

                if (is_current) {
                    float width = ImGui::GetContentRegionAvail().x +
                                  ImGui::GetScrollX();
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        line_pos,
                        ImVec2(line_pos.x + width, line_pos.y + line_height),
                        IM_COL32(80, 80, 30, 255));
                }

                // Breakpoint indicator (red circle in gutter)
                if (is_bp) {
                    float radius = line_height * 0.3f;
                    ImVec2 center(line_pos.x + gutter_width * 0.5f,
                                  line_pos.y + line_height * 0.5f);
                    ImGui::GetWindowDrawList()->AddCircleFilled(
                        center, radius, IM_COL32(220, 50, 50, 255));
                }

                ImGui::TextDisabled("%*d", gutter_digits, line_num);

                // Gutter click to toggle breakpoint
                ImVec2 gutter_min = line_pos;
                ImVec2 gutter_max(line_pos.x + gutter_width,
                                  line_pos.y + line_height);
                if (ImGui::IsMouseClicked(0) &&
                    ImGui::IsMouseHoveringRect(gutter_min, gutter_max)) {
                    toggle_breakpoint(dbg, dbg->current_source->path, line_num);
                }

                ImGui::SameLine();

                if (is_current) {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "->");
                } else {
                    ImGui::TextUnformatted("  ");
                }
                ImGui::SameLine();

                ImGui::TextUnformatted(dbg->current_source->lines[i].c_str());
            }
        }

        if (dbg->scroll_to_line && dbg->current_line > 0) {
            float target_y = (dbg->current_line - 1) * line_height;
            float window_h = ImGui::GetWindowHeight();
            ImGui::SetScrollY(target_y - window_h / 2.0f);
            dbg->scroll_to_line = false;
        }
    } else if (dbg->state == DapState::IDLE || dbg->state == DapState::CONNECTING ||
               dbg->state == DapState::INITIALIZING) {
        ImGui::TextDisabled("Waiting for debugger connection...");
    } else if (dbg->state == DapState::TERMINATED) {
        ImGui::TextDisabled("Session ended.");
    }

    ImGui::End();
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
            // Click to navigate to that frame's source
            if (!f.source_path.empty()) {
                dbg->current_source = get_source(dbg, f.source_path);
                dbg->current_line = f.line;
                dbg->scroll_to_line = true;
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
    // Main menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Source", nullptr, &dbg->show_source);
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
    ImGuiID dockspace_id = ImGui::GetID("DockSpace");

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

        ImGui::DockBuilderDockWindow("Source", center);
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
    render_source(dbg);
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
