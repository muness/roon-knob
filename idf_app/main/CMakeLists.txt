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
    "../../common/app_main.c"
    "../../common/roon_client.c"
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
        esp_adc
        espressif__esp_new_jpeg
        app_update
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)

target_compile_definitions(${COMPONENT_LIB} PRIVATE TARGET_PC=0)
