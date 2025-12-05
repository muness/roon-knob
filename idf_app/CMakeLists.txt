cmake_minimum_required(VERSION 3.16)

# App version (semver) - update this for releases
set(PROJECT_VER "1.1.1")

# Suppress CMake warnings (ESP-IDF internal component validation issues)
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(roon_knob)
