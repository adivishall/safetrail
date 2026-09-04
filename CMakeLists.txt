cmake_minimum_required(VERSION 3.16)
project(safetrail LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)

# Sanitizers on by default in Debug. Geometry bugs and off-by-one index errors are
# exactly what ASan/UBSan catch, and this project has a lot of both kinds of code.
# `make asan` is the equivalent for the plain-make path.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

# ── Source set ────────────────────────────────────────────────────────────────
#
# Discovered by the SAME glob patterns the Makefile uses (src/**.cpp and
# tests/**_test.cpp), and sorted for a reproducible link order. Neither build
# system carries a hand-written list, so they cannot drift apart -- which they did
# historically, when the Makefile's explicit CORE list fell behind the tree.
# `make manifest` prints what the Makefile sees; the check below prints what CMake
# sees, and the CI job builds both.
file(GLOB_RECURSE SAFETRAIL_SRC CONFIGURE_DEPENDS src/*.cpp)
list(SORT SAFETRAIL_SRC)
list(LENGTH SAFETRAIL_SRC SAFETRAIL_SRC_COUNT)
message(STATUS "safetrail: ${SAFETRAIL_SRC_COUNT} source files")

add_library(safetrail STATIC ${SAFETRAIL_SRC})
target_include_directories(safetrail PUBLIC include)

foreach(app safetrail_headless safetrail_bench)
  if(EXISTS ${CMAKE_SOURCE_DIR}/apps/${app}.cpp)
    add_executable(${app} apps/${app}.cpp)
    target_link_libraries(${app} PRIVATE safetrail)
  endif()
endforeach()

# ── Tests ─────────────────────────────────────────────────────────────────────
#
# Several tests read data/ and write scratch files into build/, so they run from
# the source directory and need build/ to exist. Test names carry their directory
# so that, for example, geo/projection and index/serialization cannot collide with
# a future same-named file elsewhere in the tree.
enable_testing()
file(GLOB_RECURSE TEST_SRC CONFIGURE_DEPENDS tests/*_test.cpp)
list(SORT TEST_SRC)
list(LENGTH TEST_SRC TEST_COUNT)
message(STATUS "safetrail: ${TEST_COUNT} test files")

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

foreach(test_file ${TEST_SRC})
  file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR}/tests ${test_file})
  string(REPLACE ".cpp" "" rel ${rel})
  string(REPLACE "/" "_" target ${rel})
  add_executable(${target} ${test_file})
  target_link_libraries(${target} PRIVATE safetrail)
  target_include_directories(${target} PRIVATE tests)
  add_test(NAME ${rel} COMMAND ${target} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()
