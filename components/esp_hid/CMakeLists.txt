# ESP-IDF 5.5.5's NimBLE HID host blocks some media remotes while enabling
# optional Battery Service notifications, before it reaches the HID report
# CCCDs. It also retains ownership of successful read buffers after returning
# them to its caller, which makes deinit free the caller-freed buffer again.
#
# Keep the upstream component pinned by the workflow, generate one patched
# translation unit, and fail configuration if the expected upstream source
# changes. All other esp_hid sources and headers remain the IDF implementation.

idf_build_get_property(target IDF_TARGET)

if(${target} STREQUAL "linux")
    return()
endif()

set(upstream_dir "${IDF_PATH}/components/esp_hid")
set(upstream_nimble_hidh "${upstream_dir}/src/nimble_hidh.c")
file(READ "${upstream_nimble_hidh}" nimble_hidh_source)

# ESP-IDF commit 85bee88b27f5 accidentally nested the HID characteristic
# handling inside the Device Information Service's unreadable-characteristic
# branch. Restore the branch structure used by release/v5.5 when the original
# Frame BLE host shipped, while retaining later bounds and lifetime fixes.
set(hid_branch_open_before
"                        }
                        continue;
                    } else {
                        if (cuuid == BLE_SVC_HID_CHR_UUID16_PROTOCOL_MODE) {")
set(hid_branch_open_after
"                        }
                    }
                    continue;
                } else {
                    if (cuuid == BLE_SVC_HID_CHR_UUID16_PROTOCOL_MODE) {")
string(FIND "${nimble_hidh_source}" "${hid_branch_open_before}"
       hid_branch_open_offset)
if(hid_branch_open_offset EQUAL -1)
    message(FATAL_ERROR
            "ESP-IDF NimBLE HID service branch opening no longer matches the pinned override")
endif()
string(REPLACE "${hid_branch_open_before}" "${hid_branch_open_after}"
       nimble_hidh_source "${nimble_hidh_source}")

set(protocol_mode_close_before
"                                }
                            }
                        }
                        continue;
                    }
                    if (cuuid == BLE_SVC_HID_CHR_UUID16_REPORT_MAP) {")
set(protocol_mode_close_after
"                                }
                            }
                            continue;
                        }
                        if (cuuid == BLE_SVC_HID_CHR_UUID16_REPORT_MAP) {")
string(FIND "${nimble_hidh_source}" "${protocol_mode_close_before}"
       protocol_mode_close_offset)
if(protocol_mode_close_offset EQUAL -1)
    message(FATAL_ERROR
            "ESP-IDF NimBLE HID protocol-mode branch no longer matches the pinned override")
endif()
string(REPLACE "${protocol_mode_close_before}" "${protocol_mode_close_after}"
       nimble_hidh_source "${nimble_hidh_source}")

set(report_map_close_before
"                                    dev->config.report_maps[hidindex].len = rlen;
                                }
                            }
                            continue;
                        } else if (cuuid == BLE_SVC_HID_CHR_UUID16_BOOT_KBD_INP")
set(report_map_close_after
"                                    dev->config.report_maps[hidindex].len = rlen;
                                }
                            }
                        }
                        continue;
                    } else if (cuuid == BLE_SVC_HID_CHR_UUID16_BOOT_KBD_INP")
string(FIND "${nimble_hidh_source}" "${report_map_close_before}"
       report_map_close_offset)
if(report_map_close_offset EQUAL -1)
    message(FATAL_ERROR
            "ESP-IDF NimBLE HID report-map branch no longer matches the pinned override")
endif()
string(REPLACE "${report_map_close_before}" "${report_map_close_after}"
       nimble_hidh_source "${nimble_hidh_source}")

set(hid_branch_close_before
"                        } else {
                            report->protocol_mode = ESP_HID_PROTOCOL_MODE_REPORT;
                            report->report_type = 0;
                            report->usage = ESP_HID_USAGE_GENERIC;
                            report->value_len = 0;
                        }
                    } else {
                        continue;
                    }
                }")
set(hid_branch_close_after
"                        } else {
                            continue;
                        }
                    }")
string(FIND "${nimble_hidh_source}" "${hid_branch_close_before}"
       hid_branch_close_offset)
if(hid_branch_close_offset EQUAL -1)
    message(FATAL_ERROR
            "ESP-IDF NimBLE HID service branch closing no longer matches the pinned override")
endif()
string(REPLACE "${hid_branch_close_before}" "${hid_branch_close_after}"
       nimble_hidh_source "${nimble_hidh_source}")

set(read_transfer_before
"        *out = s_read_data_val;
        *out_len = s_read_data_len;")
set(read_transfer_after
"        *out = s_read_data_val;
        *out_len = s_read_data_len;
        /* Ownership moved to the caller; deinit must not free it again. */
        s_read_data_val = NULL;
        s_read_data_len = 0;")
string(FIND "${nimble_hidh_source}" "${read_transfer_before}"
       first_read_transfer_offset)
if(first_read_transfer_offset EQUAL -1)
    message(FATAL_ERROR
            "ESP-IDF NimBLE HID read ownership transfer no longer matches the pinned override")
endif()
string(REPLACE "${read_transfer_before}" "${read_transfer_after}"
       nimble_hidh_source "${nimble_hidh_source}")

set(report_reset_before
"            for (uint16_t c = 0; c < ccount; c++) {
                cuuid = ble_uuid_u16(&char_result[c].uuid.u);")
set(report_reset_after
"            for (uint16_t c = 0; c < ccount; c++) {
                /* A report belongs to exactly one characteristic. Keeping the
                 * previous pointer can reinsert it and create a list cycle. */
                report = NULL;
                cuuid = ble_uuid_u16(&char_result[c].uuid.u);")
string(FIND "${nimble_hidh_source}" "${report_reset_before}"
       report_reset_offset)
if(report_reset_offset EQUAL -1)
    message(FATAL_ERROR
            "ESP-IDF NimBLE HID characteristic loop no longer matches the pinned override")
endif()
string(REPLACE "${report_reset_before}" "${report_reset_after}"
       nimble_hidh_source "${nimble_hidh_source}")

set(battery_notify_before
"        register_for_notify(dev->ble.conn_id, dev->ble.battery_handle);
        if (dev->ble.battery_ccc_handle && dev->ble.conn_id >= 0 && dev->connected) {
            write_char_descr(dev, dev->ble.battery_ccc_handle, 2, (uint8_t *)&ccc_data);
        }")
set(battery_notify_after
"        /* Battery telemetry is optional for this one-remote media profile.
         * Some remotes never answer this CCCD write, which otherwise blocks
         * esp_hidh_dev_open() before HID input reports are subscribed. */
        ESP_LOGI(TAG, \"Skipping optional Battery Service notification subscription\");")
string(FIND "${nimble_hidh_source}" "${battery_notify_before}"
       battery_notify_offset)
if(battery_notify_offset EQUAL -1)
    message(FATAL_ERROR
            "ESP-IDF NimBLE HID battery subscription block no longer matches the pinned override")
endif()
string(REPLACE "${battery_notify_before}" "${battery_notify_after}"
       nimble_hidh_source "${nimble_hidh_source}")

set(report_listener_before
"    while (report) {
        if (dev->ble.conn_id < 0 || !dev->connected) {
            break;
        }
        if ((report->permissions & BLE_GATT_CHR_PROP_NOTIFY) != 0 && report->protocol_mode == ESP_HID_PROTOCOL_MODE_REPORT) {
            register_for_notify(dev->ble.conn_id, report->handle);
            if (report->ccc_handle && dev->ble.conn_id >= 0 && dev->connected) {
                write_char_descr(dev, report->ccc_handle, 2, (uint8_t *)&ccc_data);
            }
        }
        report = report->next;
    }")
set(report_listener_after
"    uint16_t report_index = 0;
    while (report && report_index < dev->reports_len) {
        ESP_LOGI(TAG, \"HID report %u/%u: handle=%u ccc=%u permissions=0x%02x protocol=%u type=%u id=%u\",
                 (unsigned)(report_index + 1), (unsigned)dev->reports_len,
                 (unsigned)report->handle, (unsigned)report->ccc_handle,
                 (unsigned)report->permissions, (unsigned)report->protocol_mode,
                 (unsigned)report->report_type, (unsigned)report->report_id);
        if (dev->ble.conn_id < 0 || !dev->connected) {
            break;
        }
        if ((report->permissions & BLE_GATT_CHR_PROP_NOTIFY) != 0 && report->protocol_mode == ESP_HID_PROTOCOL_MODE_REPORT) {
            register_for_notify(dev->ble.conn_id, report->handle);
            if (report->ccc_handle && dev->ble.conn_id >= 0 && dev->connected) {
                write_char_descr(dev, report->ccc_handle, 2, (uint8_t *)&ccc_data);
            } else {
                ESP_LOGW(TAG, \"Not subscribing HID report handle=%u: no CCC descriptor\",
                         (unsigned)report->handle);
            }
        }
        report = report->next;
        report_index++;
    }
    if (report) {
        ESP_LOGE(TAG, \"HID report list exceeds discovered count %u; stopping traversal\",
                 (unsigned)dev->reports_len);
    }")
string(FIND "${nimble_hidh_source}" "${report_listener_before}"
       report_listener_offset)
if(report_listener_offset EQUAL -1)
    message(FATAL_ERROR
            "ESP-IDF NimBLE HID listener loop no longer matches the pinned override")
endif()
string(REPLACE "${report_listener_before}" "${report_listener_after}"
       nimble_hidh_source "${nimble_hidh_source}")

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated_dir}")
file(WRITE "${generated_dir}/nimble_hidh.c" "${nimble_hidh_source}")

set(srcs "${upstream_dir}/src/esp_hidd.c"
         "${upstream_dir}/src/esp_hidh.c"
         "${upstream_dir}/src/esp_hid_common.c")

if(CONFIG_BT_ENABLED)
    if(CONFIG_BT_BLUEDROID_ENABLED)
        list(APPEND srcs
             "${upstream_dir}/src/ble_hidd.c"
             "${upstream_dir}/src/ble_hidh.c"
             "${upstream_dir}/src/bt_hidh.c"
             "${upstream_dir}/src/bt_hidd.c")
    endif()
    if(CONFIG_BT_NIMBLE_ENABLED)
        list(APPEND srcs
             "${upstream_dir}/src/nimble_hidd.c"
             "${generated_dir}/nimble_hidh.c")
    endif()
endif()

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${upstream_dir}/include"
                       PRIV_INCLUDE_DIRS "${upstream_dir}/private"
                       REQUIRES esp_event bt)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
