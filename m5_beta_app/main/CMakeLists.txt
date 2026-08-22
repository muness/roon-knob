set(SRC_FILES
    "touch_ui.cpp"
    "controller_presentation_beta.c"
    "controller_presentation_semantic.c"
    "kizz_semantic_layout.cpp"
    "kizz_semantic_native_layout.cpp"
    "controller_input_profile_beta.c"
    "platform_input_beta.c"
    "platform_identity_beta.c"
    "platform_display_beta.c"
    "../../tough_app/main/main_tough.c"
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
    "../../common/surface_projection_snapshot.c"
    "../../common/controller_action_router.c"
    "../../common/controller_config.c"
    "../../common/controller_input.c"
    "../../common/controller_input_mailbox.c"
    "../../common/controller_view.c"
    "../../common/controller_view_compat.c"
    "../../common/wifi_manager.c"
    "../../common/power_debug_web.c"
    "../../common/m5_terminal_power.c"
    "../../common/platform/platform_log.c"
    "../../common/platform/platform_power.c"
    "../../common/platform/platform_time.c"
    "../../common/platform/platform_task.c")

set(_target_id 0)
if(HIPHI_M5_TARGET STREQUAL "dial")
    set(_target_id 1)
    set(_slug "hiphi-dial-beta")
elseif(HIPHI_M5_TARGET STREQUAL "sticks3")
    set(_target_id 2)
    set(_slug "hiphi-sticks3-beta")
elseif(HIPHI_M5_TARGET STREQUAL "stopwatch")
    set(_target_id 3)
    set(_slug "hiphi-stopwatch-beta")
elseif(HIPHI_M5_TARGET STREQUAL "stackchan")
    set(_target_id 4)
    set(_slug "hiphi-kizz-beta")
endif()

idf_component_register(SRCS ${SRC_FILES}
    INCLUDE_DIRS "." "../../include" "../../common" "../../tough_app/main"
    REQUIRES mdns esp_event esp_netif esp_timer esp_wifi wpa_supplicant esp_websocket_client m5_platform
    PRIV_REQUIRES nvs_flash esp_http_client esp_http_server esp_app_format driver json)
set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TARGET_PC=0
    HIPHI_M5_TARGET_ID=${_target_id}
    HIPHI_PRODUCT_NAME=\"${_slug}\"
    HIPHI_DEVICE_SLUG=\"${_slug}\"
    HIPHI_PROVISIONING_SSID=\"${_slug}-setup\")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=stringop-truncation)
