#pragma once

/**
 * Get current WiFi RSSI (signal strength).
 * @return RSSI in dBm (e.g. -45), or 0 if not connected.
 */
int platform_wifi_get_rssi(void);
