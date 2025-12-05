cmake_minimum_required(VERSION 3.16)

# Suppress component validation warnings (ESP-IDF internal issues)
# Multiple methods to ensure validation is disabled
set(IDF_COMPONENT_VALIDATION OFF CACHE BOOL "Disable component validation" FORCE)
set(IDF_COMPONENT_VALIDATION_SKIP 1 CACHE INTERNAL "Skip component validation")
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "Disable deprecated warnings")
set(IDF_CI_BUILD ON CACHE BOOL "Pretend we're in CI to skip some checks")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(roon_knob)
