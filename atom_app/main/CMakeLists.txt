set(SRC_FILES
    "main_atom.c"
    "touch_ui.cpp"
    "controller_presentation_atom.c"
    "platform_input_atom.c"
    "platform_identity_atom.c"
    "platform_display_atom.c"
    "../../tough_app/main/platform_http_tough.c"
    "../../tough_app/main/platform_storage_tough.c"
    "../../tough_app/main/platform_mdns_tough.c"
    "../../tough_app/main/platform_log_tough.c"
    "../../tough_app/main/platform_provisioning_tough.c"
    "../../tough_app/main/captive_portal.c"
    "../../tough_app/main/dns_server.c"
    "../../common/app_main.c"
    "../../common/bridge_command_plan.c"
    "../../common/bridge_client.c"
    "../../common/controller_action_router.c"
    "../../common/controller_config.c"
    "../../common/controller_input.c"
    "../../common/controller_input_mailbox.c"
    "../../common/controller_view.c"
    "../../common/controller_view_compat.c"
    "../../common/wifi_manager.c"
    "../../common/power_debug_web.c"
    "../../common/platform/platform_log.c"
    "../../common/platform/platform_power.c"
    "../../common/platform/platform_time.c"
    "../../common/platform/platform_task.c")

idf_component_register(SRCS ${SRC_FILES}
    INCLUDE_DIRS "." "../../include" "../../common" "../../tough_app/main"
    REQUIRES mdns esp_event esp_netif esp_timer esp_wifi wpa_supplicant m5_platform
    PRIV_REQUIRES nvs_flash esp_http_client esp_http_server esp_app_format driver json)
set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)
target_compile_definitions(${COMPONENT_LIB} PRIVATE TARGET_PC=0 HIPHI_PRODUCT_NAME=\"hiphi-joy\")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=stringop-truncation)
