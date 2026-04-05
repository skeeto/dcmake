# Bundle dependencies into deps/ for offline source releases.
# Usage: cmake -P cmake/BundleDeps.cmake

set(DEPS_DIR "${CMAKE_CURRENT_LIST_DIR}/../deps")

set(JSON_URL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz")
set(JSON_HASH "d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d")

set(IMGUI_URL "https://github.com/ocornut/imgui/archive/f5f6ca07be7ce0ea9eed6c04d55833bac3f6b50b.tar.gz")
set(IMGUI_HASH "91aed5e92c1e24aada8dfd2a6933222341ec291dc29a4add49828610d5cb6765")

set(GLFW_URL "https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip")
set(GLFW_HASH "b5ec004b2712fd08e8861dc271428f048775200a2df719ccf575143ba749a3e9")

function(bundle_dep name url hash)
  set(dest "${DEPS_DIR}/${name}")
  if(IS_DIRECTORY "${dest}")
    message(STATUS "${name}: already bundled")
    return()
  endif()

  # Download archive
  string(REGEX REPLACE ".*/" "" archive "${url}")
  set(archive_path "${DEPS_DIR}/${archive}")
  message(STATUS "${name}: downloading ${url}")
  file(DOWNLOAD "${url}" "${archive_path}"
       EXPECTED_HASH SHA256=${hash}
       SHOW_PROGRESS)

  # Extract to temp directory, then move the single top-level dir to dest
  set(tmp "${DEPS_DIR}/_tmp_${name}")
  file(REMOVE_RECURSE "${tmp}")
  file(MAKE_DIRECTORY "${tmp}")
  file(ARCHIVE_EXTRACT INPUT "${archive_path}" DESTINATION "${tmp}")
  file(REMOVE "${archive_path}")

  # Find the single extracted directory
  file(GLOB children "${tmp}/*")
  list(LENGTH children n)
  if(n EQUAL 1 AND IS_DIRECTORY "${children}")
    file(RENAME "${children}" "${dest}")
  else()
    file(RENAME "${tmp}" "${dest}")
  endif()
  file(REMOVE_RECURSE "${tmp}")

  message(STATUS "${name}: bundled into ${dest}")
endfunction()

file(MAKE_DIRECTORY "${DEPS_DIR}")
bundle_dep(json  "${JSON_URL}"  "${JSON_HASH}")
bundle_dep(imgui "${IMGUI_URL}" "${IMGUI_HASH}")
bundle_dep(glfw  "${GLFW_URL}"  "${GLFW_HASH}")
