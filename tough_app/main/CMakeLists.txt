set(SRC_FILES
    "main_tough.c"
    "touch_ui.c"
    "controller_input_profile_tough.c"
    "controller_presentation_tough.c"
    "platform_display_tough.c"
    "platform_input_tough.c"
    "platform_http_tough.c"
    "platform_storage_tough.c"
    "platform_mdns_tough.c"
    "platform_log_tough.c"
    "platform_identity_tough.c"
    "platform_provisioning_tough.c"
    "captive_portal.c"
    "dns_server.c"
    "../../common/app_main.c"
    "../../common/bridge_command_plan.c"
    "../../common/bridge_client.c"
    "../../common/controller_action_router.c"
    "../../common/controller_config.c"
    "../../common/controller_input.c"
    "../../common/controller_input_mailbox.c"
    "../../common/controller_view.c"
    "../../common/controller_view_compat.c"
    "../../common/wifi_manager.c"
    "../../common/platform/platform_log.c"
    "../../common/platform/platform_time.c"
    "../../common/platform/platform_task.c"
)

set_property(DIRECTORY PROPERTY CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON)

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS
        "."
        "../../include"
        "../../common"
    REQUIRES
        mdns
        esp_event
        esp_netif
        esp_timer
        esp_wifi
        wpa_supplicant
        esp_lcd
        lvgl__lvgl
        espressif__esp_lcd_ili9341
        esp_driver_i2c
        esp_driver_gpio
    PRIV_REQUIRES
        nvs_flash
        esp_http_client
        esp_http_server
        esp_app_format
        driver
        json
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)

target_compile_definitions(${COMPONENT_LIB} PRIVATE TARGET_PC=0)
# Match the shipping Dial/Frame targets until ESP-IDF 6.0/Picolibc removes the
# aggressive GCC false positives in legacy bounded string copies.
# TODO(#183): Remove after ESP-IDF 6.0 migration.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=stringop-truncation)
