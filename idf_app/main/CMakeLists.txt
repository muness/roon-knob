set(SRC_FILES
    "main_idf.c"
    "captive_portal.c"
    "config_server.c"
    "http_server_lifecycle.c"
    "dns_server.c"
    "ui_network.c"
    "platform_display_idf.c"
    "display_sleep.c"
    "platform_storage_idf.c"
    "platform_http_idf.c"
    "platform_mdns_idf.c"
    "platform_log_idf.c"
    "platform_input_idf.c"
    "platform_identity_idf.c"
    "platform_provisioning_dial.c"
    "battery.c"
    "ota_update.c"
    "controller_input_profile_dial.c"
    "controller_presentation_dial.c"
    "ble_hid_host_dial.c"
    "font_manager.c"
    # Generated bitmap fonts (run scripts/generate_fonts.sh to regenerate)
    # Typography: Lato for metadata, Noto Sans for content (matches Roon's design)
    "fonts/lato_22.c"
    "fonts/notosans_28.c"
    "fonts/material_icons_22.c"
    "fonts/material_icons_28.c"
    "fonts/material_icons_44.c"
    "fonts/material_icons_60.c"
    "fonts/lucide_battery_22.c"
    "../../common/app_main.c"
    "../../common/bridge_command_plan.c"
    "../../common/bridge_client.c"
    "../../common/controller_action_router.c"
    "../../common/controller_config.c"
    "../../common/controller_ble_input.c"
    "../../common/controller_input.c"
    "../../common/controller_input_mailbox.c"
    "../../common/controller_view.c"
    "../../common/controller_view_compat.c"
    "../../common/wifi_manager.c"
    "../../common/ui.c"
    "../../common/ui_jpeg.c"
    "../../common/platform/platform_log.c"
    "../../common/platform/platform_time.c"
    "../../common/platform/platform_task.c"
)

# Suppress component validation warnings - these are ESP-IDF internal circular dependencies
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
        espressif__esp_lcd_sh8601
        i2c_bsp
        lcd_touch_bsp
    PRIV_REQUIRES
        nvs_flash
        esp_http_client
        esp_http_server
        driver
        esp_driver_ledc
        esp_driver_uart
        esp_adc
        app_update
        json
        rk_ble_hid_host
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)

target_compile_definitions(${COMPONENT_LIB} PRIVATE TARGET_PC=0)
# TODO(#183): Remove after ESP-IDF 6.0 migration (Picolibc has proper strlcpy)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=stringop-truncation)
