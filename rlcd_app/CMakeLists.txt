cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "2.7.0-beta.1")
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
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
# M5Unified is a Tough-only display dependency.  Keep the shared component
# directory available for common code without making RLCD resolve the
# unrelated M5 component (and its managed dependencies).
set(EXCLUDE_COMPONENTS m5_platform)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hiphi_rlcd_42)
