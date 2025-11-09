set(SRC_FILES
    "main.c"
    "ui.c"
    "encoder_input.c"
    "net_client_idf.c"
    "config_store.c"
    "mdns_client.c"
    "wifi_manager.c"
    "ui_network.c"
)

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS
        "."
        "../../include"
    REQUIRES
        mdns
        esp_event
        esp_netif
        esp_timer
        esp_wifi
    PRIV_REQUIRES
        nvs_flash
        esp_http_client
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)
