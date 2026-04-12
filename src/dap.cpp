#include "dap.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>

using json = nlohmann::json;

// --- DAP wire protocol ---

static void dap_send(Debugger *dbg, const json &msg)
{
    std::string body = msg.dump();

    // Log outbound message
    {
        std::string type = msg.value("type", "?");
        std::string name = msg.value("command", "?");
        Debugger::DapMessage dm;
        dm.sent = true;
        dm.summary = "\xe2\x86\x92 " + type + " " + name;
        dm.raw = body;
        dm.timestamp = platform_now_iso8601();
        dbg->dap_log.push_back(std::move(dm));
    }

    char header[64];
    int hlen = snprintf(header, sizeof(header),
                        "Content-Length: %zu\r\n\r\n", body.size());
    if (dbg->pipe_write) {
        dbg->pipe_write(dbg->platform, header, hlen);
        dbg->pipe_write(dbg->platform, body.data(), (int)body.size());
    }
}

void dap_request(Debugger *dbg, const char *command,
                 json arguments)
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
void reader_thread_func(Debugger *dbg)
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

void stdout_thread_func(Debugger *dbg)
{
    char tmp[4096];
    int skip_lines = 3;  // cmake debugger preamble
    while (dbg->stdout_running.load()) {
        int n = dbg->stdout_read(dbg->platform, tmp, sizeof(tmp));
        if (n < 0) continue;   // poll timeout — recheck running flag
        if (n == 0) break;     // EOF
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

SourceFile *get_source(Debugger *dbg, const std::string &path)
{
    std::string norm = platform_realpath(path);
    for (auto &sf : dbg->sources) {
        if (sf.path == norm) return &sf;
    }

    std::string content = platform_read_file(norm.c_str());
    if (content.empty()) return nullptr;

    SourceFile sf;
    sf.path = norm;
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

void send_breakpoints_for_file(Debugger *dbg, const std::string &path)
{
    // Clear id on disabled breakpoints so stale IDs can't match events
    json bp_array = json::array();
    for (auto &bp : dbg->breakpoints) {
        if (bp.path != path) continue;
        if (bp.enabled) {
            bp_array.push_back({{"line", bp.line}});
        } else {
            bp.id = 0;
        }
    }
    if (dbg->run_to_path == path && dbg->run_to_line > 0) {
        bp_array.push_back({{"line", dbg->run_to_line}});
    }
    auto it = dbg->cmake_paths.find(path);
    const std::string &dap_path = (it != dbg->cmake_paths.end()) ? it->second : path;
    int seq = dbg->next_seq;
    dap_request(dbg, "setBreakpoints", {
        {"source", {{"path", dap_path}}},
        {"breakpoints", bp_array},
    });
    dbg->pending_bps[seq] = path;
}

void send_exception_breakpoints(Debugger *dbg)
{
    json filters = json::array();
    for (auto &ef : dbg->exception_filters) {
        if (ef.enabled) filters.push_back(ef.filter);
    }
    dap_request(dbg, "setExceptionBreakpoints", {{"filters", filters}});
}

void toggle_breakpoint(Debugger *dbg, const std::string &path, int line)
{
    std::string norm = platform_realpath(path);
    // Remove if exists
    for (auto it = dbg->breakpoints.begin(); it != dbg->breakpoints.end(); ++it) {
        if (it->path == norm && it->line == line) {
            dbg->breakpoints.erase(it);
            if (dbg->state != DapState::IDLE && dbg->state != DapState::TERMINATED) {
                send_breakpoints_for_file(dbg, norm);
            }
            return;
        }
    }
    // Add new
    LineBreakpoint bp;
    bp.path = norm;
    bp.line = line;
    SourceFile *sf = get_source(dbg, path);
    if (sf && line >= 1 && line <= (int)sf->lines.size()) {
        bp.line_text = sf->lines[(size_t)(line - 1)];
    }
    dbg->breakpoints.push_back(bp);
    if (dbg->state != DapState::IDLE && dbg->state != DapState::TERMINATED) {
        send_breakpoints_for_file(dbg, norm);
    }
}

// Returns: 0 = no breakpoint, 1 = enabled, 2 = disabled
int has_breakpoint(Debugger *dbg, const std::string &path, int line)
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
void relocate_breakpoints(Debugger *dbg, const std::string &path)
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

void open_source(Debugger *dbg, const std::string &path)
{
    std::string norm = platform_realpath(path);
    for (auto &os : dbg->open_sources) {
        if (os.path == norm) {
            os.focus = true;
            return;
        }
    }
    OpenSource os;
    os.path = norm;
    os.focus = true;
    dbg->open_sources.push_back(std::move(os));
}

// --- Variable fetch helpers ---

void fetch_variables(Debugger *dbg, int64_t ref)
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

// Mark variables in `next` that are new or have a different value from `prev`.
static void mark_changed_variables(std::vector<DapVariable> &prev,
                                   std::vector<DapVariable> &next)
{
    // Build name→value lookup from old variables
    std::unordered_map<std::string, std::string> old_vals;
    for (auto &v : prev)
        old_vals[v.name] = v.value;

    for (auto &v : next) {
        auto it = old_vals.find(v.name);
        v.changed = (it == old_vals.end() || it->second != v.value);
    }
}

static void mark_changed_scopes(std::vector<DapScope> &prev,
                                std::vector<DapScope> &next)
{
    for (auto &ns : next) {
        // Find matching old scope by name
        DapScope *os = nullptr;
        for (auto &s : prev) {
            if (s.name == ns.name) { os = &s; break; }
        }
        if (os) {
            mark_changed_variables(os->variables, ns.variables);
            // Also mark children of matching variables
            for (auto &nv : ns.variables) {
                for (auto &ov : os->variables) {
                    if (ov.name == nv.name) {
                        mark_changed_variables(ov.children, nv.children);
                        break;
                    }
                }
            }
        } else {
            // Entirely new scope — mark everything changed
            for (auto &v : ns.variables)
                v.changed = true;
        }
    }
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
        std::vector<std::string> resend;
        for (auto &frame : body["stackFrames"]) {
            StackFrame sf;
            sf.id = frame.value("id", 0);
            sf.name = frame.value("name", "");
            if (frame.contains("source") && frame["source"].contains("path")) {
                std::string raw = frame["source"]["path"];
                sf.source_path = platform_realpath(raw);
                if (raw != sf.source_path) {
                    auto [it, inserted] = dbg->cmake_paths.emplace(
                        sf.source_path, raw);
                    if (inserted || it->second != raw) {
                        it->second = raw;
                        resend.push_back(sf.source_path);
                    }
                }
            }
            sf.line = frame.value("line", 0);
            new_stack.push_back(std::move(sf));
        }
        dbg->stack = std::move(new_stack);

        // Re-send breakpoints for files where we learned a new CMake name
        for (auto &f : resend) {
            for (auto &bp : dbg->breakpoints) {
                if (bp.path == f) {
                    send_breakpoints_for_file(dbg, f);
                    break;
                }
            }
        }
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
                        if (dbg->pending_scope_reqs == 0) {
                            mark_changed_scopes(dbg->scopes,
                                                dbg->pending_scopes);
                            dbg->scopes = std::move(dbg->pending_scopes);
                        }
                        goto matched;
                    }
                    DapVariable *target = find_variable_by_ref(
                        scope.variables, ref);
                    if (target) {
                        target->children = std::move(parsed);
                        target->fetched = true;
                        dbg->pending_scope_reqs--;
                        if (dbg->pending_scope_reqs == 0) {
                            mark_changed_scopes(dbg->scopes,
                                                dbg->pending_scopes);
                            dbg->scopes = std::move(dbg->pending_scopes);
                        }
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
        // Breakpoint line changed
        if (msg.contains("body") && msg["body"].contains("breakpoint")) {
            auto &rbp = msg["body"]["breakpoint"];
            int id = rbp.value("id", 0);
            int line = rbp.value("line", 0);
            for (auto &bp : dbg->breakpoints) {
                if (id != 0 && bp.id == id) {
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

void process_messages(Debugger *dbg)
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

        // Log inbound message
        {
            std::string type = msg.value("type", "?");
            std::string name = (type == "event")
                ? msg.value("event", "?")
                : msg.value("command", "?");
            Debugger::DapMessage dm;
            dm.sent = false;
            dm.summary = "\xe2\x86\x90 " + type + " " + name;
            dm.raw = raw;
            dm.timestamp = platform_now_iso8601();
            dbg->dap_log.push_back(std::move(dm));
        }

        std::string type = msg.value("type", "");
        if (type == "response") {
            handle_response(dbg, msg);
        } else if (type == "event") {
            handle_event(dbg, msg);
        }
    }

    // Detect dead reader thread (pipe closed) — clean up the session
    if (!dbg->reader_running.load() && dbg->state != DapState::IDLE) {
        if (dbg->status.find("Exited") == std::string::npos &&
            dbg->status.find("exit") == std::string::npos) {
            dbg->status = "Stopped";
        }
        dcmake_stop(dbg);
    }
}
