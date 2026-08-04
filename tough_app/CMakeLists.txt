cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")

# Auto-append -dev if not building from a release tag
execute_process(
    COMMAND git describe --exact-match --tags HEAD
    RESULT_VARIABLE NOT_ON_TAG
    OUTPUT_QUIET ERROR_QUIET
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
if(NOT_ON_TAG)
    string(APPEND PROJECT_VER "-dev")
endif()

set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)

list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Without this, ESP-IDF builds every component discovered under
# EXTRA_COMPONENT_DIRS regardless of whether anything requires it -- which
# would silently pull in components/rk_ble_hid_host (and its "bt"/NimBLE
# dependency) even though no file in tough_app/ references BLE. Tough has no
# BLE component this Alpha (see tough_capabilities.h / #193 / #191); this
# property is how that is enforced at the build-system level, not just by
# convention. Matches tests/fixtures/rk_ble_hid_off's use of the same
# property for the same reason.
idf_build_set_property(MINIMAL_BUILD ON)
project(hiphi_tough)
