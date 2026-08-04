idf_component_register(
    SRCS "rk_ble_hid_host.c" "rk_ble_hid_policy.c" "rk_ble_hid_report.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES freertos esp_event nvs_flash bt esp_hid
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)
