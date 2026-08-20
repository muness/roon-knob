cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "2.7.0-beta.1")
set(HIPHI_M5_TARGET "atom_joystick" CACHE STRING "Exact M5 target")
set(SDKCONFIG_DEFAULTS
    "sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/../official_components/m5_official/sdkconfig.defaults"
    CACHE STRING "AtomS3 Joystick defaults" FORCE)
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../official_components/m5_official")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(HIPHI_M5_TARGET ${HIPHI_M5_TARGET})
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(COMPILE_OPTIONS
    "-Wno-error=stringop-truncation" APPEND)
project(hiphi_joy)
