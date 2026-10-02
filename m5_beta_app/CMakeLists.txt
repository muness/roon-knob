cmake_minimum_required(VERSION 3.16)

# Component Manager cannot resolve a plain relative local source in a lock.
# Bind its portable lock placeholder before loading IDF/project dependencies.
set(ENV{HIPHI_M5_PROJECT_DIR} "${CMAKE_CURRENT_LIST_DIR}")

set(HIPHI_M5_TARGET "sticks3" CACHE STRING
    "M5 beta target: dial, sticks3, stopwatch, or stackchan")
option(HIPHI_KIZZ_WAKE_WORD
    "Build the experimental Kizz wake/voice pipeline (requires a configured endpoint)" OFF)
set(_valid_targets dial sticks3 stopwatch stackchan)
if(NOT HIPHI_M5_TARGET IN_LIST _valid_targets)
    message(FATAL_ERROR "HIPHI_M5_TARGET must be one of: ${_valid_targets}")
endif()

if(HIPHI_KIZZ_WAKE_WORD AND NOT HIPHI_M5_TARGET STREQUAL "stackchan")
    message(FATAL_ERROR "HIPHI_KIZZ_WAKE_WORD is only available for the StackChan target")
endif()

set(PROJECT_VER "2.7.0-alpha.4")
set(_sdkconfig_defaults
    "sdkconfig.defaults;sdkconfig.${HIPHI_M5_TARGET}.defaults")
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
if(NOT HIPHI_KIZZ_WAKE_WORD)
    # Production has no voice endpoint. Keep model, AOT, TFLM and ESP-SR
    # runtime code out, including builds that retain old managed caches.
    set(EXCLUDE_COMPONENTS kizz_wake_word micro_wake_word espressif__esp-nn)
    # Arduino 3.3.11's manifest requires ESP-SR on every S3 build even when
    # CONFIG_ARDUINO_SELECTIVE_ESP_SR is off. EXCLUDE_COMPONENTS only trims
    # roots, not that transitive edge. Satisfy the unused edge with a source-
    # and header-free interface; explicit wake ON resolves managed ESP-SR.
    # Do not exclude the adapter: IDF would omit it from the local override
    # list and the component manager would restore the managed implementation.
    list(APPEND EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../optional_components/voice_disabled/espressif__esp-sr")
else()
    list(APPEND _sdkconfig_defaults "sdkconfig.stackchan.voice.defaults")
endif()
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
idf_build_set_property(HIPHI_KIZZ_WAKE_WORD ${HIPHI_KIZZ_WAKE_WORD})
idf_build_set_property(MINIMAL_BUILD ON)
if(HIPHI_M5_TARGET STREQUAL "sticks3" OR
   HIPHI_M5_TARGET STREQUAL "stopwatch")
    # These profiles use native IDF/M5Unified without Arduino's dependency
    # graph. Keep their lock separate so switching boards preserves pins.
    idf_build_set_property(DEPENDENCIES_LOCK
        "${CMAKE_CURRENT_LIST_DIR}/dependencies.native.lock")
else()
    idf_build_set_property(DEPENDENCIES_LOCK
        "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock")
endif()
if(HIPHI_M5_TARGET STREQUAL "dial" OR
   HIPHI_M5_TARGET STREQUAL "stackchan")
    # Arduino's published ESP-IDF 5.5 dependency graph currently includes an
    # esp-modbus source warning that GCC 14 otherwise promotes to an error.
    idf_build_set_property(COMPILE_OPTIONS
        "-Wno-error=stringop-truncation" APPEND)
endif()
# The Waveshare Dial is already project hiphi_dial. Keep the M5 firmware's
# embedded application identity distinct even though both chips are ESP32-S3.
if(HIPHI_M5_TARGET STREQUAL "dial")
    project(hiphi_m5dial)
else()
    project(hiphi_${HIPHI_M5_TARGET})
endif()
