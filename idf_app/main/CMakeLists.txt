set(SRC_FILES
    "main_idf.c"
    "wifi_manager.c"
    "captive_portal.c"
    "config_server.c"
    "dns_server.c"
    "ui_network.c"
    "platform_display_idf.c"
    "display_sleep.c"
    "platform_storage_idf.c"
    "platform_http_idf.c"
    "platform_mdns_idf.c"
    "platform_log_idf.c"
    "platform_input_idf.c"
    "battery.c"
    "ota_update.c"
    "font_manager.c"
    # Generated bitmap fonts (run scripts/generate_fonts.sh to regenerate)
    # Typography: Lato for metadata, Noto Sans for content (matches Roon's design)
    # Each family at three sizes: small (22px), medium (25px), large (28px)
    "fonts/lato_22.c"
    "fonts/lato_25.c"
    "fonts/lato_28.c"
    "fonts/notosans_22.c"
    "fonts/notosans_25.c"
    "fonts/notosans_28.c"
    "fonts/material_icons_22.c"
    "fonts/material_icons_25.c"
    "fonts/material_icons_28.c"
    "fonts/material_icons_44.c"
    "fonts/material_icons_60.c"
    "fonts/lucide_battery_22.c"
    "../../common/app_main.c"
    "../../common/bridge_client.c"
    "../../common/display_config.c"
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
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)

target_compile_definitions(${COMPONENT_LIB} PRIVATE TARGET_PC=0)

