cmake_minimum_required(VERSION 3.25)
set(DCMAKE_VERSION 1.5.0)
if(APPLE)
  project(dcmake VERSION ${DCMAKE_VERSION} LANGUAGES CXX OBJCXX)
else()
  project(dcmake VERSION ${DCMAKE_VERSION} LANGUAGES CXX)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DEPS "AUTO" CACHE STRING
    "Dependency resolution for json and GLFW: AUTO, DOWNLOAD, or LOCAL")
set_property(CACHE DEPS PROPERTY STRINGS AUTO DOWNLOAD LOCAL)

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

if(DEPS STREQUAL "LOCAL")
  find_package(nlohmann_json REQUIRED)
  message(STATUS "nlohmann/json: system")
  if(NOT WIN32)
    find_package(glfw3 REQUIRED)
    message(STATUS "GLFW: system")
  endif()
else()
  # Point FetchContent at bundled sources if present
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/deps/json")
    set(FETCHCONTENT_SOURCE_DIR_JSON "${CMAKE_CURRENT_SOURCE_DIR}/deps/json")
    set(_json_origin "bundled")
  endif()
  if(NOT WIN32 AND IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/deps/glfw")
    set(FETCHCONTENT_SOURCE_DIR_GLFW "${CMAKE_CURRENT_SOURCE_DIR}/deps/glfw")
    set(_glfw_origin "bundled")
  endif()

  # AUTO fallback: if disconnected and no bundled sources, try system
  if(DEPS STREQUAL "AUTO"
     AND NOT DEFINED FETCHCONTENT_SOURCE_DIR_JSON
     AND FETCHCONTENT_FULLY_DISCONNECTED)
    find_package(nlohmann_json QUIET)
    if(nlohmann_json_FOUND)
      set(_json_origin "system")
    endif()
    if(NOT WIN32)
      find_package(glfw3 QUIET)
      if(glfw3_FOUND)
        set(_glfw_origin "system")
      endif()
    endif()
  endif()

  if(NOT _json_origin STREQUAL "system")
    if(NOT _json_origin)
      set(_json_origin "downloaded")
    endif()
    FetchContent_Declare(
      json
      URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
      URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    )
    FetchContent_MakeAvailable(json)
  endif()
  message(STATUS "nlohmann/json: ${_json_origin}")

  if(NOT WIN32 AND NOT _glfw_origin STREQUAL "system")
    if(NOT _glfw_origin)
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
  endif()
  if(NOT WIN32)
    message(STATUS "GLFW: ${_glfw_origin}")
  endif()
endif()

# --- Platform-specific build ---

if(NOT WIN32)
  find_package(OpenGL REQUIRED)
  target_sources(imgui PRIVATE
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

  if(APPLE)
    add_executable(dcmake src/dcmake.cpp src/dap.cpp src/highlight.cpp
                          src/platform_glfw.cpp src/filedialog_macos.mm)
    target_link_libraries(dcmake PRIVATE imgui nlohmann_json::nlohmann_json
                          "-framework Cocoa"
                          "-framework UniformTypeIdentifiers")
  else()
    add_executable(dcmake src/dcmake.cpp src/dap.cpp src/highlight.cpp
                          src/platform_glfw.cpp)
    target_link_libraries(dcmake PRIVATE imgui nlohmann_json::nlohmann_json)
  endif()
  target_compile_definitions(dcmake PRIVATE DCMAKE_GLFW)
else()
  target_sources(imgui PRIVATE
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  )
  target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)

  add_executable(dcmake WIN32 src/dcmake.cpp src/dap.cpp src/highlight.cpp
                              src/platform_win32.cpp src/dcmake.rc)
  target_link_libraries(dcmake PRIVATE imgui nlohmann_json::nlohmann_json comdlg32)
endif()

target_compile_definitions(dcmake PRIVATE DCMAKE_VERSION="${PROJECT_VERSION}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(dcmake PRIVATE -Wall -Wconversion)
endif()
