cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "2.8.0-alpha.2")
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
# Kizz's managed wake-word dependency must not enter sibling target graphs.
set(EXCLUDE_COMPONENTS kizz_wake_word m5_platform)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-truncation" APPEND)
project(hiphi_halo)

# LVGL 9.6 no longer maps the expansion size from Kconfig. Keep the
# 24 KiB internal arena and the 72 KiB external arena's TLSF capacity.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PUBLIC LV_MEM_POOL_EXPAND_SIZE=73728)
