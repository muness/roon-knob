#!/usr/bin/env python3
"""Guard shared power snapshots and Wi-Fi scan coverage across every target."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

REQUIRED: dict[str, tuple[str, ...]] = {
    "common/platform/platform_power.h": (
        "platform_power_snapshot_t",
        "void platform_power_snapshot(platform_power_snapshot_t *out)",
        "platform_power_diagnostics_t",
        "platform_power_diagnostics_snapshot",
        "platform_power_debug_arm_sleep",
        "platform_power_prepare_for_deep_sleep",
        "platform_power_evidence_note_entry",
        "PLATFORM_POWER_CAP_VOLTAGE_EXPERIMENT",
        "PLATFORM_POWER_EXPERIMENT_MAX_SAMPLES 24",
        "platform_power_experiment_arm",
        "platform_power_experiment_note_early_boot",
        "last_boot_followed_terminal_entry",
        "PLATFORM_POWER_TRACE_MAX_EVENTS 8",
        "platform_power_trace_event_t trace_events",
        "int64_t unix_time_ms",
    ),
    "common/platform/platform_power.c": (
        "controller_config_snapshot(&config)",
        "platform_power_diagnostics_enrich(out)",
        "esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)",
        "pm_config.light_sleep_enable",
        'POWER_EVIDENCE_NAMESPACE = "pwr_evidence"',
        'POWER_EVIDENCE_KEY = "journal_v4"',
        'POWER_EVIDENCE_V3_KEY = "journal_v3"',
        "record.last_boot_followed_entry = record.entry_pending",
        "power_evidence_append(&record, PLATFORM_POWER_TRACE_BOOT",
        "platform_power_evidence_note_boot()",
        'POWER_EXPERIMENT_NAMESPACE = "pwr_experiment"',
        'POWER_EXPERIMENT_KEY = "experiment_v1"',
        "record.observer_effect = interval_sec > 0",
        "wake == ESP_SLEEP_WAKEUP_TIMER && record.interval_sec > 0",
        "power_experiment_save(&record)",
    ),
    "common/app_main.c": (
        "platform_power_evidence_note_boot()",
    ),
    "common/power_debug_web.c": (
        '"/power-debug"',
        '"/power-debug/sleep"',
        '"schema_version\\\":3',
        '"timestamp_utc\\\":%s%s%s',
        '"last_boot_followed_entry\\\"',
        '"events\\\":%s',
        '"sampling_mode\\\":\\\"%s',
        '"software-checkpoint" : "external-profiler"',
        'return "none"',
        '"power_experiment_supported\\\":%s',
        "parse_u32(value, &interval_sec)",
        "parse_u32(value, &duration_sec)",
        "interval_sec = POWER_EXPERIMENT_DEFAULT_INTERVAL_SEC",
        '"samples_truncated\\\":%s',
        "platform_power_experiment_clear()",
        'strcmp(format, "csv") == 0',
        "Persistent event tail",
        "power_debug_web_register",
    ),
    "common/bridge_client.c": (
        '"platform/platform_power.h"',
        "fetch_now_playing(&state, &power)",
        "wait_for_poll_interval(&power)",
        "check_charging_state_change(power.external_power)",
    ),
    "common/wifi_manager.c": (
        "esp_netif_sntp_init(&config)",
        "platform_power_evidence_note_time_sync",
    ),
    "common/ui.c": (
        '"platform/platform_power.h"',
        "platform_power_snapshot(&power)",
    ),
    "idf_app/main/platform_display_idf.c": (
        "void platform_power_snapshot",
        "battery_get_percentage()",
        "battery_is_charging()",
        "PLATFORM_POWER_CAP_VOLTAGE_EXPERIMENT",
        "platform_power_experiment_supported",
    ),
    "frame_app/main/platform_display_frame.c": (
        "void platform_power_snapshot",
        "pmic_get_battery_percent()",
        "PMIC_POWER_SOURCE_EXTERNAL",
        "platform_power_diagnostics_enrich",
    ),
    "rlcd_app/main/platform_display_rlcd.c": (
        "void platform_power_snapshot",
        "rlcd_battery_percent()",
        "platform_power_diagnostics_enrich",
        "rlcd_power_manager_debug_enrich",
        "platform_power_debug_arm_sleep",
    ),
    "atom_app/main/platform_display_atom.c": (
        "void platform_power_snapshot",
        "platform_power_diagnostics_enrich",
    ),
    "tough_app/main/platform_display_m5.c": (
        "void platform_power_snapshot",
        "platform_power_diagnostics_enrich",
    ),
    "m5_beta_app/main/platform_display_beta.c": (
        "void platform_power_snapshot",
        "m5_platform_power_snapshot(&snapshot)",
        "platform_power_diagnostics_enrich",
    ),
    "common/m5_terminal_power.c": (
        "platform_power_prepare_for_deep_sleep()",
        "platform_input_shutdown()",
        "wifi_mgr_stop()",
        "m5_platform_power_off()",
        "nvs_commit(handle)",
        "platform_power_evidence_note_preflight(flags)",
        "platform_power_evidence_note_entry(m5_platform_battery_level())",
    ),
    "components/m5_platform/m5_platform.cpp": (
        "POWER_SNAPSHOT_CACHE_US",
        "getBattery1Voltage()",
        "getBattery2Voltage()",
        "M5_PLATFORM_BOARD_DIAL",
    ),
    "rlcd_app/main/rlcd_battery.c": (
        "RLCD_BATTERY_CHANNEL ADC_CHANNEL_3",
        "RLCD_BATTERY_CACHE_US",
    ),
    "idf_app/main/battery.c": (
        "BATTERY_SAMPLE_CACHE_MS 15000",
        "s_cached_voltage_valid",
        "s_battery_mutex",
        "battery_get_measurement",
        "s_cached_raw_adc",
        "s_cached_adc_mv",
    ),
    "idf_app/main/main_idf.c": (
        "resume_power_experiment_before_ui();",
        "platform_power_experiment_note_early_boot(",
        "Power experiment checkpoint saved",
        "without UI, Wi-Fi, or BLE",
    ),
    "common/wifi_manager.c": (
        "esp_wifi_scan_start(&config, false)",
        "WIFI_MODE_APSTA",
    ),
    "common/wifi_portal_form.c": (
        "rk_wifi_portal_scan_prepare",
        "wifi_mgr_scan_start()",
        "rk_wifi_portal_render_options",
        "rk_wifi_portal_resolve_ssid",
    ),
    "common/wifi_portal_form.h": (
        "RK_WIFI_PORTAL_SELECT_OPEN",
        "RK_WIFI_PORTAL_SELECT_CLOSE",
        "RK_WIFI_PORTAL_AUTO_REFRESH_SCRIPT",
    ),
    "idf_app/main/captive_portal.c": (
        '"wifi_portal_form.h"',
        "rk_wifi_portal_scan_prepare",
        "rk_wifi_portal_render_options",
        "rk_wifi_portal_resolve_ssid",
    ),
    "idf_app/main/config_server.c": (
        '"wifi_portal_form.h"',
        "rk_wifi_portal_scan_prepare",
        "rk_wifi_portal_render_options",
        "rk_wifi_portal_resolve_ssid",
        "power_debug_web_register(s_server)",
    ),
    "idf_app/main/display_sleep.c": (
        "RTC_DATA_ATTR static power_debug_rtc_t",
        "s_power_debug_rtc.preflight_completions++",
        "s_power_debug_rtc.deep_sleep_entries++",
        "s_power_debug_rtc.encoder_wakes++",
        "platform_power_prepare_for_deep_sleep()",
        "LCD_CMD_SLEEP_IN",
        "LCD_CMD_SLEEP_OUT",
        "TOUCH_RESET_GPIO",
        "platform_power_evidence_note_preflight(durable_flags)",
        "platform_power_experiment_note_entry(",
        "esp_sleep_enable_timer_wakeup(",
        "must not silently cancel it",
        "display_get_state() != DISPLAY_STATE_SLEEP",
    ),
    "frame_app/main/captive_portal.c": (
        '"wifi_portal_form.h"',
        "#define PLATFORM_PORTAL_FRAME_DISPLAY_SETTINGS 1",
        "#if PLATFORM_PORTAL_FRAME_DISPLAY_SETTINGS",
        "rk_wifi_portal_scan_prepare",
        "rk_wifi_portal_render_options",
        "rk_wifi_portal_resolve_ssid",
        "power_debug_web_register(s_server)",
        "#define PORTAL_HTTPD_STACK_SIZE 16384",
        "config.max_uri_handlers = 20",
        "config.stack_size = PORTAL_HTTPD_STACK_SIZE;",
        "httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN)",
        "Portal stack headroom at request start",
        "Portal stack headroom after render",
        'name=\'show_ip\'',
        "frame_display_preferences_set_show_ip(show_ip)",
    ),
    "frame_app/main/eink_ui.c": (
        '"controller_utf8.h"',
        "controller_utf8_decode_next(&cursor)",
        "ART_CROP_LIMIT_PERCENT 10",
        "ART_CACHE_SIZE ((ART_W * ART_H) / 2)",
        '"eink_acep6", "smart"',
        "Unexpected packed artwork size",
        "Packed artwork ready for panel",
        "New artwork unavailable; preserving the current panel",
        "if (artwork_changed && now < s_ui.art_retry_after)",
        "#define ART_RETRY_MAX_MS 60000",
        "s_ui.art_retry_after = platform_millis() + s_ui.art_retry_delay",
        "s_ui.art_retry_delay = next_delay > ART_RETRY_MAX_MS",
        "const bool urgent_refresh = artwork_changed || s_ui.display_pref_dirty",
        "New artwork bypassing the general render cooldown",
        "if (!urgent_refresh && s_ui.initial_draw_done",
        "s_ui.show_ip && s_ui.device_ip[0]",
        "void eink_ui_post_device_ip(const char *ip)",
    ),
    "frame_app/main/main_frame.c": (
        "eink_ui_post_device_ip(ip_opt)",
        "eink_ui_post_show_ip(frame_display_preferences_show_ip())",
        "eink_ui_post_device_ip(NULL)",
    ),
    "frame_app/main/platform_http_frame.c": (
        "max_image_size = 2 * 1024 * 1024",
        "content_length + 4096",
        "next_size = max_image_size",
    ),
    "frame_app/main/frame_display_preferences.c": (
        'NAMESPACE = "rk_frame"',
        'SHOW_IP_KEY = "show_ip"',
        "ATOMIC_VAR_INIT(true)",
        "nvs_commit(handle)",
    ),
    "frame_app/main/frame_power_manager.c": (
        "RTC_DATA_ATTR static frame_power_debug_rtc_t",
        "frame_power_manager_debug_arm",
        "s_power_debug_rtc.preflight_completions++",
        "s_power_debug_rtc.entries++",
        "platform_power_prepare_for_deep_sleep()",
        ".provisioning = wifi_mgr_is_ap_mode()",
        "pmic_prepare_for_deep_sleep()",
        "platform_power_evidence_note_preflight(",
        "platform_power_evidence_note_entry(pmic_get_battery_percent())",
    ),
    "rlcd_app/main/rlcd_power_manager.c": (
        "RTC_DATA_ATTR static rlcd_power_debug_rtc_t",
        "platform_power_prepare_for_deep_sleep()",
        "esp_sleep_enable_ext1_wakeup_io(1ULL << RLCD_WAKE_GPIO",
        "ble_hid_host_rlcd_prepare_for_sleep()",
        "rlcd_display_prepare_for_sleep()",
        "s_debug.preflight_completions++",
        "s_debug.entries++",
        "platform_power_evidence_note_preflight(s_debug.last_preflight_flags)",
        "platform_power_evidence_note_entry(rlcd_battery_percent())",
    ),
    "rlcd_app/main/rlcd_display.c": (
        "ST7305 SLPIN",
        "esp_lcd_panel_io_tx_param(s_io, 0x10",
    ),
    "tough_app/main/captive_portal.c": (
        '"wifi_portal_form.h"',
        "rk_wifi_portal_scan_prepare",
        "rk_wifi_portal_render_options",
        "rk_wifi_portal_resolve_ssid",
        "power_debug_web_register(s_server)",
    ),
    "atom_app/main/touch_ui.cpp": ("wifi_mgr_scan_start()",),
    "tough_app/main/touch_ui.cpp": ("wifi_mgr_scan_start()",),
    "m5_beta_app/main/touch_ui.cpp": ("wifi_mgr_scan_start()",),
    "rlcd_app/main/CMakeLists.txt": (
        '"../../frame_app/main/captive_portal.c"',
        '"../../common/power_debug_web.c"',
        '"../../common/wifi_portal_form.c"',
        '"../../common/controller_text_ascii.c"',
        "PLATFORM_PORTAL_FRAME_DISPLAY_SETTINGS=0",
    ),
    "idf_app/main/CMakeLists.txt": (
        '"../../common/power_debug_web.c"',
        '"../../common/wifi_portal_form.c"',
    ),
    "frame_app/main/CMakeLists.txt": (
        '"../../common/power_debug_web.c"',
        '"../../common/wifi_portal_form.c"',
        '"../../common/controller_utf8.c"',
        '"frame_display_preferences.c"',
    ),
    "tough_app/main/CMakeLists.txt": (
        '"../../common/power_debug_web.c"',
        '"../../common/wifi_portal_form.c"',
    ),
    "atom_app/main/CMakeLists.txt": (
        '"../../common/power_debug_web.c"',
        '"../../common/wifi_portal_form.c"',
    ),
    "m5_beta_app/main/CMakeLists.txt": (
        '"../../common/power_debug_web.c"',
        '"../../common/wifi_portal_form.c"',
    ),
    ".github/RELEASE_TEMPLATE.md": (
        "All nine physical controllers now scan for nearby 2.4 GHz",
        "one shared power snapshot now supplies",
        "Every physical controller exposes `/power-debug`",
        "The Dial's `/power-debug/sleep` endpoint now runs one persistent power",
        "All nine controllers retain the terminal-power evidence",
        "could therefore\nkeep the processor awake indefinitely",
        "HiPhi RLCD now sleeps the panel and processor",
    ),
}

FORBIDDEN: dict[str, tuple[str, ...]] = {
    "common/platform/platform_display.h": ("platform_battery_",),
    "common/bridge_client.c": (
        "platform_battery_get_level()",
        "platform_battery_is_charging()",
    ),
    "common/ui.c": (
        "battery_get_percentage()",
        "battery_is_charging()",
    ),
    "idf_app/main/main_idf.c": (
        "wifi_mgr_start();\n    resume_power_experiment_before_ui();",
        "platform_display_init();\n    resume_power_experiment_before_ui();",
    ),
    "idf_app/main/config_server.c": ("display_power_debug_snapshot",),
    ".github/RELEASE_TEMPLATE.md": (
        "HiPhi Dial captive page still uses manual SSID entry",
    ),
    "frame_app/main/frame_power_manager.c": (
        "wifi_mgr_is_ap_mode() || captive_portal_is_running()",
        "esp_sleep_enable_timer_wakeup",
    ),
    "tough_app/main/platform_display_m5.c": (
        "out->external_power = true",
        "USB-powered appliance",
    ),
    "rlcd_app/main/rlcd_power_manager.c": (
        "!captive_portal_is_running()",
    ),
}


def main() -> int:
    failures: list[str] = []
    for relative, needles in REQUIRED.items():
        path = ROOT / relative
        if not path.is_file():
            failures.append(f"{relative}: required file is missing")
            continue
        text = path.read_text(encoding="utf-8")
        for needle in needles:
            if needle not in text:
                failures.append(f"{relative}: missing contract {needle!r}")

    for relative, needles in FORBIDDEN.items():
        text = (ROOT / relative).read_text(encoding="utf-8")
        for needle in needles:
            if needle in text:
                failures.append(f"{relative}: forbidden legacy path {needle!r}")

    frame_portal = (ROOT / "frame_app/main/captive_portal.c").read_text(
        encoding="utf-8"
    )
    root_handler = frame_portal.split(
        "static esp_err_t root_get_handler(httpd_req_t *req) {", 1
    )[-1].split(
        "static esp_err_t configure_post_handler(httpd_req_t *req) {", 1
    )[0]
    for legacy_render in (
        "snprintf(html, html_size,",
        "heap_caps_malloc(html_size",
        "httpd_resp_send(req, html, strlen(html))",
    ):
        if legacy_render in root_handler:
            failures.append(
                "frame_app/main/captive_portal.c: root setup page must stream "
                f"instead of using legacy monolithic render {legacy_render!r}"
            )

    if failures:
        print("Platform power/Wi-Fi contract check FAILED", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print("Platform power/Wi-Fi contract check passed")
    print("- one power snapshot feeds each shared controller poll cycle")
    print("- every physical target exposes the shared non-blocking Wi-Fi scan")
    print("- every physical target exposes the shared power-debug schema")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
