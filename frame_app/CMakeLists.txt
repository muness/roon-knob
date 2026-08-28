cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "2.7.0-alpha.4")

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

list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
# Frame keeps its proven e-ink/BLE platform and must not resolve the unrelated
# M5Unified component merely because the shared components directory is visible.
set(EXCLUDE_COMPONENTS m5_platform kizz_wake_word)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hiphi_frame)
