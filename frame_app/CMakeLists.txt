cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")

# Auto-append -dev if not building from a release tag
execute_process(
    COMMAND git describe --exact-match --tags HEAD
    RESULT_VARIABLE NOT_ON_TAG
    OUTPUT_QUIET ERROR_QUIET
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
if(NOT_ON_TAG)
    string(APPEND PROJECT_VER "-dev")
endif()

set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hiphi_frame)
