# Create a source release tarball with bundled dependencies.
# Usage: cmake -P cmake/SourceRelease.cmake
# Produces: dcmake-VERSION.tar.gz in the project root.

cmake_minimum_required(VERSION 3.25)

set(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

# Require git
find_program(GIT git REQUIRED)

# Determine version from git tags
execute_process(
  COMMAND "${GIT}" describe --tags --abbrev=8 --always
  WORKING_DIRECTORY "${SOURCE_DIR}"
  OUTPUT_VARIABLE VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE rc
)
if(rc OR NOT VERSION)
  string(TIMESTAMP VERSION "%Y%m%d" UTC)
endif()

set(NAME "dcmake-${VERSION}")
set(WORK "${SOURCE_DIR}/_source_release")
set(STAGE "${WORK}/${NAME}")
set(OUTPUT "${SOURCE_DIR}/${NAME}.tar.gz")

message(STATUS "Preparing source release: ${NAME}")

# Clean working area
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

# Export git tree into staging directory
message(STATUS "Exporting git tree...")
execute_process(
  COMMAND "${GIT}" archive --format=tar --prefix=${NAME}/ HEAD
  WORKING_DIRECTORY "${SOURCE_DIR}"
  OUTPUT_FILE "${WORK}/src.tar"
  RESULT_VARIABLE rc
)
if(rc)
  message(FATAL_ERROR "git archive failed")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E tar xf src.tar
  WORKING_DIRECTORY "${WORK}"
  RESULT_VARIABLE rc
)
file(REMOVE "${WORK}/src.tar")
if(rc)
  message(FATAL_ERROR "Failed to extract git archive")
endif()

# Bundle dependencies into the staging tree
message(STATUS "Bundling dependencies...")
set(DEPS_DIR "${STAGE}/deps")
include("${CMAKE_CURRENT_LIST_DIR}/BundleDeps.cmake")

# Create final tarball
message(STATUS "Creating ${NAME}.tar.gz...")
execute_process(
  COMMAND ${CMAKE_COMMAND} -E tar czf "${OUTPUT}" "${NAME}"
  WORKING_DIRECTORY "${WORK}"
  RESULT_VARIABLE rc
)
if(rc)
  message(FATAL_ERROR "Failed to create tarball")
endif()

# Clean up
file(REMOVE_RECURSE "${WORK}")

file(SIZE "${OUTPUT}" size)
math(EXPR size_kb "${size} / 1024")
message(STATUS "Source release: ${OUTPUT} (${size_kb} KiB)")
