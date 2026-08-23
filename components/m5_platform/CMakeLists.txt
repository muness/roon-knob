set(m5_platform_requires m5stack__m5unified)
idf_build_get_property(m5_platform_target HIPHI_M5_TARGET)
if(m5_platform_target STREQUAL "dial" OR
   m5_platform_target STREQUAL "stackchan" OR
   m5_platform_target STREQUAL "atom_joystick")
    list(APPEND m5_platform_requires m5_official)
endif()
if(m5_platform_target STREQUAL "stackchan")
    list(APPEND m5_platform_requires
        esp_websocket_client json esp-sr nvs_flash lwip kizz_wake_word)
endif()

idf_component_register(
    SRCS "m5_platform.cpp" "m5_interaction_policy.c"
    # The Kizz personality catalogs are deliberately public data headers:
    # current and future adaptive-UI renderers consume the same vocabulary.
    INCLUDE_DIRS "include" "."
    REQUIRES ${m5_platform_requires}
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    OUTPUT_VARIABLE hiphi_firmware_git_sha
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT hiphi_firmware_git_sha)
    set(hiphi_firmware_git_sha "unknown")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    HIPHI_FIRMWARE_GIT_SHA="${hiphi_firmware_git_sha}")
