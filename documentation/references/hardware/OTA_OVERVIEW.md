# OTA Update Implementation Notes

This document summarizes the generic requirements for enabling over-the-air (OTA) firmware updates on the Roon Knob hardware. It distills the baseline capabilities that already exist in the firmware image and outlines the additional work needed across the firmware, bridge/backend, and build pipeline to deliver a complete OTA workflow.

## Current Baseline

- The ESP32 partition table (`idf_app/partitions.csv`) already reserves a factory slot and two OTA slots (`ota_0`, `ota_1`), enabling dual-bank updates without changing the flash map.
- Project documentation acknowledges OTA as a future enhancement, confirming that no OTA control flow is wired up yet.
- The default factory + OTA partitioning leaves sufficient flash headroom for the updater logic and temporary download buffers.
- Wi-Fi event handling (see `rk_net_evt_cb` in `idf_app/main/main_idf.c`) starts the Roon client after the network comes up; this is the natural hook for triggering update checks.
- Existing HTTP utilities in `platform_http_idf.c` buffer full responses in RAM and work well for JSON payloads, but they cannot stream multi-megabyte firmware binaries.

## Firmware Changes Required

1. **Version identity** – Surface the running firmware version (e.g., via `CONFIG_APP_PROJECT_VER` or an auto-generated header) so update requests can report the current build.
2. **Update trigger** – Launch an OTA task once the device joins the network (after `RK_NET_EVT_GOT_IP`), similar to how the Roon client is started today.
3. **Streaming download** – Implement the ESP-IDF OTA flow (`esp_https_ota` or manual `esp_ota_begin` / `esp_ota_write`) that streams the image directly into the inactive OTA partition. The buffered helper in `platform_http_idf.c` should be bypassed for firmware downloads.
4. **Configuration persistence** – Extend `rk_cfg_t` and the NVS storage helpers in `platform_storage_idf.c` so OTA preferences and the last-known version can be stored and migrated when `cfg_ver` changes.
5. **User interface** – Update the LVGL settings panels (`idf_app/main/ui_network.c`) to display update state, expose a manual “Check for updates” action, and surface rollback messaging.
6. **Post-update validation** – Call `esp_ota_mark_app_valid_cancel_rollback` after a successful boot and integrate failure messaging with the existing `ui_set_message` / `ui_set_status` helpers.

## Bridge / Backend Requirements

- Add routes to the Node.js bridge for serving a manifest describing the latest firmware (version, checksum, URL) and optionally streaming the binary itself.
- Reuse the `extractKnob` metadata to log requesting device IDs, enforce channel gating, or apply staged rollouts.
- Store signed firmware images in `roon-extension/public/` or an object store and have the manifest reference them.

## Build & Distribution Pipeline

- Update the ESP-IDF build scripts to emit versioned binaries (for example, `roon-knob-vX.Y.bin`) and checksums.
- Generate the manifest automatically so the bridge’s `/firmware/latest` endpoint stays aligned with the produced artifacts.

## Security & Reliability Considerations

- Perform downloads over HTTPS and pin the expected certificates when using `esp_https_ota` to mitigate tampering.
- Verify SHA-256 hashes or signatures from the manifest before switching to the new image.
- Leverage the dual-partition layout to remain resilient to power loss and mark images valid only after the first successful boot.

## Testing Strategy

- Add simulator-level tests (`pc_sim/`) that mock the OTA manifest route to validate the logic without requiring hardware.
- For hardware validation, script end-to-end updates: publish a test manifest, monitor the download, reboot into the updated slot, and confirm rollback behaviors when validation intentionally fails.

