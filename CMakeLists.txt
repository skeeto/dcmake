cmake_minimum_required(VERSION 3.25)
if(APPLE)
  project(dcmake LANGUAGES CXX OBJCXX)
else()
  project(dcmake LANGUAGES CXX)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)

FetchContent_Declare(
  imgui
  URL https://github.com/ocornut/imgui/archive/f5f6ca07be7ce0ea9eed6c04d55833bac3f6b50b.tar.gz
  URL_HASH SHA256=91aed5e92c1e24aada8dfd2a6933222341ec291dc29a4add49828610d5cb6765
)

FetchContent_Declare(
  glfw
  URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
  URL_HASH SHA256=b5ec004b2712fd08e8861dc271428f048775200a2df719ccf575143ba749a3e9
)

FetchContent_MakeAvailable(json)

# Dear ImGui has no CMakeLists.txt -- build it ourselves
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

if(NOT WIN32)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glfw)

  target_sources(imgui PRIVATE
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_link_libraries(imgui PUBLIC glfw)

  if(APPLE)
    add_executable(dcmake src/dcmake.cpp src/platform_glfw.cpp
                          src/filedialog_macos.mm)
    target_link_libraries(dcmake PRIVATE imgui nlohmann_json::nlohmann_json
                          "-framework OpenGL" "-framework Cocoa")
  else()
    add_executable(dcmake src/dcmake.cpp src/platform_glfw.cpp)
    target_link_libraries(dcmake PRIVATE imgui nlohmann_json::nlohmann_json GL)
  endif()
else()
  target_sources(imgui PRIVATE
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  )
  target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)

  add_executable(dcmake WIN32 src/dcmake.cpp src/platform_win32.cpp src/dcmake.rc)
  target_link_libraries(dcmake PRIVATE imgui nlohmann_json::nlohmann_json comdlg32)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(dcmake PRIVATE -Wall -Wextra -Wconversion)
endif()
