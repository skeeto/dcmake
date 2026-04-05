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

To bundle dependencies for a source release:

    $ cmake -P cmake/BundleDeps.cmake

## Usage

    $ dcmake [cmake args..]

Where any arguments become CMake arguments in the debugger. The
command line is editable from the UI and supports shell variable
expansion (`$VAR` on Unix, `%VAR%` on Windows).

## Shortcuts

| Key | Action |
|-----|--------|
| F5 | Start (free-running) / Continue |
| Shift+F5 | Stop |
| F10 | Start (break at first line) / Step Over |
| F11 | Start (break at first line) / Step In |
| Shift+F11 | Step Out |

## Features

* **CMake syntax highlighting** in source tabs with keywords, strings,
  variables, generator expressions, and comments color-coded
* **Variable hover tooltips** — hover over a variable name in the source
  view to see its current value
* **Search filters** on variable panels (Locals, Cache Variables, Targets,
  Tests) — case-insensitive substring match on name or value
* **Semicolon-separated list unfolding** — CMake list values expand into
  indexed sub-items in variable panels
* **Breakpoint drift mitigation** — breakpoints track their line content
  so they survive file edits between sessions
* **Breakpoint enable/disable** — toggle breakpoints without removing them
* **Output window** — captures cmake's stdout/stderr (build messages,
  warnings, errors) with selectable text
* **Stop reason in status bar** — shows why execution paused (step,
  breakpoint, entry, etc.)
* **Exception filter controls** — enable/disable cmake exception breakpoint
  categories independently
* **Persistent layout and state** — window positions, panel visibility,
  and breakpoints are saved across sessions
* **Set Working Directory** from the File menu with native directory picker;
  current directory shown in the title bar
* **Global keyboard shortcuts** work regardless of which widget has focus

## Configuration

Layout and window state are saved to `imgui.ini` in a platform config
directory:

| Platform | Path |
|----------|------|
| Linux/macOS | `$XDG_CONFIG_HOME/dcmake/` or `~/.config/dcmake/` |
| Windows | `%AppData%\dcmake\` |


[DAP]: https://microsoft.github.io/debug-adapter-protocol/
[doc]: https://cmake.org/cmake/help/latest/manual/cmake.1.html#cmdoption-cmake-debugger
