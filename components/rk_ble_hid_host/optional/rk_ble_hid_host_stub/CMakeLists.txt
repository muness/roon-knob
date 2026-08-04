set(RK_BLE_HID_HOST_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")

idf_component_register(
    SRCS
        "${RK_BLE_HID_HOST_SOURCE_DIR}/rk_ble_hid_host_stub.c"
        "${RK_BLE_HID_HOST_SOURCE_DIR}/rk_ble_hid_report.c"
    INCLUDE_DIRS "${RK_BLE_HID_HOST_SOURCE_DIR}/include"
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)
