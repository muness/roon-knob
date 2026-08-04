cmake_minimum_required(VERSION 3.16)

# App version (semver) - update this for releases
set(PROJECT_VER "2.2.2")

# Auto-append -dev if not building from a release tag
# CI builds from tags get clean version, local builds get -dev
execute_process(
    COMMAND git describe --exact-match --tags HEAD
    RESULT_VARIABLE NOT_ON_TAG
    OUTPUT_QUIET ERROR_QUIET
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
if(NOT_ON_TAG)
    string(APPEND PROJECT_VER "-dev")
endif()

# Suppress CMake warnings (ESP-IDF internal component validation issues)
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)

list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

# Detect when an existing sdkconfig was generated from different defaults. Without
# this, ESP-IDF silently retains old values (including debug optimization).
set(_DEFAULTS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults")
set(_SDKCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig")
set(_STAMP_FILE "${CMAKE_BINARY_DIR}/.sdkconfig_defaults_hash")
if(EXISTS "${_DEFAULTS_FILE}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_DEFAULTS_FILE}")
    file(MD5 "${_DEFAULTS_FILE}" _DEFAULTS_HASH)
    if(EXISTS "${_SDKCONFIG}" AND EXISTS "${_STAMP_FILE}")
        file(READ "${_STAMP_FILE}" _PREV_HASH)
        string(STRIP "${_PREV_HASH}" _PREV_HASH)
        if(NOT "${_DEFAULTS_HASH}" STREQUAL "${_PREV_HASH}")
            message(FATAL_ERROR
                "sdkconfig.defaults changed since the last build; remove sdkconfig and run idf.py build again.")
        endif()
    endif()
    file(WRITE "${_STAMP_FILE}" "${_DEFAULTS_HASH}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hiphi_dial)
