cmake_minimum_required(VERSION 3.16)
project(safetrail LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)

# Sanitizers on by default in Debug. Geometry bugs and off-by-one index errors are
# exactly what ASan/UBSan catch, and this project has a lot of both kinds of code.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

file(GLOB_RECURSE SAFETRAIL_SRC src/*.cpp)
add_library(safetrail STATIC ${SAFETRAIL_SRC})
target_include_directories(safetrail PUBLIC include)

foreach(app safetrail_server safetrail_headless safetrail_bench)
  if(EXISTS ${CMAKE_SOURCE_DIR}/apps/${app}.cpp)
    add_executable(${app} apps/${app}.cpp)
    target_link_libraries(${app} PRIVATE safetrail)
  endif()
endforeach()

enable_testing()
file(GLOB_RECURSE TEST_SRC tests/*_test.cpp)
foreach(test_file ${TEST_SRC})
  get_filename_component(test_name ${test_file} NAME_WE)
  add_executable(${test_name} ${test_file})
  target_link_libraries(${test_name} PRIVATE safetrail)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
