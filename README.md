# dcmake: CMake debugger front-end

An ImGui front end for [`cmake --debugger`][doc] communicating with
[DAP][]. It allows stepping through and inspecting a CMake build. Supports
at least Windows, macOS, and Linux.

![](docs/dcmake.png)

## Build

On any platform:

    $ cmake -B build
    $ cmake --build build

Linux distributions may need `xorg-dev` or `libwayland-dev`. File
dialogs (Ctrl+O, Set Working Directory) use `kdialog` or `zenity`,
tried in that order. By default dependencies are downloaded
automatically, currently:

* [Dear ImGui][] ([docking branch][])
* [nlohmann/json][]
* [GLFW][] (except on Windows)

Source releases bundle dependencies in `deps/` for offline builds.
Distribution packagers can use system libraries instead:

    $ cmake -B build -DDEPS=LOCAL

The `DEPS` variable controls how nlohmann/json and GLFW are resolved:

| Value | Behavior |
|-------|----------|
| `AUTO` (default) | bundled → downloaded → local |
| `DOWNLOAD` | bundled → downloaded |
| `LOCAL` | system `find_package` only |

Dear ImGui is always bundled or downloaded because no distributions
package the docking branch. To produce a source release tarball with
bundled dependencies:

    $ cmake -P cmake/SourceRelease.cmake

Which runs `git archive`, downloads dependencies into the tree, and
produces a self-contained `dcmake-VERSION.tar.gz`.

## Usage

    $ dcmake [cmake args..]

Where all arguments become CMake arguments in the debugger. The command
line is editable within UI and supports shell variable expansion (`$VAR`
on Unix, `%VAR%` on Windows). The debugger can handle configuration (`-B`,
`-S`) and scripts (`-P`). For example, to step through building a source
release:

    $ dcmake -P cmake/SourceRelease.cmake

Then press F10 to begin stepping from the first line of the script.

## Shortcuts

| Key | Action |
|-----|--------|
| F5 | Start (free-running) / Continue |
| Shift+F5 | Stop |
| Ctrl+Shift+F5 | Restart |
| F10 | Start (break at first line) / Step Over |
| F11 | Start (break at first line) / Step In |
| Shift+F11 | Step Out |
| F9 | Toggle breakpoint on current line |
| Ctrl+F | Find in source |
| F3 / Shift+F3 | Next / previous match |
| Enter / Shift+Enter | Next / previous match (while in find bar) |
| Escape | Close find or go-to-line bar |
| Ctrl+G | Go to line |
| Ctrl+O | Open file |

## Features

* Run to line: right-click a source line and select "Run to line" to
  continue execution up to that line

* Variable hover tooltips: hover over a variable name in the source view
  to see its current value

* Search filters: on variable panels (Locals, Cache Variables, Targets,
  Tests); case-insensitive substring match on name or value

* Breakpoint drift mitigation: breakpoints track their line content so
  they survive file edits between runs

* Watch window: pin variables for monitoring across steps; bare names
  search Locals then Cache, use `$CACHE{VAR}` to target cache explicitly

* Persistent layout and state: window positions, panel visibility,
  breakpoints, and watch expressions are saved across sessions

## Configuration

Layout and window state are saved to `imgui.ini` in a platform config
directory:

| Platform | Path |
|----------|------|
| Linux/macOS | `$XDG_CONFIG_HOME/dcmake/` or `~/.config/dcmake/` |
| Windows | `%XDG_CONFIG_HOME%/dcmake/`, `%HOME%/.config/dcmake/`, or `%AppData%\dcmake\` |

## Known CMake bugs

CMake's DAP debugger inconsistently normalizes paths on case-insensitive
filesystems (macOS HFS+, Windows NTFS). When a path differs between the
DAP client and the command line — e.g. a breakpoint set on `Test.cmake`
while CMake was invoked with `cmake -P test.cmake` — the breakpoint may
not trip. On macOS, dcmake mitigates this by re-sending breakpoints using
CMake's version of the path, but this only activates if CMake stops for
some other reason, e.g. pause on entry. On Windows, CMake's
`GetActualCaseForPath` normalizes the `setBreakpoints` path but not its
internal execution paths, so no client-side workaround is possible.
Therefore, **use the canonical path case in command line arguments and
includes**.

If the debuggee is older than CMake 4.4.0, and dcmake is abruptly stopped
on a non-Windows host, the CMake debuggee may [get stuck in an infinite
loop][loop], requiring manual cleanup. This was fixed upstream. On Windows
dcmake uses Job Objects, circumventing the bug. Other supported platforms
do not have the equivalent functionality.


[DAP]: https://microsoft.github.io/debug-adapter-protocol/
[Dear ImGui]: https://github.com/ocornut/imgui
[GLFW]: https://www.glfw.org/
[doc]: https://cmake.org/cmake/help/latest/manual/cmake.1.html#cmdoption-cmake-debugger
[docking branch]: https://github.com/ocornut/imgui/wiki/Docking
[loop]: https://gitlab.kitware.com/cmake/cmake/-/work_items/27743
[nlohmann/json]: https://json.nlohmann.me/
