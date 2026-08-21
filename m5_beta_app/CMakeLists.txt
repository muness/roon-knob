cmake_minimum_required(VERSION 3.16)

set(HIPHI_M5_TARGET "sticks3" CACHE STRING
    "M5 beta target: dial, sticks3, stopwatch, or stackchan")
set(_valid_targets dial sticks3 stopwatch stackchan)
if(NOT HIPHI_M5_TARGET IN_LIST _valid_targets)
    message(FATAL_ERROR "HIPHI_M5_TARGET must be one of: ${_valid_targets}")
endif()

set(PROJECT_VER "2.7.0-alpha.2")
set(_sdkconfig_defaults
    "sdkconfig.defaults;sdkconfig.${HIPHI_M5_TARGET}.defaults")
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
if(HIPHI_M5_TARGET STREQUAL "dial" OR
   HIPHI_M5_TARGET STREQUAL "stackchan")
    list(APPEND EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../official_components/m5_official")
    list(APPEND _sdkconfig_defaults
        "${CMAKE_CURRENT_LIST_DIR}/../official_components/m5_official/sdkconfig.defaults")
endif()
set(SDKCONFIG_DEFAULTS ${_sdkconfig_defaults}
    CACHE STRING "M5 beta target defaults" FORCE)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(HIPHI_M5_TARGET ${HIPHI_M5_TARGET})
idf_build_set_property(MINIMAL_BUILD ON)
if(HIPHI_M5_TARGET STREQUAL "dial" OR
   HIPHI_M5_TARGET STREQUAL "stackchan")
    # Arduino's published ESP-IDF 5.5 dependency graph currently includes an
    # esp-modbus source warning that GCC 14 otherwise promotes to an error.
    idf_build_set_property(COMPILE_OPTIONS
        "-Wno-error=stringop-truncation" APPEND)
endif()
project(hiphi_${HIPHI_M5_TARGET})
