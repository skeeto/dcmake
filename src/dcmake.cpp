#include "dap.hpp"
#include "highlight.hpp"
#include "icon_font.hpp"
#include "jetbrains_mono_font.hpp"
#include "roboto_font.hpp"

#include <cstdio>
#include <cstring>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#define ICON_TRIANGLE_LEFT     "\xee\xad\xaf"  // U+EB6F
#define ICON_TRIANGLE_RIGHT    "\xee\xad\xb0"  // U+EB70
#define ICON_CLOSE             "\xee\xa9\xb6"  // U+EA76
#define ICON_DEBUG_CONTINUE    "\xee\xab\x8f"  // U+EACF
#define ICON_DEBUG_PAUSE       "\xee\xab\x91"  // U+EAD1
#define ICON_DEBUG_RESTART     "\xee\xab\x92"  // U+EAD2
#define ICON_DEBUG_START       "\xee\xab\x93"  // U+EAD3
#define ICON_DEBUG_STEP_INTO   "\xee\xab\x94"  // U+EAD4
#define ICON_DEBUG_STEP_OUT    "\xee\xab\x95"  // U+EAD5
#define ICON_DEBUG_STEP_OVER   "\xee\xab\x96"  // U+EAD6
#define ICON_DEBUG_STOP        "\xee\xab\x97"  // U+EAD7

// --- ImGui UI ---

static void push_mono_font(Debugger *dbg)
{
    ImGui::PushFont(dbg->mono_font);
    float s = dbg->dpi_scale;
    ImVec2 sp = ImGui::GetStyle().ItemSpacing;
    ImVec2 fp = ImGui::GetStyle().FramePadding;
    ImVec2 cp = ImGui::GetStyle().CellPadding;
    sp.y -= 2.0f * s;  if (sp.y < 0.0f) sp.y = 0.0f;
    fp.y -= 2.0f * s;  if (fp.y < 0.0f) fp.y = 0.0f;
    cp.y -= 1.0f * s;  if (cp.y < 0.0f) cp.y = 0.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, sp);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, fp);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cp);
}

static void pop_mono_font()
{
    ImGui::PopStyleVar(3);
    ImGui::PopFont();
}

static void render_toolbar(Debugger *dbg)
{
    bool idle = dbg->state == DapState::IDLE;
    bool terminated = dbg->state == DapState::TERMINATED;
    bool stopped = dbg->state == DapState::STOPPED;
    bool editable = idle || terminated;

    // Global keyboard shortcuts (F-keys work regardless of focus, like VS)
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        if (io.KeyCtrl && io.KeyShift) {
            if (!idle) {
                dbg->pause_at_entry = false;
                dcmake_stop(dbg);
                dcmake_start(dbg);
            }
        } else if (io.KeyShift) {
            if (!idle) dcmake_stop(dbg);
        } else if (editable) {
            dbg->pause_at_entry = false;
            dcmake_start(dbg);
        } else if (stopped) {
            dap_request(dbg, "continue", {{"threadId", dbg->thread_id}});
            dbg->state = DapState::RUNNING;
            dbg->status = "Running";
        } else if (dbg->state == DapState::RUNNING) {
            dap_request(dbg, "pause", {{"threadId", dbg->thread_id}});
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

    // Command line text box — calculate button group width so
    // the input takes whatever remains and buttons stay visible.
    ImVec2 default_padding = ImGui::GetStyle().FramePadding;
    ImVec2 toolbar_padding(default_padding.x + 2, default_padding.y + 4);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, toolbar_padding);
    float btn_w = ImGui::CalcTextSize(ICON_DEBUG_START).x +
                  toolbar_padding.x * 2;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float sep_w = spacing * 2 + 1;  // SameLine + separator + SameLine
    float buttons_w = sep_w + btn_w * 6 + spacing * 5;
    float avail_w = ImGui::GetContentRegionAvail().x;
    float input_w = avail_w - buttons_w - spacing;
    if (input_w < 100) input_w = 100;

    // Buttons anchored to the right edge
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + input_w + spacing);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    ImGui::BeginDisabled(idle);
    if (editable) {
        if (ImGui::Button(ICON_DEBUG_START)) {
            dbg->pause_at_entry = false;
            dcmake_start(dbg);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                                 ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Start (F5)");
    } else if (dbg->state == DapState::RUNNING ||
               dbg->state == DapState::CONNECTING ||
               dbg->state == DapState::INITIALIZING) {
        if (ImGui::Button(ICON_DEBUG_PAUSE)) {
            dap_request(dbg, "pause", {{"threadId", dbg->thread_id}});
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                                 ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Pause (F5)");
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

    // Restart button (Ctrl+Shift+F5)
    ImGui::SameLine();
    ImGui::BeginDisabled(idle);
    if (ImGui::Button(ICON_DEBUG_RESTART)) {
        dbg->pause_at_entry = false;
        dcmake_stop(dbg);
        dcmake_start(dbg);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                             ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Restart (Ctrl+Shift+F5)");
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

    // Command line input fills the left side
    ImGui::SameLine(0, 0);
    ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x);
    ImGui::SetNextItemWidth(input_w);
    if (!editable) ImGui::BeginDisabled();
    ImGui::InputTextWithHint("##cmdline", "(CMake arguments)", dbg->cmdline,
                              sizeof(dbg->cmdline));
    if (!editable) ImGui::EndDisabled();
    ImGui::PopStyleVar();
}

static DapVariable *find_variable_by_name(Debugger *dbg, std::string_view name);
static std::string expand_watch(Debugger *dbg, const char *expr);

static std::vector<size_t> ifind_all(const std::string &haystack,
                                     const char *needle)
{
    std::vector<size_t> results;
    size_t nlen = strlen(needle);
    if (nlen == 0 || haystack.size() < nlen) return results;
    for (size_t i = 0; i <= haystack.size() - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) results.push_back(i);
    }
    return results;
}

static void render_source_content(Debugger *dbg, SourceFile *sf,
                                   int highlight_line, bool scroll_to,
                                   OpenSource *os)
{
    int line_count = (int)sf->lines.size();
    float line_height = ImGui::GetTextLineHeightWithSpacing();

    int gutter_digits = 1;
    for (int n = line_count; n >= 10; n /= 10) gutter_digits++;

    char gutter_buf[16];
    snprintf(gutter_buf, sizeof(gutter_buf), "%*d", gutter_digits, line_count);
    float gutter_width = ImGui::CalcTextSize(gutter_buf).x;
    float arrow_width = ImGui::CalcTextSize(ICON_TRIANGLE_RIGHT).x;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float text_x_off = gutter_width + spacing + arrow_width + spacing;

    static int context_menu_line = 0;

    // Find: count matches and locate the current one
    struct FindMatch { int line; size_t col; };
    FindMatch current_match = {-1, 0};
    size_t needle_len = 0;
    if (os->find_open && os->find_buf[0]) {
        needle_len = strlen(os->find_buf);
        int idx = 0;
        for (int li = 0; li < line_count; li++) {
            auto positions = ifind_all(sf->lines[(size_t)li], os->find_buf);
            for (size_t col : positions) {
                if (idx == os->find_match_idx)
                    current_match = {li + 1, col};
                idx++;
            }
        }
        os->find_match_count = idx;
        if (os->find_match_idx >= idx)
            os->find_match_idx = idx > 0 ? 0 : -1;
    } else {
        os->find_match_count = 0;
    }

    // Click detection: compute line from mouse position directly.
    // Pass explicit line_height to the clipper so its layout matches.
    ImVec2 content_origin = ImGui::GetCursorScreenPos();
    ImVec2 mouse = ImGui::GetMousePos();
    int mouse_line = (int)floorf((mouse.y - content_origin.y) / line_height) + 1;
    bool mouse_in_gutter = mouse.x >= content_origin.x &&
                           mouse.x < content_origin.x + gutter_width;
    if (mouse_line >= 1 && mouse_line <= line_count &&
        ImGui::IsWindowHovered()) {
        if (ImGui::IsMouseClicked(0) && mouse_in_gutter)
            toggle_breakpoint(dbg, sf->path, mouse_line);
        if (ImGui::IsMouseClicked(1)) {
            context_menu_line = mouse_line;
            ImGui::OpenPopup("source_context");
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin(line_count, line_height);
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

            if (line_num == os->flash_line && os->flash_time > 0) {
                float alpha = os->flash_time / 0.5f;  // 1.0 -> 0.0
                float width = ImGui::GetContentRegionAvail().x +
                              ImGui::GetScrollX();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    line_pos,
                    ImVec2(line_pos.x + width, line_pos.y + line_height),
                    IM_COL32(220, 180, 50, (int)(alpha * 120)));
            }

            // Find match highlights
            if (needle_len > 0) {
                auto matches = ifind_all(sf->lines[(size_t)i], os->find_buf);
                for (size_t col : matches) {
                    const char *s = sf->lines[(size_t)i].c_str();
                    float x0 = line_pos.x + text_x_off +
                               ImGui::CalcTextSize(s, s + col).x;
                    float x1 = line_pos.x + text_x_off +
                               ImGui::CalcTextSize(s, s + col + needle_len).x;
                    bool is_cur = (current_match.line == line_num &&
                                   current_match.col == col);
                    ImU32 color = is_cur ? IM_COL32(220, 180, 50, 180)
                                        : IM_COL32(180, 140, 30, 80);
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImVec2(x0, line_pos.y),
                        ImVec2(x1, line_pos.y + line_height), color);
                }
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

            ImGui::SameLine();

            if (is_current) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f),
                                   ICON_TRIANGLE_RIGHT);
            } else {
                ImGui::Dummy(ImVec2(arrow_width, 0));
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
                    if (stopped &&
                        ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                        std::string_view word = tokens[t].text;
                        if (tokens[t].type == TokenType::VARIABLE &&
                            !word.starts_with("$ENV{")) {
                            // Strip outer ${} or $CACHE{} to get inner
                            // content, expand that to the resolved name,
                            // then look up its value.
                            std::string expr(word);
                            std::string inner;
                            if (expr.starts_with("$CACHE{"))
                                inner = expr.substr(7, expr.size() - 8);
                            else if (expr.starts_with("${"))
                                inner = expr.substr(2, expr.size() - 3);
                            else
                                inner = expr;
                            std::string name = expand_watch(dbg, inner.c_str());
                            std::string val = expand_watch(dbg, expr.c_str());
                            if (!val.empty()) {
                                ImGui::BeginTooltip();
                                ImGui::Text("%s = %s", name.c_str(),
                                            val.c_str());
                                ImGui::EndTooltip();
                            }
                        } else if (tokens[t].type == TokenType::DEFAULT) {
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
        ImGui::Separator();
        int bp_state = has_breakpoint(dbg, sf->path, context_menu_line);
        if (bp_state == 0) {
            if (ImGui::MenuItem("Add Breakpoint"))
                toggle_breakpoint(dbg, sf->path, context_menu_line);
            ImGui::BeginDisabled();
            ImGui::MenuItem("Disable Breakpoint");
            ImGui::MenuItem("Remove Breakpoint");
            ImGui::EndDisabled();
        } else {
            ImGui::MenuItem("Add Breakpoint", nullptr, false, false);
            if (bp_state == 1) {
                if (ImGui::MenuItem("Disable Breakpoint")) {
                    for (auto &bp : dbg->breakpoints) {
                        if (bp.path == sf->path && bp.line == context_menu_line) {
                            bp.enabled = false;
                            if (dbg->state != DapState::IDLE &&
                                dbg->state != DapState::TERMINATED)
                                send_breakpoints_for_file(dbg, sf->path);
                            break;
                        }
                    }
                }
            } else {
                if (ImGui::MenuItem("Enable Breakpoint")) {
                    for (auto &bp : dbg->breakpoints) {
                        if (bp.path == sf->path && bp.line == context_menu_line) {
                            bp.enabled = true;
                            if (dbg->state != DapState::IDLE &&
                                dbg->state != DapState::TERMINATED)
                                send_breakpoints_for_file(dbg, sf->path);
                            break;
                        }
                    }
                }
            }
            if (ImGui::MenuItem("Remove Breakpoint"))
                toggle_breakpoint(dbg, sf->path, context_menu_line);
        }
        ImGui::EndPopup();
    }

    // Scroll handling
    if (scroll_to && highlight_line > 0) {
        float target_y = (float)(highlight_line - 1) * line_height;
        float window_h = ImGui::GetWindowHeight();
        ImGui::SetScrollY(target_y - window_h / 2.0f);
    }
    if (os->find_scroll && current_match.line > 0) {
        float target_y = (float)(current_match.line - 1) * line_height;
        float window_h = ImGui::GetWindowHeight();
        ImGui::SetScrollY(target_y - window_h / 2.0f);
        os->find_scroll = false;
    }
    if (os->goto_line > 0) {
        float target_y = (float)(os->goto_line - 1) * line_height;
        float window_h = ImGui::GetWindowHeight();
        ImGui::SetScrollY(target_y - window_h / 2.0f);
        os->flash_line = os->goto_line;
        os->flash_time = 0.5f;
        os->goto_line = 0;
    }

    // Decay flash
    if (os->flash_time > 0)
        os->flash_time -= ImGui::GetIO().DeltaTime;
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

        // Track last focused source for Edit menu
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            dbg->focused_source = os.path;

        // Keyboard shortcuts
        ImGuiIO &io = ImGui::GetIO();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
                os.find_open = true;
                os.find_focus = true;
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G)) {
                os.goto_open = true;
                os.goto_focus = true;
            }
        }

        // Find bar (pinned to top of window)
        if (os.find_open) {
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
            ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                        ImGuiInputTextFlags_AutoSelectAll;
            if (os.find_focus) {
                ImGui::SetKeyboardFocusHere();
                os.find_focus = false;
            }
            char prev_buf[256];
            memcpy(prev_buf, os.find_buf, sizeof(prev_buf));
            bool enter = ImGui::InputTextWithHint("##find", "Find...",
                os.find_buf, sizeof(os.find_buf), flags);
            ImGui::PopItemWidth();

            // Reset match index on text change
            if (strcmp(prev_buf, os.find_buf) != 0) {
                os.find_match_idx = 0;
                os.find_scroll = true;
            }

            ImGui::SameLine();
            if (os.find_match_count > 0)
                ImGui::Text("%d of %d",
                    os.find_match_idx + 1, os.find_match_count);
            else if (os.find_buf[0])
                ImGui::TextDisabled("No results");

            bool has_matches = os.find_match_count > 0;
            ImGui::SameLine();
            ImGui::BeginDisabled(!has_matches);
            if (ImGui::SmallButton(ICON_TRIANGLE_LEFT "##prev")) {
                os.find_match_idx = (os.find_match_idx - 1 +
                    os.find_match_count) % os.find_match_count;
                os.find_scroll = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_TRIANGLE_RIGHT "##next")) {
                os.find_match_idx = (os.find_match_idx + 1) %
                    os.find_match_count;
                os.find_scroll = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_CLOSE "##find")) {
                os.find_open = false;
                os.find_buf[0] = '\0';
                os.find_match_idx = -1;
            }

            // Enter / Shift+Enter to cycle
            if (enter && has_matches) {
                if (io.KeyShift)
                    os.find_match_idx = (os.find_match_idx - 1 +
                        os.find_match_count) % os.find_match_count;
                else
                    os.find_match_idx = (os.find_match_idx + 1) %
                        os.find_match_count;
                os.find_scroll = true;
            }
            // F3 / Shift+F3
            if (ImGui::IsKeyPressed(ImGuiKey_F3) && has_matches) {
                if (io.KeyShift)
                    os.find_match_idx = (os.find_match_idx - 1 +
                        os.find_match_count) % os.find_match_count;
                else
                    os.find_match_idx = (os.find_match_idx + 1) %
                        os.find_match_count;
                os.find_scroll = true;
            }
            // Escape to close
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                os.find_open = false;
                os.find_buf[0] = '\0';
                os.find_match_idx = -1;
            }
        }

        // Go to Line bar (pinned to top of window)
        if (os.goto_open) {
            ImGui::SetNextItemWidth(100);
            ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                        ImGuiInputTextFlags_CharsDecimal |
                                        ImGuiInputTextFlags_AutoSelectAll;
            if (os.goto_focus) {
                ImGui::SetKeyboardFocusHere();
                os.goto_focus = false;
            }
            bool enter = ImGui::InputTextWithHint("##goto", "Line number",
                os.goto_buf, sizeof(os.goto_buf), flags);
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_CLOSE "##goto") ||
                ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                os.goto_open = false;
                os.goto_buf[0] = '\0';
            }
            if (enter) {
                int target = atoi(os.goto_buf);
                int lc = (int)sf->lines.size();
                if (target >= 1 && target <= lc)
                    os.goto_line = target;
                os.goto_open = false;
                os.goto_buf[0] = '\0';
            }
        }

        if (ImGui::BeginChild("##source_scroll")) {
            bool is_current_file = (dbg->current_source == sf);
            int highlight_line = is_current_file ? dbg->current_line : 0;
            bool scroll = is_current_file && dbg->scroll_to_line;

            push_mono_font(dbg);
            render_source_content(dbg, sf, highlight_line, scroll, &os);
            pop_mono_font();

            if (scroll) dbg->scroll_to_line = false;
        }
        ImGui::EndChild();

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

    push_mono_font(dbg);
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
    pop_mono_font();

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
            if (v.changed)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32(ImVec4(0.4f, 0.4f, 0.1f, 0.35f)));
            ImGui::TableNextColumn();
            bool open = ImGui::TreeNodeEx("##tree",
                ImGuiTreeNodeFlags_AllowOverlap);
            ImGui::SameLine(0, 0);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY()
                - ImGui::GetStyle().FramePadding.y);
            selectable_text("##name", v.name.c_str(), v.name.size());
            ImGui::TableNextColumn();
            const char *val = v.value.empty() ? v.type.c_str()
                                              : v.value.c_str();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY()
                - ImGui::GetStyle().FramePadding.y);
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
            if (v.changed)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32(ImVec4(0.4f, 0.4f, 0.1f, 0.35f)));
            ImGui::TableNextColumn();
            bool open = ImGui::TreeNodeEx("##tree",
                ImGuiTreeNodeFlags_AllowOverlap);
            ImGui::SameLine(0, 0);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY()
                - ImGui::GetStyle().FramePadding.y);
            selectable_text("##name", v.name.c_str(), v.name.size());
            ImGui::TableNextColumn();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY()
                - ImGui::GetStyle().FramePadding.y);
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
            if (v.changed)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32(ImVec4(0.4f, 0.4f, 0.1f, 0.35f)));
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
    push_mono_font(dbg);
    if (ImGui::BeginTable("##vars", 2,
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Value");
        render_variable_rows(dbg, vars, filter);
        ImGui::EndTable();
    }
    pop_mono_font();
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

// Cache variable names include a type suffix (e.g. "CMAKE_BUILD_TYPE:STRING").
// Match by the name prefix before the colon.
static DapVariable *find_cache_var(Debugger *dbg, std::string_view name)
{
    DapVariable *scope = find_scope_child(dbg, "CacheVariables");
    if (!scope) return nullptr;
    for (auto &v : scope->children) {
        std::string_view vn = v.name;
        size_t colon = vn.find(':');
        std::string_view prefix = (colon != std::string_view::npos)
                                  ? vn.substr(0, colon) : vn;
        if (prefix == name) return &v;
    }
    return nullptr;
}

// Look up a variable by name in locals then cache (for hover tooltips)
static DapVariable *find_variable_by_name(Debugger *dbg, std::string_view name)
{
    DapVariable *locals = find_scope_child(dbg, "Locals");
    if (locals)
        for (auto &v : locals->children)
            if (v.name == name) return &v;
    return find_cache_var(dbg, name);
}

// Expand CMake ${} and $CACHE{} references in a watch expression.
// Bare names (no $) are handled separately in render_watch.
static std::string expand_watch(Debugger *dbg, const char *expr)
{
    std::string result;
    const char *p = expr;
    while (*p) {
        // Check for $CACHE{...} or ${...}
        bool is_cache = false;
        const char *brace = nullptr;
        if (strncmp(p, "$CACHE{", 7) == 0) {
            is_cache = true;
            brace = p + 7;
        } else if (p[0] == '$' && p[1] == '{') {
            brace = p + 2;
        }

        if (brace) {
            // Find matching closing brace (accounting for nesting)
            int depth = 1;
            const char *q = brace;
            while (*q && depth > 0) {
                if (*q == '{') depth++;
                else if (*q == '}') depth--;
                if (depth > 0) q++;
            }
            if (depth == 0) {
                // Recursively expand the content between braces
                std::string inner(brace, (size_t)(q - brace));
                std::string name = expand_watch(dbg, inner.c_str());
                // Look up the variable
                if (is_cache) {
                    DapVariable *v = find_cache_var(dbg, name);
                    if (v) result += v->value;
                } else {
                    DapVariable *v = find_variable_by_name(dbg, name);
                    if (v) result += v->value;
                }
                p = q + 1;
                continue;
            }
        }

        result += *p++;
    }
    return result;
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

    if (ImGui::BeginChild("##scroll")) {
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
    }
    ImGui::EndChild();

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

    if (ImGui::BeginChild("##scroll")) {
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
    }
    ImGui::EndChild();

    ImGui::End();
}

static void render_watch(Debugger *dbg)
{
    if (!dbg->show_watch) return;
    if (!ImGui::Begin("Watch", &dbg->show_watch)) {
        ImGui::End();
        return;
    }

    bool stopped = dbg->state == DapState::STOPPED ||
                   dbg->state == DapState::RUNNING;

    // Sentinel entry for adding new watches
    static char sentinel_buf[256] = {};

    push_mono_font(dbg);
    if (ImGui::BeginTable("##watches", 3,
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed, 50);

        int remove_idx = -1;
        int n = (int)dbg->watches.size();

        // Existing watches + one sentinel row
        for (int i = 0; i <= n; i++) {
            bool is_sentinel = (i == n);
            ImGui::PushID(i);
            ImGui::TableNextRow();

            // Resolve value: bare names do a variable lookup,
            // expressions with $ are expanded.
            DapVariable *var = nullptr;
            std::string expanded;
            bool is_expr = false;
            if (!is_sentinel && stopped) {
                auto &w = dbg->watches[(size_t)i];
                is_expr = (strchr(w.buf, '$') != nullptr);
                if (is_expr)
                    expanded = expand_watch(dbg, w.buf);
                else
                    var = find_variable_by_name(dbg, w.buf);
            }

            if (var && var->changed)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    IM_COL32(80, 80, 30, 255));

            // Name column — always an editable InputText
            ImGui::TableNextColumn();
            char *buf = is_sentinel ? sentinel_buf :
                                      dbg->watches[(size_t)i].buf;
            size_t buf_size = is_sentinel ? sizeof(sentinel_buf) :
                                            sizeof(dbg->watches[0].buf);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGuiInputTextFlags fl = ImGuiInputTextFlags_EnterReturnsTrue;
            bool enter = ImGui::InputText("##name", buf, buf_size, fl);
            ImGui::PopStyleColor(2);

            if (ImGui::IsItemDeactivatedAfterEdit() || enter) {
                if (is_sentinel) {
                    if (sentinel_buf[0]) {
                        WatchEntry w;
                        snprintf(w.buf, sizeof(w.buf), "%s", sentinel_buf);
                        dbg->watches.push_back(w);
                        sentinel_buf[0] = '\0';
                        n++;
                    }
                } else if (!buf[0]) {
                    remove_idx = i;
                }
            }

            // Context menu to remove (not on sentinel)
            if (!is_sentinel) {
                if (ImGui::BeginPopupContextItem("watch_ctx")) {
                    if (ImGui::MenuItem("Remove"))
                        remove_idx = i;
                    ImGui::EndPopup();
                }
            }

            // Value column
            ImGui::TableNextColumn();
            if (is_sentinel) {
                // empty
            } else if (!stopped) {
                ImGui::TextDisabled("--");
            } else if (is_expr) {
                if (!expanded.empty())
                    selectable_text("##val", expanded.c_str(),
                                    expanded.size());
            } else if (!var) {
                ImGui::TextDisabled("<not found>");
            } else if (!var->value.empty()) {
                selectable_text("##val", var->value.c_str(),
                                var->value.size());
            }

            // Scope column
            ImGui::TableNextColumn();
            if (!is_sentinel && !is_expr) {
                if (var) {
                    DapVariable *locals = find_scope_child(dbg, "Locals");
                    bool in_locals = false;
                    if (locals)
                        for (auto &v : locals->children)
                            if (&v == var) { in_locals = true; break; }
                    ImGui::TextUnformatted(in_locals ? "Local" : "Cache");
                } else if (stopped) {
                    ImGui::TextDisabled("--");
                }
            }

            ImGui::PopID();
        }
        ImGui::EndTable();

        if (remove_idx >= 0)
            dbg->watches.erase(dbg->watches.begin() + remove_idx);
    }
    pop_mono_font();

    ImGui::End();
}

static void render_targets(Debugger *dbg)
{
    if (!dbg->show_targets) return;
    if (!ImGui::Begin("Targets", &dbg->show_targets)) {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter...",
                             dbg->filter_targets, sizeof(dbg->filter_targets));

    if (ImGui::BeginChild("##scroll")) {
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
    }
    ImGui::EndChild();

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

    if (ImGui::BeginChild("##scroll")) {
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
    }
    ImGui::EndChild();

    ImGui::End();
}

static void render_breakpoints_panel(Debugger *dbg)
{
    if (!dbg->show_breakpoints) return;
    if (!ImGui::Begin("Breakpoints", &dbg->show_breakpoints)) {
        ImGui::End();
        return;
    }

    bool has_any = !dbg->breakpoints.empty();
    bool running = dbg->state != DapState::IDLE &&
                   dbg->state != DapState::TERMINATED;
    ImGui::BeginDisabled(!has_any);
    if (ImGui::SmallButton("Enable All")) {
        for (auto &bp : dbg->breakpoints) bp.enabled = true;
        if (running)
            for (auto &bp : dbg->breakpoints)
                send_breakpoints_for_file(dbg, bp.path);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Disable All")) {
        for (auto &bp : dbg->breakpoints) bp.enabled = false;
        if (running)
            for (auto &bp : dbg->breakpoints)
                send_breakpoints_for_file(dbg, bp.path);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete All")) {
        std::vector<LineBreakpoint> old;
        old.swap(dbg->breakpoints);
        if (running)
            for (auto &bp : old)
                send_breakpoints_for_file(dbg, bp.path);
    }
    ImGui::EndDisabled();
    ImGui::Separator();

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
        snprintf(label, sizeof(label), "%s:%d", filename, bp.line);
        if (ImGui::Checkbox("##enable", &bp.enabled)) {
            if (dbg->state != DapState::IDLE &&
                dbg->state != DapState::TERMINATED) {
                send_breakpoints_for_file(dbg, bp.path);
            }
        }
        ImGui::SameLine();
        float close_w = ImGui::CalcTextSize(ICON_CLOSE).x +
                        ImGui::GetStyle().FramePadding.x * 2 +
                        ImGui::GetStyle().ItemSpacing.x;
        if (ImGui::Selectable(label, false, 0,
                ImVec2(ImGui::GetContentRegionAvail().x - close_w, 0))) {
            open_source(dbg, bp.path);
            for (auto &os : dbg->open_sources) {
                if (os.path == bp.path) {
                    os.goto_line = bp.line;
                    break;
                }
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s:%d", bp.path.c_str(), bp.line);
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_CLOSE)) {
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

static void render_json_value(const json &j)
{
    if (j.is_object()) {
        for (auto &[key, val] : j.items()) {
            if (val.is_structured()) {
                if (ImGui::TreeNode(key.c_str())) {
                    render_json_value(val);
                    ImGui::TreePop();
                }
            } else {
                ImGui::BulletText("%s: %s", key.c_str(),
                                  val.dump(-1).c_str());
            }
        }
    } else if (j.is_array()) {
        int idx = 0;
        for (auto &elem : j) {
            char label[32];
            snprintf(label, sizeof(label), "[%d]", idx++);
            if (elem.is_structured()) {
                if (ImGui::TreeNode(label)) {
                    render_json_value(elem);
                    ImGui::TreePop();
                }
            } else {
                ImGui::BulletText("%s: %s", label,
                                  elem.dump(-1).c_str());
            }
        }
    } else {
        ImGui::TextUnformatted(j.dump(-1).c_str());
    }
}

static void render_dap_log(Debugger *dbg)
{
    if (!dbg->show_dap_log) return;
    if (!ImGui::Begin("DAP Log", &dbg->show_dap_log)) {
        ImGui::End();
        return;
    }

    // Export button
    if (ImGui::Button("Export...")) {
        std::string path = platform_save_file_dialog();
        if (!path.empty()) {
            std::string out;
            for (auto &m : dbg->dap_log) {
                auto j = json::parse(m.raw, nullptr, false);
                out += j.is_discarded() ? m.raw : j.dump();
                out += '\n';
            }
            platform_write_file(path.c_str(), out.data(), out.size());
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%d messages)", (int)dbg->dap_log.size());

    push_mono_font(dbg);
    if (ImGui::BeginChild("##dap_scroll")) {
        for (size_t i = 0; i < dbg->dap_log.size(); i++) {
            auto &m = dbg->dap_log[i];
            char label[256];
            snprintf(label, sizeof(label), "%s##%zu",
                     m.summary.c_str(), i);

            if (ImGui::TreeNode(label)) {
                json parsed;
                try { parsed = json::parse(m.raw); } catch (...) {}
                if (!parsed.is_null())
                    render_json_value(parsed);
                else
                    ImGui::TextUnformatted(m.raw.c_str());
                ImGui::TreePop();
            }
        }
    }
    ImGui::EndChild();
    pop_mono_font();

    ImGui::End();
}

static void render_output(Debugger *dbg)
{
    if (!dbg->show_output) return;
    if (!ImGui::Begin("Output", &dbg->show_output)) {
        ImGui::End();
        return;
    }

    push_mono_font(dbg);
    if (ImGui::BeginChild("##output_scroll")) {
        bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
        ImGui::TextUnformatted(dbg->output.c_str());
        if (at_bottom) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    pop_mono_font();

    ImGui::End();
}

static void render_ui(Debugger *dbg)
{
    static bool show_about = false;
    static bool reset_pending = false;

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
            if (ImGui::MenuItem("Open File...",
                                menu_io.ConfigMacOSXBehaviors ? "Cmd+O"
                                                              : "Ctrl+O")) {
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
            if (ImGui::MenuItem("Exit",
                                menu_io.ConfigMacOSXBehaviors ? "Cmd+Q"
                                                              : "Alt+F4"))
                dbg->want_quit = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            OpenSource *fs = nullptr;
            for (auto &s : dbg->open_sources)
                if (s.path == dbg->focused_source) { fs = &s; break; }
            if (ImGui::MenuItem("Find",
                                menu_io.ConfigMacOSXBehaviors ? "Cmd+F"
                                                              : "Ctrl+F",
                                false, fs != nullptr)) {
                fs->find_open = true;
                fs->find_focus = true;
            }
            if (ImGui::MenuItem("Go to Line",
                                menu_io.ConfigMacOSXBehaviors ? "Cmd+G"
                                                              : "Ctrl+G",
                                false, fs != nullptr)) {
                fs->goto_open = true;
                fs->goto_focus = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Call Stack", nullptr, &dbg->show_stack);
            ImGui::MenuItem("Locals", nullptr, &dbg->show_locals);
            ImGui::MenuItem("Cache Variables", nullptr, &dbg->show_cache);
            ImGui::MenuItem("Watch", nullptr, &dbg->show_watch);
            ImGui::MenuItem("Targets", nullptr, &dbg->show_targets);
            ImGui::MenuItem("Tests", nullptr, &dbg->show_tests);
            ImGui::MenuItem("Breakpoints", nullptr, &dbg->show_breakpoints);
            ImGui::MenuItem("Exception Filters", nullptr, &dbg->show_filters);
            ImGui::MenuItem("Output", nullptr, &dbg->show_output);
            ImGui::MenuItem("DAP Log", nullptr, &dbg->show_dap_log);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) {
                reset_pending = true;
                dbg->show_stack = true;
                dbg->show_locals = true;
                dbg->show_cache = true;
                dbg->show_watch = true;
                dbg->show_targets = true;
                dbg->show_tests = true;
                dbg->show_breakpoints = true;
                dbg->show_filters = true;
                dbg->show_output = true;
                dbg->show_dap_log = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About"))
                show_about = true;
            ImGui::EndMenu();
        }
        // Right-aligned status text
        float status_w = ImGui::CalcTextSize(dbg->status.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - status_w -
                             ImGui::GetStyle().ItemSpacing.x);
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
            "Dear ImGui -- Copyright (C) 2014-2026 Omar Cornut (MIT)");
#ifdef DCMAKE_GLFW
        ImGui::BulletText(
            "GLFW -- Copyright (C) 2002-2026 various (zlib/libpng)");
#endif
        ImGui::BulletText(
            "nlohmann/json -- Copyright (C) 2013-2026 various (MIT)");
        ImGui::BulletText(
            "Codicons -- Copyright (C) 2019-2026 Microsoft Corporation (MIT)");
        ImGui::BulletText(
            "Roboto -- Copyright (C) 2011 Google (Apache 2.0)");
        ImGui::BulletText(
            "JetBrains Mono -- Copyright (C) 2020 JetBrains (OFL 1.1)");
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

    // Reset layout: defer rebuild by one frame so all re-shown
    // windows are submitted to ImGui before DockBuilder places them.
    if (reset_pending) {
        reset_pending = false;
        dbg->reset_layout = true;
    } else if (dbg->reset_layout ||
               (dbg->first_layout && !ImGui::DockBuilderGetNode(dockspace_id))) {
        dbg->first_layout = false;
        dbg->reset_layout = false;
        ImGui::DockBuilderRemoveNode(dockspace_id);
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
        ImGui::DockBuilderDockWindow("DAP Log", left_bottom);
        ImGui::DockBuilderDockWindow("Locals", right_top);
        ImGui::DockBuilderDockWindow("Cache Variables", right_top);
        ImGui::DockBuilderDockWindow("Watch", right_top);
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
    render_watch(dbg);
    render_targets(dbg);
    render_tests(dbg);
    render_breakpoints_panel(dbg);
    render_stack(dbg);
    render_filters_panel(dbg);
    render_output(dbg);
    render_dap_log(dbg);
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
    out += "show_watch=";       out += dbg->show_watch ? '1' : '0';       out += '\n';
    out += "show_dap_log=";    out += dbg->show_dap_log ? '1' : '0';    out += '\n';
    out += "win_x=";            out += std::to_string(dbg->win_x);        out += '\n';
    out += "win_y=";            out += std::to_string(dbg->win_y);        out += '\n';
    out += "win_w=";            out += std::to_string(dbg->win_w);        out += '\n';
    out += "win_h=";            out += std::to_string(dbg->win_h);        out += '\n';
    out += "win_maximized=";    out += dbg->win_maximized ? '1' : '0';    out += '\n';

    for (auto &bp : dbg->breakpoints) {
        out += "bp\t";
        out += bp.path;      out += '\t';
        out += std::to_string(bp.line); out += '\t';
        out += bp.enabled ? '1' : '0';  out += '\t';
        out += bp.line_text; out += '\n';
    }

    for (auto &w : dbg->watches) {
        if (!w.buf[0]) continue;
        out += "watch=";
        out += w.buf;
        out += '\n';
    }

    platform_write_file(config_path(dbg).c_str(), out.data(), out.size());
}

void dcmake_load_config(Debugger *dbg)
{
    // Explicit defaults (Debugger dbg={} may zero-init instead of using DMIs)
    dbg->win_x = -1;
    dbg->win_y = -1;
    dbg->win_w = 1280;
    dbg->win_h = 720;
    dbg->win_maximized = false;

    std::string content = platform_read_file(config_path(dbg).c_str());
    if (content.empty()) return;

    size_t pos = 0;
    while (pos < content.size()) {
        size_t nl = content.find('\n', pos);
        size_t end = (nl == std::string::npos) ? content.size() : nl;
        if (end > pos && content[end - 1] == '\r') end--;
        std::string line = content.substr(pos, end - pos);
        pos = (nl == std::string::npos) ? content.size() : nl + 1;

        if (line.starts_with("watch=")) {
            const char *expr = line.c_str() + 6;
            if (expr[0]) {
                WatchEntry w;
                snprintf(w.buf, sizeof(w.buf), "%s", expr);
                dbg->watches.push_back(w);
            }
            continue;
        }

        if (line.starts_with("bp\t")) {
            size_t p1 = 3;
            size_t p2 = line.find('\t', p1);
            if (p2 == std::string::npos) continue;
            size_t p3 = line.find('\t', p2 + 1);
            if (p3 == std::string::npos) continue;
            size_t p4 = line.find('\t', p3 + 1);
            if (p4 == std::string::npos) continue;

            LineBreakpoint bp;
            bp.path = platform_realpath(line.substr(p1, p2 - p1));
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
        else if (key == "show_watch") dbg->show_watch = val;
        else if (key == "show_dap_log") dbg->show_dap_log = val;
        else if (key == "win_x") dbg->win_x = std::atoi(line.c_str() + eq + 1);
        else if (key == "win_y") dbg->win_y = std::atoi(line.c_str() + eq + 1);
        else if (key == "win_w") dbg->win_w = std::atoi(line.c_str() + eq + 1);
        else if (key == "win_h") dbg->win_h = std::atoi(line.c_str() + eq + 1);
        else if (key == "win_maximized") dbg->win_maximized = val;
    }
}

// --- Lifecycle ---

void dcmake_init(Debugger *dbg)
{
    // Load fonts at physical pixel size for crisp high-DPI rendering
    ImGuiIO &io = ImGui::GetIO();
    float s = dbg->dpi_scale;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        roboto_font_compressed_data, roboto_font_compressed_size, 15.0f * s);

    // Merge codicon icons into the UI font
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.GlyphOffset = ImVec2(0, 2 * s);
    static const ImWchar icon_ranges[] = {
        0xEA60, 0xF102, 0
    };
    io.Fonts->AddFontFromMemoryCompressedTTF(
        icon_compressed_data, sizeof(icon_compressed_data),
        14.0f * s, &cfg, icon_ranges);

    // Monospace font for source code, variables, and output
    dbg->mono_font = io.Fonts->AddFontFromMemoryCompressedTTF(
        jetbrains_mono_font_compressed_data,
        jetbrains_mono_font_compressed_size, 15.0f * s);

    // Merge codicon icons into the mono font too (for source gutter)
    ImFontConfig mono_icon_cfg;
    mono_icon_cfg.MergeMode = true;
    mono_icon_cfg.GlyphOffset = ImVec2(0, 3 * s);
    mono_icon_cfg.GlyphMaxAdvanceX = 1.0f * s;
    static const ImWchar mono_icon_ranges[] = {
        0xEB6F, 0xEB70, 0
    };
    io.Fonts->AddFontFromMemoryCompressedTTF(
        icon_compressed_data, sizeof(icon_compressed_data),
        16.0f * s, &mono_icon_cfg, mono_icon_ranges);

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
    dbg->dap_log.clear();
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
    dbg->stdout_running.store(false);

    // Shut down the socket first — this unblocks the reader thread.
    if (dbg->pipe_shutdown) {
        dbg->pipe_shutdown(dbg->platform);
    }
    if (dbg->reader_thread.joinable()) {
        dbg->reader_thread.join();
    }

    // Kill cmake before joining the stdout thread.  On Linux, close()
    // does not unblock a read() in another thread, so the stdout thread
    // will hang until the write end of the pipe closes (cmake exits).
    platform_cleanup(dbg);

    if (dbg->stdout_thread.joinable()) {
        dbg->stdout_thread.join();
    }

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
    dbg->cmake_paths.clear();
    dbg->status = "Stopped";
}

void dcmake_frame(Debugger *dbg)
{
    for (auto &path : dbg->dropped_files)
        open_source(dbg, path);
    dbg->dropped_files.clear();

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
