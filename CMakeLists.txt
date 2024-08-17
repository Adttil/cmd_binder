cmake_minimum_required (VERSION 3.8)

project (cmd_binder)

add_library(CMD_BINDER INTERFACE)
target_include_directories(CMD_BINDER INTERFACE include)

option(CMD_BINDER_INCLUDE_TESTS "Include tests" ON)
if(NOT CMD_BINDER_INCLUDE_TESTS)
        return()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:preprocessor /permissive-")
endif()

set(CMAKE_EXE_LINKER_FLAGS "-static")

add_executable(cmd_binder_test "test.cpp")