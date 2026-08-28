set(SRC_FILES
    "main_rlcd.c"
    "rlcd_art_dither.c"
    "rlcd_display.c"
    "rlcd_st7305_window.c"
    "rlcd_text.c"
    "rlcd_ui.c"
    "rlcd_power_manager.c"
    "rlcd_battery.c"
    "controller_presentation_rlcd.c"
    "controller_input_profile_rlcd.c"
    "platform_input_rlcd.c"
    "platform_display_rlcd.c"
    "platform_identity_rlcd.c"
    "ble_hid_host_rlcd.c"
    "rlcd_portal_ui_compat.c"
    "../../common/app_main.c"
    "../../common/bridge_command_plan.c"
    "../../common/bridge_client.c"
    "../../common/controller_action_router.c"
    "../../common/controller_ble_input.c"
    "../../common/controller_button_gesture.c"
    "../../common/controller_config.c"
    "../../common/controller_input.c"
    "../../common/controller_input_mailbox.c"
    "../../common/controller_view.c"
    "../../common/controller_view_compat.c"
    "../../common/controller_text_ascii.c"
    "../../common/controller_utf8.c"
    "../../common/wifi_portal_form.c"
    "../../common/wifi_manager.c"
    "../../common/power_debug_web.c"
    "../../common/platform/platform_log.c"
    "../../common/platform/platform_power.c"
    "../../common/platform/platform_task.c"
    "../../common/platform/platform_time.c"
    "../../frame_app/main/captive_portal.c"
    "../../frame_app/main/dns_server.c"
    "../../frame_app/main/platform_http_frame.c"
    "../../frame_app/main/platform_mdns_frame.c"
    "../../frame_app/main/platform_provisioning_frame.c"
    "../../frame_app/main/platform_storage_frame.c"
)

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "." "../../include" "../../common" "../../frame_app/main"
    REQUIRES mdns esp_event esp_netif esp_timer esp_wifi wpa_supplicant lvgl esp_lcd
    PRIV_REQUIRES nvs_flash esp_http_client esp_http_server esp_app_format driver esp_adc json bt esp_hid rk_ble_hid_host esp_pm
)

set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TARGET_PC=0
    PLATFORM_MDNS_PRODUCT="hiphi-rlcd-42"
    PLATFORM_HOSTNAME_FROM_BRIDGE_NAME=0
    PLATFORM_PORTAL_PRODUCT_NAME="HiPhi RLCD"
    PLATFORM_PORTAL_PRODUCT_SLUG="hiphi rlcd"
    PLATFORM_PORTAL_FRAME_DISPLAY_SETTINGS=0
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=stringop-truncation)
