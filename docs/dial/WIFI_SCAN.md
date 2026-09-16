# Wi-Fi scan behavior and quirks

Wi-Fi scans are shared controller behavior. The Tough setup screen and the
captive portal use the same asynchronous scan state machine:

`IDLE → RUNNING → READY` (or `FAILED`)

Starting a scan does not return results synchronously. The first render/request
shows **Scanning…**; refresh or wait for the next UI update before expecting
the list. A scan already in `RUNNING` is not started again.

## What the scan includes

- Active scan, with 50–120 ms dwell per channel.
- Visible networks only (`show_hidden = false`). Hidden SSIDs must be entered
  manually.
- 2.4 GHz channels only. Records whose primary channel is above 14 are
  discarded; a 5 GHz-only network will not appear.
- At most 20 unique SSIDs are retained. Duplicate BSSIDs advertising the same
  SSID collapse to one row; the first record encountered in the driver result
  order is retained.

## Provisioning-mode quirks

Provisioning runs `WIFI_MODE_APSTA`: the setup AP remains available while the
STA interface performs the scan. Keep the phone/laptop connected to the setup
SSID while waiting for results. Some clients may not automatically open the
captive portal; browse directly to `http://192.168.4.1/`.

The scan is refused while Wi-Fi is changing modes. If a scan starts during a
transition or the driver cannot read its result list, the state becomes
`FAILED`; reload the setup page to request a new scan. An empty result is a
valid `READY` result, not necessarily a radio failure.

Selecting a scanned SSID copies it into the editable SSID field. This is only a
convenience: hidden networks and names not returned by the scan remain valid
manual inputs.

## Troubleshooting checklist

1. Confirm the device is still connected to `hiphi-*-setup`.
2. Open `http://192.168.4.1/` directly.
3. Wait for the scan to finish; do not repeatedly submit or refresh during
   `RUNNING`.
4. If no 2.4 GHz networks appear, enter the SSID manually and verify the
   router is not using a hidden SSID or an unsupported channel configuration.
5. If the device repeatedly returns to setup, inspect serial logs for scan,
   association, and DHCP events before changing NVS.
