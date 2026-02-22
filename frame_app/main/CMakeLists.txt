set(SRC_FILES
    "main_frame.c"
    "eink_display.c"
    "eink_dither.c"
    "eink_ui.c"
    "eink_font.c"
    "platform_display_frame.c"
    "platform_input_frame.c"
    "platform_http_frame.c"
    "platform_storage_frame.c"
    "platform_mdns_frame.c"
    "platform_wifi_frame.c"
    "platform_log_frame.c"
    "wifi_manager.c"
    "captive_portal.c"
    "dns_server.c"
    "pmic_axp2101.c"
    "ble_remote.c"
    "../../common/app_main.c"
    "../../common/bridge_client.c"
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
    PRIV_REQUIRES
        nvs_flash
        esp_http_client
        esp_http_server
        esp_app_format
        driver
        json
        bt
        esp_hid
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)

target_compile_definitions(${COMPONENT_LIB} PRIVATE TARGET_PC=0)
target_compile_definitions(${COMPONENT_LIB} PRIVATE USE_EINK=1)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
