# dcmake: CMake debugger front-end

An ImGui front end for [`cmake --debugger`][doc] communicating with
[DAP][]. It allows stepping through and inspecting a CMake build. Supports
at least Windows, macOS, and Linux.

![](docs/dcmake.png)

## Build

On any platform:

    $ cmake -B build
    $ cmake --build build

On Linux you may need to install `xorg-dev` or similar first.

By default dependencies are downloaded automatically. Source releases
bundle dependencies in `deps/` for offline builds. Distribution
packagers can use system libraries instead:

    $ cmake -B build -DDEPS=LOCAL

The `DEPS` variable controls how nlohmann/json and GLFW are resolved:

| Value | Behavior |
|-------|----------|
| `AUTO` (default) | bundled → downloaded → local |
| `DOWNLOAD` | bundled → downloaded |
| `LOCAL` | system `find_package` only |

Dear ImGui (docking branch) is always bundled or downloaded since no
distributions package it.

To produce a source release tarball with bundled dependencies:

    $ cmake -P cmake/SourceRelease.cmake

This runs `git archive`, downloads dependencies into the tree, and
produces a self-contained `dcmake-VERSION.tar.gz`. To bundle
dependencies into `deps/` in the working tree instead:

    $ cmake -P cmake/BundleDeps.cmake

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
| F10 | Start (break at first line) / Step Over |
| F11 | Start (break at first line) / Step In |
| Shift+F11 | Step Out |
| F9 | Toggle breakpoint on current line |

## Features

* Run to line: right-click a source line and select "Run to line" to
  continue execution up to that line

* Variable hover tooltips: hover over a variable name in the source view
  to see its current value

* Search filters: on variable panels (Locals, Cache Variables, Targets,
  Tests); case-insensitive substring match on name or value

* Breakpoint drift mitigation: breakpoints track their line content so
  they survive file edits between runs

* Persistent layout and state: window positions, panel visibility, and
  breakpoints are saved across sessions

## Configuration

Layout and window state are saved to `imgui.ini` in a platform config
directory:

| Platform | Path |
|----------|------|
| Linux/macOS | `$XDG_CONFIG_HOME/dcmake/` or `~/.config/dcmake/` |
| Windows | `%AppData%\dcmake\` |


[DAP]: https://microsoft.github.io/debug-adapter-protocol/
[doc]: https://cmake.org/cmake/help/latest/manual/cmake.1.html#cmdoption-cmake-debugger
