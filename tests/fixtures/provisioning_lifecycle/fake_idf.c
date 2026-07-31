#include "fake_esp_api.inc"

#include <pthread.h>
#include <string.h>

#include "controller_config.h"
#include "platform/platform_identity.h"
#include "platform/platform_provisioning.h"

struct fake_timer {
    void (*callback)(void *);
    const char *name;
    bool active;
    uint64_t delay_us;
};

static struct fake_timer s_timers[2];
static size_t s_timer_count;
static esp_netif_t s_sta_netif;
static esp_netif_t s_ap_netif;
static bool s_provisioning_ready;
static int s_provisioning_start_calls;
static int s_provisioning_stop_calls;
static int s_wifi_connect_calls;
static wifi_config_t s_last_sta_config;
static esp_event_handler_t s_wifi_handler;
static esp_event_handler_t s_ip_handler;
static pthread_mutex_t s_fixture_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_fixture_cond = PTHREAD_COND_INITIALIZER;
static bool s_block_task_delay;
static bool s_task_delay_entered;
static bool s_block_provisioning_start;
static bool s_provisioning_start_entered;
static bool s_provisioning_start_active;
static bool s_provisioning_effects_overlapped;
static uint64_t s_effect_sequence;
static uint64_t s_last_connect_sequence;
static uint64_t s_ap_mode_sequence;

void fixture_reset(void) {
    memset(s_timers, 0, sizeof(s_timers));
    s_timer_count = 0;
    s_provisioning_ready = false;
    s_provisioning_start_calls = 0;
    s_provisioning_stop_calls = 0;
    s_wifi_connect_calls = 0;
    memset(&s_last_sta_config, 0, sizeof(s_last_sta_config));
    s_wifi_handler = NULL;
    s_ip_handler = NULL;
    pthread_mutex_lock(&s_fixture_lock);
    s_block_task_delay = false;
    s_task_delay_entered = false;
    s_block_provisioning_start = false;
    s_provisioning_start_entered = false;
    s_provisioning_start_active = false;
    s_provisioning_effects_overlapped = false;
    s_effect_sequence = 0;
    s_last_connect_sequence = 0;
    s_ap_mode_sequence = 0;
    pthread_mutex_unlock(&s_fixture_lock);
}

void fixture_set_provisioning_ready(bool ready) {
    pthread_mutex_lock(&s_fixture_lock);
    s_provisioning_ready = ready;
    pthread_mutex_unlock(&s_fixture_lock);
}

int fixture_provisioning_start_calls(void) {
    return s_provisioning_start_calls;
}

int fixture_provisioning_stop_calls(void) {
    return s_provisioning_stop_calls;
}

int fixture_wifi_connect_calls(void) { return s_wifi_connect_calls; }

const char *fixture_wifi_ssid(void) {
    return (const char *)s_last_sta_config.sta.ssid;
}

const char *fixture_wifi_pass(void) {
    return (const char *)s_last_sta_config.sta.password;
}

static struct fake_timer *find_timer(const char *name) {
    for (size_t i = 0; i < s_timer_count; i++) {
        if (strcmp(s_timers[i].name, name) == 0) {
            return &s_timers[i];
        }
    }
    return NULL;
}

bool fixture_timer_active(const char *name) {
    struct fake_timer *timer = find_timer(name);
    return timer && timer->active;
}

uint64_t fixture_timer_delay_us(const char *name) {
    struct fake_timer *timer = find_timer(name);
    return timer ? timer->delay_us : 0;
}

void fixture_timer_fire(const char *name) {
    struct fake_timer *timer = find_timer(name);
    if (!timer || !timer->active) {
        return;
    }
    timer->active = false;
    timer->callback(NULL);
}

void fixture_emit_sta_start(void) {
    if (s_wifi_handler) {
        s_wifi_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_START, NULL);
    }
}

void fixture_emit_sta_disconnected(uint8_t reason) {
    if (s_wifi_handler) {
        wifi_event_sta_disconnected_t event = {.reason = reason};
        s_wifi_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event);
    }
}

void fixture_block_task_delay(void) {
    pthread_mutex_lock(&s_fixture_lock);
    s_task_delay_entered = false;
    s_block_task_delay = true;
    pthread_mutex_unlock(&s_fixture_lock);
}

void fixture_wait_task_delay_entered(void) {
    pthread_mutex_lock(&s_fixture_lock);
    while (!s_task_delay_entered) {
        pthread_cond_wait(&s_fixture_cond, &s_fixture_lock);
    }
    pthread_mutex_unlock(&s_fixture_lock);
}

void fixture_release_task_delay(void) {
    pthread_mutex_lock(&s_fixture_lock);
    s_block_task_delay = false;
    pthread_cond_broadcast(&s_fixture_cond);
    pthread_mutex_unlock(&s_fixture_lock);
}

uint64_t fixture_last_connect_sequence(void) {
    pthread_mutex_lock(&s_fixture_lock);
    const uint64_t sequence = s_last_connect_sequence;
    pthread_mutex_unlock(&s_fixture_lock);
    return sequence;
}

uint64_t fixture_ap_mode_sequence(void) {
    pthread_mutex_lock(&s_fixture_lock);
    const uint64_t sequence = s_ap_mode_sequence;
    pthread_mutex_unlock(&s_fixture_lock);
    return sequence;
}

void fixture_block_provisioning_start(void) {
    pthread_mutex_lock(&s_fixture_lock);
    s_provisioning_start_entered = false;
    s_block_provisioning_start = true;
    pthread_mutex_unlock(&s_fixture_lock);
}

void fixture_wait_provisioning_start_entered(void) {
    pthread_mutex_lock(&s_fixture_lock);
    while (!s_provisioning_start_entered) {
        pthread_cond_wait(&s_fixture_cond, &s_fixture_lock);
    }
    pthread_mutex_unlock(&s_fixture_lock);
}

void fixture_release_provisioning_start(void) {
    pthread_mutex_lock(&s_fixture_lock);
    s_block_provisioning_start = false;
    pthread_cond_broadcast(&s_fixture_cond);
    pthread_mutex_unlock(&s_fixture_lock);
}

bool fixture_provisioning_effects_overlapped(void) {
    pthread_mutex_lock(&s_fixture_lock);
    const bool overlapped = s_provisioning_effects_overlapped;
    pthread_mutex_unlock(&s_fixture_lock);
    return overlapped;
}

const char *esp_err_to_name(esp_err_t err) {
    (void)err;
    return "fake";
}

esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id,
                                     esp_event_handler_t handler, void *arg) {
    (void)id;
    (void)arg;
    if (base == WIFI_EVENT) {
        s_wifi_handler = handler;
    } else if (base == IP_EVENT) {
        s_ip_handler = handler;
    }
    return ESP_OK;
}
esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t id,
                                       esp_event_handler_t handler) {
    if (base == WIFI_EVENT) {
        s_wifi_handler = NULL;
    } else if (base == IP_EVENT) {
        s_ip_handler = NULL;
    }
    (void)id;
    (void)handler;
    return ESP_OK;
}
esp_err_t esp_read_mac(uint8_t *mac, int type) {
    (void)type;
    memset(mac, 0, 6);
    return ESP_OK;
}
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_netif_t *esp_netif_create_default_wifi_sta(void) { return &s_sta_netif; }
esp_netif_t *esp_netif_create_default_wifi_ap(void) { return &s_ap_netif; }
esp_err_t esp_netif_set_hostname(esp_netif_t *netif, const char *hostname) {
    netif->hostname = hostname;
    return ESP_OK;
}
esp_err_t esp_netif_get_hostname(esp_netif_t *netif, const char **hostname) {
    *hostname = netif->hostname;
    return ESP_OK;
}
char *esp_ip4addr_ntoa(const esp_ip4_addr_t *addr, char *buf, size_t size) {
    (void)addr;
    snprintf(buf, size, "0.0.0.0");
    return buf;
}
void esp_restart(void) {}

esp_err_t esp_timer_create(const esp_timer_create_args_t *args,
                           esp_timer_handle_t *out) {
    if (s_timer_count == sizeof(s_timers) / sizeof(s_timers[0])) {
        return ESP_ERR_INVALID_STATE;
    }
    struct fake_timer *timer = &s_timers[s_timer_count++];
    timer->callback = args->callback;
    timer->name = args->name;
    *out = timer;
    return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    if (timer) {
        timer->active = false;
    }
    return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
    timer->active = true;
    timer->delay_us = timeout_us;
    return ESP_OK;
}

esp_err_t esp_wifi_init(const wifi_init_config_t *cfg) { (void)cfg; return ESP_OK; }
esp_err_t esp_wifi_deinit(void) { return ESP_OK; }
esp_err_t esp_wifi_set_mode(int mode) {
    if (mode == WIFI_MODE_AP) {
        pthread_mutex_lock(&s_fixture_lock);
        s_ap_mode_sequence = ++s_effect_sequence;
        pthread_mutex_unlock(&s_fixture_lock);
    }
    return ESP_OK;
}
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type) { (void)type; return ESP_OK; }
esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *cfg) {
    if (interface == WIFI_IF_STA) {
        s_last_sta_config = *cfg;
    }
    return ESP_OK;
}
esp_err_t esp_wifi_start(void) { return ESP_OK; }
esp_err_t esp_wifi_stop(void) { return ESP_OK; }
esp_err_t esp_wifi_connect(void) {
    pthread_mutex_lock(&s_fixture_lock);
    s_last_connect_sequence = ++s_effect_sequence;
    pthread_mutex_unlock(&s_fixture_lock);
    s_wifi_connect_calls++;
    return ESP_OK;
}
esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }
esp_err_t esp_wifi_set_max_tx_power(int power) { (void)power; return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }
void vTaskDelay(unsigned ticks) {
    (void)ticks;
    pthread_mutex_lock(&s_fixture_lock);
    if (s_block_task_delay) {
        s_task_delay_entered = true;
        pthread_cond_broadcast(&s_fixture_cond);
        while (s_block_task_delay) {
            pthread_cond_wait(&s_fixture_cond, &s_fixture_lock);
        }
    }
    pthread_mutex_unlock(&s_fixture_lock);
}

const char *platform_device_slug(void) { return "fixture"; }
const char *platform_provisioning_ssid(void) { return "Fixture setup"; }
bool platform_provisioning_start(void) {
    s_provisioning_start_calls++;
    pthread_mutex_lock(&s_fixture_lock);
    s_provisioning_start_active = true;
    if (s_block_provisioning_start) {
        s_provisioning_start_entered = true;
        pthread_cond_broadcast(&s_fixture_cond);
        while (s_block_provisioning_start) {
            pthread_cond_wait(&s_fixture_cond, &s_fixture_lock);
        }
    }
    s_provisioning_start_active = false;
    const bool ready = s_provisioning_ready;
    pthread_mutex_unlock(&s_fixture_lock);
    return ready;
}
void platform_provisioning_stop(void) {
    pthread_mutex_lock(&s_fixture_lock);
    if (s_provisioning_start_active) {
        s_provisioning_effects_overlapped = true;
    }
    pthread_mutex_unlock(&s_fixture_lock);
    s_provisioning_stop_calls++;
}
bool controller_config_snapshot(controller_config_snapshot_t *out) {
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->durability = CONTROLLER_CONFIG_DURABILITY_DURABLE;
    return true;
}
bool controller_config_wifi_snapshot(controller_config_wifi_snapshot_t *out) {
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    return true;
}
