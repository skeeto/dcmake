cmake_minimum_required(VERSION 3.25)
set(DCMAKE_VERSION 1.7.1)
option(DCMAKE_CCACHE "Use ccache if found" ON)
if(DCMAKE_CCACHE)
  find_program(CCACHE ccache)
  if(CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE}")
    set(CMAKE_OBJCXX_COMPILER_LAUNCHER "${CCACHE}")
  endif()
endif()
if(APPLE)
  project(dcmake VERSION ${DCMAKE_VERSION} LANGUAGES CXX OBJCXX)
else()
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  project(dcmake VERSION ${DCMAKE_VERSION} LANGUAGES CXX)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DEPS "FETCH" CACHE STRING
    "Dependency resolution for json and GLFW: FETCH or LOCAL")
set_property(CACHE DEPS PROPERTY STRINGS FETCH LOCAL)

# --- Dear ImGui (always bundled or downloaded) ---

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui")
  set(FETCHCONTENT_SOURCE_DIR_IMGUI "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui")
  message(STATUS "Dear ImGui: bundled")
else()
  message(STATUS "Dear ImGui: downloaded")
endif()

FetchContent_Declare(
  imgui
  URL https://github.com/ocornut/imgui/archive/f5f6ca07be7ce0ea9eed6c04d55833bac3f6b50b.tar.gz
  URL_HASH SHA256=91aed5e92c1e24aada8dfd2a6933222341ec291dc29a4add49828610d5cb6765
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

# --- nlohmann/json and GLFW ---
#
# Only macOS and Linux use the GLFW+GL3 GUI half.  Windows, Cygwin,
# and MSYS2's msys environment all use Win32+DX11 and do not need
# GLFW located or built, even though Cygwin/MSYS2 look like POSIX
# to most of the rest of CMake.  Keep this branch selector in sync
# with the four-way platform block below.
if(WIN32 OR CYGWIN OR MSYS)
  set(_need_glfw FALSE)
else()
  set(_need_glfw TRUE)
endif()

if(DEPS STREQUAL "LOCAL")
  find_package(nlohmann_json REQUIRED)
  message(STATUS "nlohmann/json: system")
  if(_need_glfw)
    find_package(glfw3 REQUIRED)
    message(STATUS "GLFW: system")
  endif()
else()
  # Use bundled sources under deps/ when present, otherwise download.
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/deps/json")
    set(FETCHCONTENT_SOURCE_DIR_JSON "${CMAKE_CURRENT_SOURCE_DIR}/deps/json")
    set(_json_origin "bundled")
  else()
    set(_json_origin "downloaded")
  endif()
  FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  )
  FetchContent_MakeAvailable(json)
  message(STATUS "nlohmann/json: ${_json_origin}")

  if(_need_glfw)
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/deps/glfw")
      set(FETCHCONTENT_SOURCE_DIR_GLFW "${CMAKE_CURRENT_SOURCE_DIR}/deps/glfw")
      set(_glfw_origin "bundled")
    else()
      set(_glfw_origin "downloaded")
    endif()
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      glfw
      URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
      URL_HASH SHA256=b5ec004b2712fd08e8861dc271428f048775200a2df719ccf575143ba749a3e9
    )
    FetchContent_MakeAvailable(glfw)
    message(STATUS "GLFW: ${_glfw_origin}")
  endif()
endif()

# --- Platform-specific build ---
#
# The platform layer is split into a GUI half (window/render loop,
# dialogs, entry point) and an OS half (subprocess launch, DAP socket,
# file I/O).  This lets Cygwin mix the Win32+DX11 GUI with POSIX OS
# APIs.  Four valid combinations:
#
#   macOS            gui_glfw  + os_posix  (+ filedialog_macos.mm)
#   Linux            gui_glfw  + os_posix
#   Windows native   gui_win32 + os_win32  (MSVC, MinGW, MSYS2 mingw*)
#   Cygwin / MSYS2   gui_win32 + os_posix  (msys env only — mingw*
#                                           envs hit the WIN32 branch)

if(APPLE)
  find_package(OpenGL REQUIRED)
  target_sources(imgui PRIVATE
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
  add_executable(dcmake src/dcmake.cpp src/dap.cpp src/highlight.cpp
                        src/platform_gui_glfw.cpp src/platform_os_posix.cpp
                        src/filedialog_macos.mm)
  target_link_libraries(dcmake PRIVATE imgui nlohmann_json::nlohmann_json
                        "-framework Cocoa"
                        "-framework UniformTypeIdentifiers")
  target_compile_definitions(dcmake PRIVATE DCMAKE_GLFW)

elseif(WIN32)
  # Native Windows: MSVC or MinGW.  /SUBSYSTEM:WINDOWS via WIN32 flag.
  target_sources(imgui PRIVATE
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  )
  target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)

  configure_file(src/dcmake.rc.in dcmake.rc @ONLY)
  add_executable(dcmake WIN32 src/dcmake.cpp src/dap.cpp src/highlight.cpp
                              src/platform_gui_win32.cpp src/platform_os_win32.cpp
                              ${CMAKE_CURRENT_BINARY_DIR}/dcmake.rc)
  target_link_libraries(dcmake PRIVATE imgui nlohmann_json::nlohmann_json comdlg32)

elseif(CYGWIN OR MSYS)
  # Cygwin or MSYS2's msys environment: Win32+DX11 GUI with POSIX OS
  # APIs.  Both ignore the WIN32 add_executable flag, so request GUI
  # subsystem explicitly with -mwindows.  DAP uses Cygwin AF_UNIX
  # sockets, so the target debuggee must be the same environment's
  # cmake (Cygwin cmake or MSYS2 pacman's msys/cmake), not a native
  # Windows cmake.  MSYS2's mingw64/ucrt64/clang64 environments are
  # native Windows and take the WIN32 branch above instead.
  target_sources(imgui PRIVATE
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  )
  target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)
  # Upstream imgui_impl_dx11.cpp uses intptr_t without including
  # <cstdint>.  MSVC and glibc pull it in transitively; Cygwin's
  # GCC + newlib does not.  Force-include it for the whole imgui
  # target to keep the fix minimal and bounded to this path.
  target_compile_options(imgui PRIVATE -include cstdint)

  configure_file(src/dcmake.rc.in dcmake.rc @ONLY)
  add_executable(dcmake src/dcmake.cpp src/dap.cpp src/highlight.cpp
                        src/platform_gui_win32.cpp src/platform_os_posix.cpp
                        ${CMAKE_CURRENT_BINARY_DIR}/dcmake.rc)
  # ole32 is for CoTaskMemFree (freeing the PIDL returned by
  # SHBrowseForFolderW).  MSVC auto-links it via a pragma in the
  # Windows SDK headers; Cygwin's w32api carries no such pragma.
  target_link_libraries(dcmake PRIVATE imgui nlohmann_json::nlohmann_json
                        comdlg32 ole32)
  target_link_options(dcmake PRIVATE -mwindows)

else()
  # Linux
  find_package(OpenGL REQUIRED)
  target_sources(imgui PRIVATE
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
  add_executable(dcmake src/dcmake.cpp src/dap.cpp src/highlight.cpp
                        src/platform_gui_glfw.cpp src/platform_os_posix.cpp)
  target_link_libraries(dcmake PRIVATE imgui nlohmann_json::nlohmann_json)
  target_compile_definitions(dcmake PRIVATE DCMAKE_GLFW)
endif()

target_compile_definitions(dcmake PRIVATE DCMAKE_VERSION="${PROJECT_VERSION}")
if(WIN32)
  target_compile_definitions(dcmake PRIVATE WINVER=0x0600 _WIN32_WINNT=0x0600)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(dcmake PRIVATE -Wall -Wconversion)
endif()
