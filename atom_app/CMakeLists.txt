cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "2.7.0-alpha.4")
set(HIPHI_M5_TARGET "atom_joystick" CACHE STRING "Exact M5 target")
set(SDKCONFIG_DEFAULTS
    "sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/../official_components/m5_official/sdkconfig.defaults"
    CACHE STRING "AtomS3 Joystick defaults" FORCE)
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../official_components/m5_official")
# Kizz's managed wake-word dependency must not enter sibling target graphs.
set(EXCLUDE_COMPONENTS kizz_wake_word)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(HIPHI_M5_TARGET ${HIPHI_M5_TARGET})
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(COMPILE_OPTIONS
    "-Wno-error=stringop-truncation" APPEND)
project(hiphi_joy)
