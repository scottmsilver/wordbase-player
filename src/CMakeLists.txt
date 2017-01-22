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



