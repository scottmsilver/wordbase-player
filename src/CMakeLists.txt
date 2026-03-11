cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(WordBase LANGUAGES CXX)

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WORDBASE_DEPENDENCY_PREFIX "" CACHE PATH
  "Optional prefix containing non-system dependencies, for example a Conda environment")

if (WORDBASE_DEPENDENCY_PREFIX)
  message(STATUS "Using non-system dependency prefix: ${WORDBASE_DEPENDENCY_PREFIX}")
else()
  message(STATUS "Using system dependency search paths only")
  set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
  set(CMAKE_FIND_USE_PACKAGE_REGISTRY FALSE)
  set(CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH FALSE)
endif()

function(wordbase_append_prefix_hints out_var)
  set(hints)
  if (WORDBASE_DEPENDENCY_PREFIX)
    list(APPEND hints "${WORDBASE_DEPENDENCY_PREFIX}")
  endif()
  set(${out_var} "${hints}" PARENT_SCOPE)
endfunction()

wordbase_append_prefix_hints(WORDBASE_DEPENDENCY_HINTS)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)

option(WORDBASE_USE_SIMD "Enable AVX2 SIMD optimizations for hot paths" OFF)

find_package(Boost 1.45 REQUIRED COMPONENTS timer)
find_package(OpenSSL REQUIRED)

find_path(WORDBASE_SQLITE3_INCLUDE_DIR
  NAMES sqlite3.h
  HINTS ${WORDBASE_DEPENDENCY_HINTS}
  PATH_SUFFIXES include include/x86_64-linux-gnu
  PATHS /usr/include /usr/include/x86_64-linux-gnu /usr/local/include)

find_library(WORDBASE_SQLITE3_LIBRARY
  NAMES sqlite3 libsqlite3.so libsqlite3.so.0
  HINTS ${WORDBASE_DEPENDENCY_HINTS}
  PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu
  PATHS /usr/lib /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/local/lib)

if (NOT WORDBASE_SQLITE3_INCLUDE_DIR OR NOT WORDBASE_SQLITE3_LIBRARY)
  message(FATAL_ERROR
    "Could not find SQLite3. Set WORDBASE_DEPENDENCY_PREFIX or pass "
    "-DWORDBASE_SQLITE3_INCLUDE_DIR=... and -DWORDBASE_SQLITE3_LIBRARY=...")
endif()

find_path(WORDBASE_READLINE_INCLUDE_DIR
  NAMES editline/readline.h readline/readline.h
  HINTS ${WORDBASE_DEPENDENCY_HINTS}
  PATH_SUFFIXES include include/editline include/readline
  PATHS /usr/include /usr/include/x86_64-linux-gnu /usr/local/include)

find_library(WORDBASE_READLINE_LIBRARY
  NAMES readline libreadline.so libreadline.so.8 edit libedit.so libedit.so.2
  HINTS ${WORDBASE_DEPENDENCY_HINTS}
  PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu
  PATHS /usr/lib /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/local/lib)

if (NOT WORDBASE_READLINE_INCLUDE_DIR OR NOT WORDBASE_READLINE_LIBRARY)
  message(FATAL_ERROR
    "Could not find readline/editline. Set WORDBASE_DEPENDENCY_PREFIX or pass "
    "-DWORDBASE_READLINE_INCLUDE_DIR=... and -DWORDBASE_READLINE_LIBRARY=...")
endif()

add_executable(example example.cpp obstack/obstack.cpp)
target_link_libraries(example PRIVATE gtest gtest_main ${Boost_LIBRARIES})
add_test(NAME example_test COMMAND example)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cli-smoke.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/cli-smoke.cmake
  @ONLY)
add_test(
  NAME wordbase_driver_cli_smoke_test
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cli-smoke.cmake)

add_executable(perf-test perf-test.cpp obstack/obstack.cpp)
target_compile_definitions(perf-test PRIVATE HAS_BOOST)
target_include_directories(perf-test PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(perf-test PRIVATE ${Boost_LIBRARIES})

add_executable(decrypt decrypt.cpp)
target_include_directories(decrypt PRIVATE
  ${OPENSSL_INCLUDE_DIR}
  ${WORDBASE_SQLITE3_INCLUDE_DIR})
target_link_libraries(decrypt PRIVATE
  ${OPENSSL_LIBRARIES}
  ${WORDBASE_SQLITE3_LIBRARY})

add_executable(test-namespace test-namespace.cpp)
target_include_directories(test-namespace PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(test-namespace PRIVATE ${Boost_LIBRARIES})

add_executable(wordbase-driver driver.cpp obstack/obstack.cpp)
target_compile_definitions(wordbase-driver PRIVATE HAS_BOOST)
target_include_directories(wordbase-driver PRIVATE
  ${Boost_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIR}
  ${WORDBASE_SQLITE3_INCLUDE_DIR}
  ${WORDBASE_READLINE_INCLUDE_DIR})
target_link_libraries(wordbase-driver PRIVATE
  ${Boost_LIBRARIES}
  ${OPENSSL_LIBRARIES}
  ${WORDBASE_SQLITE3_LIBRARY}
  ${WORDBASE_READLINE_LIBRARY})

if (WORDBASE_USE_SIMD)
  message(STATUS "AVX2 SIMD optimizations enabled")
  foreach(tgt example perf-test wordbase-driver)
    target_compile_definitions(${tgt} PRIVATE WORDBASE_USE_SIMD)
    target_compile_options(${tgt} PRIVATE -mavx2)
  endforeach()
endif()

add_executable(test-simd test-simd.cpp)
target_compile_options(test-simd PRIVATE -mavx2)
target_compile_definitions(test-simd PRIVATE WORDBASE_USE_SIMD)
add_test(NAME simd_correctness_test COMMAND test-simd --test)
