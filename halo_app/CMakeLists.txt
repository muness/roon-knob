cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "2.8.0-alpha.1")
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
# Kizz's managed wake-word dependency must not enter sibling target graphs.
set(EXCLUDE_COMPONENTS kizz_wake_word m5_platform)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-truncation" APPEND)
project(hiphi_halo)
