cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../../components"
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/rk_ble_hid_host/optional"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(rk_ble_hid_off)
