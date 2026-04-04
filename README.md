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

## Usage

    $ dcmake [cmake args..]

Where any arguments become CMake arguments in the debugger.


[DAP]: https://microsoft.github.io/debug-adapter-protocol/
[doc]: https://cmake.org/cmake/help/latest/manual/cmake.1.html#cmdoption-cmake-debugger
