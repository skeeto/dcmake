#include "dcmake.h"

#include <cstdio>
#include <cstring>
#include <fstream>

#include <imgui.h>
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

            int content_length = 0;
            std::string_view headers(buf.data(), sep);
            auto cl = headers.find("Content-Length:");
            if (cl == std::string_view::npos) {
                buf.erase(0, sep + 4);
                continue;
            }
            content_length = std::atoi(buf.data() + cl + 15);

            size_t msg_start = sep + 4;
            size_t msg_end = msg_start + content_length;
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
        // Capabilities received; wait for initialized event
    } else if (command == "configurationDone") {
        // Wait for stopped event
    } else if (command == "stackTrace") {
        auto &body = msg["body"];
        dbg->stack.clear();
        for (auto &frame : body["stackFrames"]) {
            StackFrame sf;
            sf.id = frame.value("id", 0);
            sf.name = frame.value("name", "");
            if (frame.contains("source") && frame["source"].contains("path")) {
                sf.source_path = frame["source"]["path"];
            }
            sf.line = frame.value("line", 0);
            dbg->stack.push_back(std::move(sf));
        }
        if (!dbg->stack.empty()) {
            auto &top = dbg->stack[0];
            if (!top.source_path.empty()) {
                dbg->current_source = get_source(dbg, top.source_path);
                dbg->current_line = top.line;
                dbg->scroll_to_line = true;
                dbg->status = top.source_path + ":" + std::to_string(top.line);
            }
        }
        dbg->state = DapState::STOPPED;
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
        // Send pause before configurationDone so cmake stops at the first line.
        // CMake's adapter processes requests in order on its session thread:
        // pause sets PauseRequest flag, then configurationDone unblocks cmake,
        // and cmake immediately hits the flag on the first function call.
        dap_request(dbg, "pause", {{"threadId", 0}});
        dap_request(dbg, "configurationDone");
    } else if (event == "stopped") {
        auto &body = msg["body"];
        dbg->thread_id = body.value("threadId", dbg->thread_id);
        std::string reason = body.value("reason", "");
        dbg->status = "Stopped (" + reason + ")";

        dap_request(dbg, "stackTrace", {{"threadId", dbg->thread_id}});
    } else if (event == "terminated") {
        dbg->state = DapState::TERMINATED;
        dbg->status = "Terminated";
        dap_request(dbg, "disconnect");
    } else if (event == "exited") {
        auto &body = msg["body"];
        int code = body.value("exitCode", -1);
        dbg->status = "Exited (code " + std::to_string(code) + ")";
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
            dbg->status = "Connection lost";
        }
    }
}

// --- ImGui UI ---

static void render_ui(Debugger *dbg)
{
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("dcmake", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Toolbar
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
                dcmake_start(dbg);
            } else if (stopped) {
                dap_request(dbg, "continue", {{"threadId", dbg->thread_id}});
                dbg->state = DapState::RUNNING;
                dbg->status = "Running...";
            }
        }
        if (stopped && ImGui::IsKeyPressed(ImGuiKey_F10)) {
            dap_request(dbg, "next", {{"threadId", dbg->thread_id}});
            dbg->state = DapState::RUNNING;
            dbg->status = "Running...";
        }
        if (stopped && ImGui::IsKeyPressed(ImGuiKey_F11)) {
            if (io.KeyShift) {
                dap_request(dbg, "stepOut", {{"threadId", dbg->thread_id}});
            } else {
                dap_request(dbg, "stepIn", {{"threadId", dbg->thread_id}});
            }
            dbg->state = DapState::RUNNING;
            dbg->status = "Running...";
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

    // Start/Stop button
    ImGui::SameLine();
    if (editable) {
        if (ImGui::Button("Start")) {
            dcmake_start(dbg);
        }
    } else {
        if (ImGui::Button("Stop")) {
            dcmake_stop(dbg);
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!stopped);
    if (ImGui::Button("Continue")) {
        dap_request(dbg, "continue", {{"threadId", dbg->thread_id}});
        dbg->state = DapState::RUNNING;
        dbg->status = "Running...";
    }
    ImGui::SameLine();
    if (ImGui::Button("Step Over")) {
        dap_request(dbg, "next", {{"threadId", dbg->thread_id}});
        dbg->state = DapState::RUNNING;
        dbg->status = "Running...";
    }
    ImGui::SameLine();
    if (ImGui::Button("Step In")) {
        dap_request(dbg, "stepIn", {{"threadId", dbg->thread_id}});
        dbg->state = DapState::RUNNING;
        dbg->status = "Running...";
    }
    ImGui::SameLine();
    if (ImGui::Button("Step Out")) {
        dap_request(dbg, "stepOut", {{"threadId", dbg->thread_id}});
        dbg->state = DapState::RUNNING;
        dbg->status = "Running...";
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Text(" | ");
    ImGui::SameLine();
    ImGui::TextUnformatted(dbg->status.c_str());

    ImGui::Separator();

    // Source view + stack trace split
    float stack_height = ImGui::GetTextLineHeightWithSpacing() *
                         (float)(dbg->stack.size() + 2);
    float avail = ImGui::GetContentRegionAvail().y;
    float source_height = avail - stack_height;
    if (source_height < avail * 0.3f) source_height = avail * 0.7f;

    // Source view
    ImGui::BeginChild("Source", ImVec2(0, source_height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (dbg->current_source && !dbg->current_source->lines.empty()) {
        int line_count = (int)dbg->current_source->lines.size();
        float line_height = ImGui::GetTextLineHeightWithSpacing();

        int gutter_digits = 1;
        for (int n = line_count; n >= 10; n /= 10) gutter_digits++;

        ImGuiListClipper clipper;
        clipper.Begin(line_count);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                int line_num = i + 1;
                bool is_current = (line_num == dbg->current_line);

                if (is_current) {
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    float width = ImGui::GetContentRegionAvail().x +
                                  ImGui::GetScrollX();
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        pos,
                        ImVec2(pos.x + width, pos.y + line_height),
                        IM_COL32(80, 80, 30, 255));
                }

                ImGui::TextDisabled("%*d", gutter_digits, line_num);
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
    ImGui::EndChild();

    // Stack trace panel
    ImGui::BeginChild("Stack", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Call Stack");
    for (int i = 0; i < (int)dbg->stack.size(); i++) {
        auto &f = dbg->stack[i];
        char label[512];
        snprintf(label, sizeof(label), "%s%s  %s:%d",
                 i == 0 ? "> " : "  ",
                 f.name.c_str(),
                 f.source_path.c_str(),
                 f.line);
        ImGui::TextUnformatted(label);
    }
    ImGui::EndChild();

    ImGui::End();
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
    dbg->current_source = nullptr;
    dbg->current_line = 0;
    dbg->scroll_to_line = false;
    dbg->inbox.clear();

    dbg->state = DapState::CONNECTING;
    dbg->status = "Connecting...";

    if (!platform_launch(dbg, dbg->cmdline)) {
        dbg->state = DapState::TERMINATED;
        return;
    }

    // Start reader thread
    dbg->reader_running.store(true);
    dbg->reader_thread = std::thread(reader_thread_func, dbg);

    // Begin DAP handshake
    dbg->state = DapState::INITIALIZING;
    dbg->status = "Initializing...";
    dap_request(dbg, "initialize", {
        {"adapterID", "dcmake"},
        {"clientID", "dcmake"},
        {"clientName", "dcmake"},
        {"linesStartAt1", true},
        {"columnsStartAt1", true},
        {"pathFormat", "path"},
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
