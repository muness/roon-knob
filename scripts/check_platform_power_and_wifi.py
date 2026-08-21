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
        "last_boot_followed_terminal_entry",
        "PLATFORM_POWER_TRACE_MAX_EVENTS 8",
        "platform_power_trace_event_t trace_events",
    ),
    "common/platform/platform_power.c": (
        "controller_config_snapshot(&config)",
        "platform_power_diagnostics_enrich(out)",
        "esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)",
        "pm_config.light_sleep_enable",
        'POWER_EVIDENCE_NAMESPACE = "pwr_evidence"',
        'POWER_EVIDENCE_KEY = "journal_v3"',
        "record.last_boot_followed_entry = record.entry_pending",
        "power_evidence_append(&record, PLATFORM_POWER_TRACE_BOOT",
        "platform_power_evidence_note_boot()",
    ),
    "common/app_main.c": (
        "platform_power_evidence_note_boot()",
    ),
    "common/power_debug_web.c": (
        '"/power-debug"',
        '"/power-debug/sleep"',
        '"schema_version\\\":2',
        '"last_boot_followed_entry\\\"',
        '"events\\\":%s',
        "Persistent event tail",
        "power_debug_web_register",
    ),
    "common/bridge_client.c": (
        '"platform/platform_power.h"',
        "fetch_now_playing(&state, &power)",
        "wait_for_poll_interval(&power)",
        "check_charging_state_change(power.external_power)",
    ),
    "common/ui.c": (
        '"platform/platform_power.h"',
        "platform_power_snapshot(&power)",
    ),
    "idf_app/main/platform_display_idf.c": (
        "void platform_power_snapshot",
        "battery_get_percentage()",
        "battery_is_charging()",
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
    ),
    "common/wifi_manager.c": (
        "esp_wifi_scan_start(&config, false)",
        "WIFI_MODE_APSTA",
    ),
    "idf_app/main/captive_portal.c": (
        "Nearby 2.4 GHz networks",
        "wifi_mgr_scan_start()",
        "this list will refresh automatically",
    ),
    "idf_app/main/config_server.c": (
        "Nearby 2.4 GHz networks",
        "wifi_mgr_scan_start()",
        "Scanning&hellip; this list will refresh ",
        "automatically.</p>",
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
    ),
    "frame_app/main/captive_portal.c": (
        "Nearby 2.4 GHz networks",
        "wifi_mgr_scan_start()",
        "power_debug_web_register(s_server)",
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
        "wifi_mgr_scan_start()",
        "No nearby networks found",
        "power_debug_web_register(s_server)",
    ),
    "atom_app/main/touch_ui.cpp": ("wifi_mgr_scan_start()",),
    "tough_app/main/touch_ui.cpp": ("wifi_mgr_scan_start()",),
    "m5_beta_app/main/touch_ui.cpp": ("wifi_mgr_scan_start()",),
    "rlcd_app/main/CMakeLists.txt": (
        '"../../frame_app/main/captive_portal.c"',
        '"../../common/power_debug_web.c"',
    ),
    "idf_app/main/CMakeLists.txt": ('"../../common/power_debug_web.c"',),
    "frame_app/main/CMakeLists.txt": ('"../../common/power_debug_web.c"',),
    "tough_app/main/CMakeLists.txt": ('"../../common/power_debug_web.c"',),
    "atom_app/main/CMakeLists.txt": ('"../../common/power_debug_web.c"',),
    "m5_beta_app/main/CMakeLists.txt": (
        '"../../common/power_debug_web.c"',
    ),
    ".github/RELEASE_TEMPLATE.md": (
        "All nine physical controllers now scan for nearby 2.4 GHz",
        "one shared power snapshot now supplies",
        "Every physical controller exposes `/power-debug`",
        "All nine controllers provide the one-time 15-second powered terminal-power",
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
