#include "platform/platform_wifi.h"

#include <esp_wifi.h>

int platform_wifi_get_rssi(void) {
  wifi_ap_record_t info;
  if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
    return info.rssi;
  }
  return 0;
}
