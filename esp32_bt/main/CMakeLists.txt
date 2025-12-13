idf_component_register(
    SRCS
        "main.c"
        "bt_avrcp.c"
        "uart_protocol.c"
        "ble_hid_vol.c"
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        bt
        nvs_flash
        driver
        ble_hid
)

# HACK: Add internal Bluedroid include paths to access BTA structures for SDP manipulation
# This is needed to delete the A2DP SDP record so phones don't see us as an audio sink
idf_build_get_property(idf_path IDF_PATH)
target_include_directories(__idf_main PRIVATE
    "${idf_path}/components/bt/common/include"
    "${idf_path}/components/bt/host/bluedroid/bta/include"
    "${idf_path}/components/bt/host/bluedroid/bta/av/include"
    "${idf_path}/components/bt/host/bluedroid/stack/include"
    "${idf_path}/components/bt/host/bluedroid/common/include"
)
