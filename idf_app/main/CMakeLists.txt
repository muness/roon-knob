set(SRC_FILES
    "main_idf.c"
    "wifi_manager.c"
    "ui_network.c"
    "platform_display_idf.c"
    "platform_storage_idf.c"
    "platform_http_idf.c"
    "platform_mdns_idf.c"
    "platform_log_idf.c"
    "platform_input_idf.c"
    "platform_power_idf.c"
    "cst816.c"
    "../../common/app_main.c"
    "../../common/roon_client.c"
    "../../common/ui.c"
    "../../common/platform/platform_log.c"
    "../../common/platform/platform_time.c"
    "../../common/platform/platform_task.c"
)

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
        esp_lcd
        lvgl__lvgl
        espressif__esp_lcd_sh8601
    PRIV_REQUIRES
        nvs_flash
        esp_http_client
        driver
        esp_driver_ledc
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)

target_compile_definitions(${COMPONENT_LIB} PRIVATE TARGET_PC=0)
