cmake_minimum_required(VERSION 3.16)

set(HIPHI_M5_TARGET "sticks3" CACHE STRING
    "M5 beta target: dial, sticks3, stopwatch, or stackchan")
set(_valid_targets dial sticks3 stopwatch stackchan)
if(NOT HIPHI_M5_TARGET IN_LIST _valid_targets)
    message(FATAL_ERROR "HIPHI_M5_TARGET must be one of: ${_valid_targets}")
endif()

set(PROJECT_VER "0.1.0-beta.1")
set(SDKCONFIG_DEFAULTS
    "sdkconfig.defaults;sdkconfig.${HIPHI_M5_TARGET}.defaults"
    CACHE STRING "M5 beta target defaults" FORCE)
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(hiphi_${HIPHI_M5_TARGET})
