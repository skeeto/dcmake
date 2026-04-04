# dcmake

Specialized GUI front end for `cmake --debugger` using the Debug Adapter
Protocol (DAP) over a pipe. C++20, CMake build, Dear ImGui UI.

## Build

    cmake -B build
    cmake --build build

Test against any CMake project:

    ./build/dcmake -S /path/to/source -B /path/to/build

All arguments after `dcmake` are forwarded to cmake as-is.

## Architecture

All source files live under `src/`:

- `src/dcmake.h` -- Debugger state struct, DapState enum, lifecycle prototypes
- `src/dcmake.cpp` -- DAP protocol, state machine, ImGui UI (all shared logic)
- `src/platform_glfw.cpp` -- macOS/Linux: main(), GLFW+OpenGL3 render loop
- `src/platform_win32.cpp` -- Windows: WinMain(), Win32+DX11, named pipes + CreateProcess
- `CMakeLists.txt` -- FetchContent for nlohmann/json, Dear ImGui, GLFW

Platform files own the entry point, render loop, pipe creation, and subprocess
management. They implement `platform_launch` and `platform_cleanup`, and fill
in function pointers (`pipe_read`, `pipe_write`, `pipe_shutdown`) plus a
`void *platform` context on the Debugger struct. The shared code in
`src/dcmake.cpp` uses these for all I/O — it has no platform-specific includes.

## Dependencies

All via FetchContent with SHA256 hashes. Dear ImGui has no CMakeLists.txt so
we build it as a static library ourselves and conditionally add backend
sources (GLFW+OpenGL3 on POSIX, Win32+DX11 on Windows).

## DAP pipe mechanism

CMake is the pipe server. We are the client. Each platform implements this in
`platform_launch`:

**POSIX** (`platform_glfw.cpp`): Unix domain socket at `/tmp/dcmake-<pid>.sock`.
CMake binds/listens/accepts. We `fork`/`exec` cmake, then retry `connect()`
in a loop (10ms intervals, up to ~5s). I/O via `read()`/`write()` on the fd.

**Windows** (`platform_win32.cpp`): Named pipe at `\\.\pipe\dcmake-<pid>`.
CMake calls `CreateNamedPipeA` + `ConnectNamedPipe`. We `CreateProcessA` cmake,
then retry `CreateFileA` to open the pipe. I/O via synchronous `ReadFile`/`WriteFile`.

## CMake debugger gotchas

**CMake does NOT auto-stop on entry.** It only pauses when a breakpoint is
hit, a step request condition is active, or PauseRequest is set. To get an
initial stop, we send a `pause` request *before* `configurationDone`. CMake's
session thread processes requests in order: pause sets the atomic flag, then
configurationDone unblocks cmake execution, and cmake immediately hits the
pause flag on the very first function call. See `src/dcmake.cpp` handle_event
for "initialized" and `cmake-4.3.1/Source/cmDebuggerAdapter.cxx:386-421`.

**CMake has a single thread** called "CMake script". The thread ID comes from
the first `stopped` event's `body.threadId`. All step/continue requests
require this threadId.

**Stack frames** are returned newest-first (index 0 = top of stack). Each
frame has `source.path` and `line` for the current location.

## I/O model

A dedicated reader thread reads from the socket and parses DAP Content-Length
framing. Complete JSON message strings are pushed into `Debugger::inbox`
(mutex-protected). The main thread drains the queue each frame via
`process_messages()`. Only the main thread writes to the socket.

## DAP handshake sequence

1. Client sends `initialize` (adapterID: "dcmake")
2. Server responds with capabilities (CMake-specific: includes cmakeVersion)
3. Server sends `initialized` event
4. Client sends `pause` (sets PauseRequest flag)
5. Client sends `configurationDone` (unblocks cmake)
6. Server sends `stopped` event (reason: "pause")
7. Client sends `stackTrace` to learn current file + line

## Not yet implemented

- Linux platform (should share platform_glfw.cpp as-is)
- Variable inspection (scopes, variables requests)
- Breakpoints (setBreakpoints request)
- Step out
- Output/console panel for cmake's stdout/stderr
- Exception breakpoints (cmake message types: FATAL_ERROR, WARNING, etc.)

## Reference material

- `debugAdapterProtocol.json` -- full DAP JSON schema (checked in)
- `cmake-4.3.1/` -- CMake source for reference (gitignored), key files:
  - `Source/cmDebuggerAdapter.cxx` -- adapter implementation, request handlers
  - `Source/cmDebuggerPosixPipeConnection.cxx` -- Unix domain socket server
  - `Source/cmDebuggerProtocol.h` -- CMake-specific DAP extensions
