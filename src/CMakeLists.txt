# Download and unpack googletest at configure time
configure_file(CMakeLists.txt.in
               googletest-download/CMakeLists.txt)
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-download )
execute_process(COMMAND ${CMAKE_COMMAND} --build .
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-download )
 
# Prevent GoogleTest from overriding our compiler/linker options
# when building with Visual Studio
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
 
# Add googletest directly to our build. This adds
# the following targets: gtest, gtest_main, gmock
# and gmock_main
add_subdirectory(${CMAKE_BINARY_DIR}/googletest-src
                 ${CMAKE_BINARY_DIR}/googletest-build)
 
# The gtest/gmock targets carry header search path
# dependencies automatically when using CMake 2.8.11 or
# later. Otherwise we have to add them here ourselves.
if (CMAKE_VERSION VERSION_LESS 2.8.11)
  include_directories("${gtest_SOURCE_DIR}/include"
                      "${gmock_SOURCE_DIR}/include")
endif()
 
# First test
add_executable(example example.cpp)
target_link_libraries(example gtest_main)
add_test(NAME example_test COMMAND example)
set_property(TARGET example PROPERTY CXX_STANDARD 11)
set_property(TARGET example PROPERTY CXX_STANDARD_REQUIRED ON)

# First test
add_executable(perf-test perf-test.cpp)
target_link_libraries(perf-test)
set_property(TARGET perf-test PROPERTY CXX_STANDARD 11)
set_property(TARGET perf-test PROPERTY CXX_STANDARD_REQUIRED ON)

add_executable(test-namespace test-namespace.cpp)
target_link_libraries(test-namespace)
set_property(TARGET test-namespace PROPERTY CXX_STANDARD 11)
set_property(TARGET test-namespace PROPERTY CXX_STANDARD_REQUIRED ON)

project(WordBase CXX)
cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

add_executable(wordbase-driver driver.cpp)
set_property(TARGET wordbase-driver PROPERTY CXX_STANDARD 11)
set_property(TARGET wordbase-driver PROPERTY CXX_STANDARD_REQUIRED ON)
target_link_libraries(wordbase-driver readline)

FIND_PACKAGE(Boost)
IF (Boost_FOUND)
    INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR})
    ADD_DEFINITIONS( "-DHAS_BOOST" )
ENDIF()



