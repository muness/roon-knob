#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_WIFI_NOT_STARTED 2
#define ESP_ERR_WIFI_NOT_INIT 3
#define ESP_ERROR_CHECK(expr) ((void)(expr))

const char *esp_err_to_name(esp_err_t err);

typedef int32_t esp_event_base_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);

#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define IP_EVENT_STA_GOT_IP 1
#define ESP_EVENT_ANY_ID (-1)

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id,
                                     esp_event_handler_t handler, void *arg);
esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t id,
                                       esp_event_handler_t handler);

#define ESP_LOGI(tag, fmt, ...) ((void)(tag))
#define ESP_LOGW(tag, fmt, ...) ((void)(tag))
#define ESP_LOGE(tag, fmt, ...) ((void)(tag))

#define ESP_MAC_WIFI_STA 0
esp_err_t esp_read_mac(uint8_t *mac, int type);

typedef struct esp_netif_t esp_netif_t;
struct esp_netif_t { const char *hostname; };

esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
esp_err_t esp_netif_set_hostname(esp_netif_t *netif, const char *hostname);
esp_err_t esp_netif_get_hostname(esp_netif_t *netif, const char **hostname);

typedef uint32_t esp_ip4_addr_t;
char *esp_ip4addr_ntoa(const esp_ip4_addr_t *addr, char *buf, size_t size);

void esp_restart(void);

typedef struct fake_timer *esp_timer_handle_t;
typedef struct {
    void (*callback)(void *);
    const char *name;
} esp_timer_create_args_t;

esp_err_t esp_timer_create(const esp_timer_create_args_t *args,
                           esp_timer_handle_t *out);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);

typedef struct {
    struct {
        uint8_t ssid[33];
        uint8_t password[65];
        struct { int authmode; } threshold;
        struct { bool capable; bool required; } pmf_cfg;
    } sta;
    struct {
        uint8_t ssid[33];
        uint8_t ssid_len;
        uint8_t channel;
        uint8_t max_connection;
        int authmode;
        uint16_t beacon_interval;
    } ap;
} wifi_config_t;

typedef struct { int unused; } wifi_init_config_t;
typedef struct { uint8_t reason; } wifi_event_sta_disconnected_t;
typedef struct { struct { esp_ip4_addr_t ip; } ip_info; } ip_event_got_ip_t;

#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define WIFI_AUTH_WPA2_PSK 1
#define WIFI_AUTH_OPEN 0
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_PS_NONE 0
#define WIFI_PS_MIN_MODEM 1
#define WIFI_REASON_NO_AP_FOUND 10
#define WIFI_REASON_AUTH_FAIL 11
#define WIFI_REASON_MIC_FAILURE 12
#define WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT 13
#define WIFI_REASON_HANDSHAKE_TIMEOUT 14
#define WIFI_REASON_AUTH_EXPIRE 15
#define WIFI_REASON_ASSOC_FAIL 16
#define WIFI_REASON_ASSOC_EXPIRE 17
#define WIFI_REASON_BEACON_TIMEOUT 18
#define WIFI_REASON_ASSOC_LEAVE 19
#define WIFI_REASON_CONNECTION_FAIL 20
#define WIFI_REASON_AP_TSF_RESET 21

typedef int wifi_ps_type_t;
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type);
esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *cfg);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_set_max_tx_power(int power);

#define WIFI_IF_STA 0
#define WIFI_IF_AP 1

esp_err_t nvs_flash_erase(void);
void vTaskDelay(unsigned ticks);
#define pdMS_TO_TICKS(ms) (ms)

void fixture_reset(void);
void fixture_set_provisioning_ready(bool ready);
int fixture_provisioning_start_calls(void);
int fixture_provisioning_stop_calls(void);
int fixture_wifi_connect_calls(void);
const char *fixture_wifi_ssid(void);
const char *fixture_wifi_pass(void);
bool fixture_timer_active(const char *name);
uint64_t fixture_timer_delay_us(const char *name);
void fixture_timer_fire(const char *name);
void fixture_emit_sta_start(void);
void fixture_emit_sta_disconnected(uint8_t reason);
void fixture_block_task_delay(void);
void fixture_wait_task_delay_entered(void);
void fixture_release_task_delay(void);
uint64_t fixture_last_connect_sequence(void);
uint64_t fixture_ap_mode_sequence(void);
void fixture_block_provisioning_start(void);
void fixture_wait_provisioning_start_entered(void);
void fixture_release_provisioning_start(void);
bool fixture_provisioning_effects_overlapped(void);
