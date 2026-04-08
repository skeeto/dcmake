#ifndef DAP_HPP
#define DAP_HPP

#include "dcmake.hpp"

#include <nlohmann/json.hpp>

void dap_request(Debugger *dbg, const char *command,
                 nlohmann::json arguments = nlohmann::json::object());
void process_messages(Debugger *dbg);
void reader_thread_func(Debugger *dbg);
void stdout_thread_func(Debugger *dbg);

SourceFile *get_source(Debugger *dbg, const std::string &path);
void open_source(Debugger *dbg, const std::string &path);
void toggle_breakpoint(Debugger *dbg, const std::string &path, int line);
int has_breakpoint(Debugger *dbg, const std::string &path, int line);
void send_breakpoints_for_file(Debugger *dbg, const std::string &path);
void send_exception_breakpoints(Debugger *dbg);
void relocate_breakpoints(Debugger *dbg, const std::string &path);
void fetch_variables(Debugger *dbg, int64_t ref);

#endif
