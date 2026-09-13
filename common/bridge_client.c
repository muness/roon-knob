#include "bridge_client.h"

#include "bridge_command_plan.h"
#include "controller_config.h"
#include "platform/platform_display.h"
#include "platform/platform_http.h"
#include "platform/platform_log.h"
#include "platform/platform_mdns.h"
#include "platform/platform_mdns_endpoint.h"
#include "platform/platform_power.h"
#include "platform/platform_task.h"
#include "platform/platform_time.h"
#include "os_mutex.h"
#include "controller_presentation.h"
#include "controller_view.h"
#include "controller_view_compat.h"

#include <ctype.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdatomic.h>
#include <cJSON.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

// Forward declarations for config handling
static bool fetch_knob_config(void);
static void apply_knob_config(const rk_cfg_t *cfg);
static void check_config_sha(const char *new_sha);
static void check_zones_sha(const char *new_sha);
static void check_charging_state_change(bool current_external_power);

#define MAX_LINE 128
#define MAX_ZONE_NAME 64
#define MAX_ZONES BRIDGE_CLIENT_MAX_ZONES
#define POLL_DELAY_AWAKE_CHARGING_MS 2000   // 2 seconds when charging and display on
#define POLL_DELAY_AWAKE_BATTERY_MS 5000   // 5 seconds on battery to save power
#define POLL_DELAY_SLEEPING_MS 30000       // 30 seconds when display is sleeping
#define POLL_DELAY_SLEEPING_STOPPED_MS 60000  // 60 seconds when sleeping AND zone stopped
#define POLL_DELAY_BRIDGE_ERROR_MS 10000   // 10 seconds when bridge unreachable

/* Bridge responses and queued presentation snapshots are transient and can be
 * relatively numerous. Keep them out of the small internal heap shared by the
 * radio controller and display DMA. free() is valid for ESP heap-cap blocks. */
static void *bridge_external_alloc(size_t size) {
#ifdef ESP_PLATFORM
    // PSRAM is optional across targets (the original AtomS3 has none).
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return ptr ? ptr : heap_caps_malloc(size, MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

static char *bridge_external_strdup(const char *value) {
    if (!value) {
        return NULL;
    }
    size_t size = strlen(value) + 1;
    char *copy = bridge_external_alloc(size);
    if (copy) {
        memcpy(copy, value, size);
    }
    return copy;
}

static void bridge_json_allocator_init(void) {
    static bool initialized = false;
    if (initialized) {
        return;
    }

    /* cJSON hooks are process-global. Install them once before the bridge
     * worker starts, and never switch them while JSON trees may be live. */
    cJSON_Hooks hooks = {
        .malloc_fn = bridge_external_alloc,
        .free_fn = free,
    };
    cJSON_InitHooks(&hooks);
    initialized = true;
}

struct now_playing_state {
    char line1[MAX_LINE];
    char line2[MAX_LINE];
    char line3[MAX_LINE];
    bool is_playing;
    float volume;
    float volume_min;
    float volume_max;
    float volume_step;
    int seek_position;
    int length;
    char image_key[128];  // For tracking album artwork changes
    char config_sha[9];   // Config SHA for change detection
    char zones_sha[9];    // Zones SHA for zone list change detection
};

struct bridge_state {
    bridge_zone_t zones[MAX_ZONES];
    int zone_count;
    char zone_label[MAX_ZONE_NAME];
    /*
     * The selected zone is runtime state, not a second persisted config
     * cache.  It is pinned only when an unverified zone transaction must not
     * displace the currently-operating zone.
     */
    char runtime_zone_id[sizeof(((rk_cfg_t *)0)->zone_id)];
    bool runtime_zone_pinned;
};

static struct bridge_state s_state;
static controller_connection_t s_connection;
static os_mutex_t s_state_lock = OS_MUTEX_INITIALIZER;
static atomic_bool s_running = ATOMIC_VAR_INIT(false);
static atomic_uint s_worker_start_attempts = ATOMIC_VAR_INIT(0);
static bool s_trigger_poll;
static atomic_bool s_network_ready = ATOMIC_VAR_INIT(false);
static bool s_force_artwork_refresh;  // Force artwork reload on zone change
static float s_last_known_volume = 0.0f;   // Cached volume for optimistic UI updates
static float s_last_known_volume_min = -80.0f;  // Cached volume min for clamping
static float s_last_known_volume_max = 0.0f;    // Cached volume max for clamping
static float s_last_known_volume_step = 1.0f;  // Cached volume step
static uint32_t s_artwork_generation;
static bool s_last_charging_state = true;  // Track charging state for config reapply
static bool s_last_is_playing = false;     // Track play state for extended sleep polling
static char s_last_zones_sha[9] = {0};     // Track zones SHA for zone list change detection

// Bridge connection retry tracking (mirrors WiFi retry pattern)
#define BRIDGE_POLL_TASK_STACK_SIZE 16384
#define BRIDGE_POLL_TASK_START_ATTEMPTS 2
_Static_assert(BRIDGE_POLL_TASK_STACK_SIZE >= 16384,
               "bridge worker stack budget must cover response parsing");

void bridge_client_request_poll(void) {
    s_trigger_poll = true;
}

/* The network worker has a PSRAM stack, so it must never enter NVS/flash
 * persistence. A single static mailbox moves discovered-endpoint commits onto
 * the internal UI stack without creating another heap allocation. */
typedef struct {
    controller_config_endpoint_token_t token;
    char discovered[sizeof(((rk_cfg_t *)0)->bridge_base)];
} discovered_endpoint_commit_t;
static discovered_endpoint_commit_t s_discovered_endpoint_commit;
static atomic_bool s_discovered_endpoint_commit_pending = ATOMIC_VAR_INIT(false);

// Fallback bridge URL when mDNS discovery fails and no bridge is stored.
#ifndef CONFIG_RK_DEFAULT_BRIDGE_BASE
#define CONFIG_RK_DEFAULT_BRIDGE_BASE "http://127.0.0.1:8088"
#endif

static void strip_trailing_slashes(char *url);
static void bridge_poll_thread(void *arg);
static void post_ui_connectivity_update(const char *line1, const char *line2);
static void post_ui_network_status(const char *status);

static bool start_bridge_poll_task(void) {
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
            &s_running, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        return true;
    }

    unsigned attempt = atomic_fetch_add_explicit(
                           &s_worker_start_attempts, 1,
                           memory_order_relaxed) + 1;
    if (attempt > BRIDGE_POLL_TASK_START_ATTEMPTS) {
        atomic_store_explicit(&s_running, false, memory_order_release);
        return false;
    }

    size_t heap_before = platform_task_internal_heap_free_bytes();
    size_t largest_before =
        platform_task_internal_heap_largest_free_block_bytes();
    if (heap_before != SIZE_MAX && largest_before != SIZE_MAX) {
        LOGI("Bridge worker internal heap before start: free=%zu largest=%zu",
             heap_before, largest_before);
    }
    /* The poller can persist a discovered or selected zone.  NVS temporarily
     * disables the external-memory cache, so this task must have an internal
     * RAM stack; the external-stack API is explicitly only for non-persisting
     * background work. */
    if (platform_task_start_configured("bridge_poll",
                                       BRIDGE_POLL_TASK_STACK_SIZE,
                                       bridge_poll_thread, NULL) == 0) {
        size_t heap_after = platform_task_internal_heap_free_bytes();
        size_t largest_after =
            platform_task_internal_heap_largest_free_block_bytes();
        if (heap_after != SIZE_MAX && largest_after != SIZE_MAX) {
            LOGI("Bridge worker internal heap after start: free=%zu largest=%zu",
                 heap_after, largest_after);
        }
        return true;
    }
    atomic_store_explicit(&s_running, false, memory_order_release);
    LOGE("Could not start Unified Hi-Fi Control polling task (attempt %u/%u)",
         attempt, BRIDGE_POLL_TASK_START_ATTEMPTS);
    if (attempt < BRIDGE_POLL_TASK_START_ATTEMPTS) {
        post_ui_network_status("Hi-Fi Control startup delayed");
    } else {
        post_ui_connectivity_update("Restart device",
                                    "Hi-Fi Control unavailable");
        post_ui_network_status("Hi-Fi Control unavailable - restart device");
    }
    return false;
}

static void lock_state(void) {
    os_mutex_lock(&s_state_lock);
}

static void unlock_state(void) {
    os_mutex_unlock(&s_state_lock);
}

static void pin_runtime_zone_selection(const char *zone_id) {
    lock_state();
    rk_strlcpy(s_state.runtime_zone_id, zone_id ? zone_id : "",
               sizeof(s_state.runtime_zone_id));
    s_state.runtime_zone_pinned = true;
    unlock_state();
}

static bool bridge_config_snapshot(rk_cfg_t *out) {
    controller_config_snapshot_t snapshot;
    if (!out || !controller_config_snapshot(&snapshot)) {
        return false;
    }
    *out = snapshot.value;
    return true;
}

static bool bridge_endpoint_snapshot(char *bridge_base, size_t bridge_len,
                                     char *zone_id, size_t zone_len) {
    rk_cfg_t cfg;
    if (!bridge_base || bridge_len == 0 || !zone_id || zone_len == 0 ||
        !bridge_config_snapshot(&cfg)) {
        return false;
    }
    rk_strlcpy(bridge_base, cfg.bridge_base, bridge_len);
    rk_strlcpy(zone_id, cfg.zone_id, zone_len);
    lock_state();
    if (strcmp(s_connection.selected, cfg.bridge_base) == 0 && s_connection.resolved) {
        rk_strlcpy(bridge_base, s_connection.endpoint, bridge_len);
    }
    if (s_state.runtime_zone_pinned) {
        rk_strlcpy(zone_id, s_state.runtime_zone_id, zone_len);
    }
    unlock_state();
    if (bridge_base[0] == '\0' && CONFIG_RK_DEFAULT_BRIDGE_BASE[0] != '\0') {
        rk_strlcpy(bridge_base, CONFIG_RK_DEFAULT_BRIDGE_BASE, bridge_len);
        strip_trailing_slashes(bridge_base);
    }
    return true;
}

static bool fetch_now_playing(struct now_playing_state *state,
                              const platform_power_snapshot_t *power);
static bool refresh_zone_label(bool prefer_zone_id);
static void parse_zones_from_response(const char *resp);
static const char *extract_json_string(const char *start, const char *key, char *out, size_t len);
static bool send_control_json(const char *json);
static void default_now_playing(struct now_playing_state *state);
static void wait_for_poll_interval(const platform_power_snapshot_t *power);
static void bridge_poll_thread(void *arg);
static bool update_connection(void);
static void commit_discovered_endpoint_on_ui(void *arg);
static void post_ui_update(const struct now_playing_state *state);
static void post_ui_status(bool online);
static void post_ui_zone_name(const char *name);
static void post_ui_message(const char *msg);
static void post_ui_message_copy(char *msg_copy);
static void strip_trailing_slashes(char *url);
static void post_ui_status_copy(bool *status_copy);
static void post_ui_zone_name_copy(char *name_copy);

static void ui_update_cb(void *arg) {
    controller_media_view_t *view = arg;
    if (!view) {
        LOGI("ui_update_cb: view is NULL!");
        return;
    }

    // Cache volume for optimistic UI updates
    lock_state();
    s_last_known_volume = view->volume;
    s_last_known_volume_min = view->volume_min;
    s_last_known_volume_max = view->volume_max;
    s_last_known_volume_step = view->volume_step;
    unlock_state();

    controller_view_compat_apply_media(view);
    free(view);
}

static void ui_status_cb(void *arg) {
    bool *online = arg;
    if (!online) {
        return;
    }
    controller_presentation_set_status(*online);
    free(online);
}

static void ui_message_cb(void *arg) {
    char *msg = arg;
    if (!msg) {
        return;
    }
    controller_presentation_set_message(msg);
    free(msg);
}

static void ui_network_status_cb(void *arg) {
    char *status = arg;
    if (!status) {
        return;
    }
    controller_presentation_set_network_status(status);
    free(status);
}

static void post_ui_network_status(const char *status) {
    /* A normal reconnect clears its transient banner.  Do not let it erase
     * the durability warning raised by a committed-but-unverified update. */
    controller_config_snapshot_t config;
    bool keep_durability_warning =
        controller_config_snapshot(&config) &&
        config.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT;
    if ((!status || status[0] == '\0') && keep_durability_warning) {
        status = "Settings saved but could not be verified";
    }
    char *copy = bridge_external_strdup(status ? status : "");
    if (!copy) {
        return;
    }
    if (!platform_task_post_to_ui(ui_network_status_cb, copy)) {
        free(copy);
    }
}

static void post_unverified_config_diagnostic(const char *operation) {
    LOGW("%s committed but could not be verified", operation);
    /* This uses the existing persistent network-status presentation path. */
    post_ui_network_status("Settings saved but could not be verified");
}

static void commit_discovered_endpoint_on_ui(void *arg) {
    discovered_endpoint_commit_t *commit = arg;
    if (!commit) {
        atomic_store_explicit(&s_discovered_endpoint_commit_pending, false,
                              memory_order_release);
        return;
    }

    controller_config_snapshot_t committed;
    controller_config_write_result_t result =
        controller_config_set_endpoint_if_current(
            &commit->token, commit->discovered, true,
            !commit->token.from_mdns, &committed);
    atomic_store_explicit(&s_discovered_endpoint_commit_pending, false,
                          memory_order_release);

    if (result == CONTROLLER_CONFIG_NOT_COMMITTED) {
        LOGI("Ignoring stale mDNS bridge result");
        return;
    }
    if (result == CONTROLLER_CONFIG_COMMITTED_UNVERIFIED) {
        post_unverified_config_diagnostic("Discovered bridge endpoint");
        return;
    }
    if (committed.value.bridge_base[0] == '\0') {
        LOGW("Could not persist discovered bridge");
        return;
    }
    controller_presentation_set_message("Hi-Fi Control: Found");
}

static void ui_connectivity_update_cb(void *arg) {
    controller_connectivity_view_t *view = arg;
    if (!view) {
        return;
    }
    controller_view_compat_apply_connectivity(view);
    free(view);
}

static void post_ui_connectivity_update(const char *line1, const char *line2) {
    controller_connectivity_view_t *view = bridge_external_alloc(sizeof(*view));
    if (!view) {
        return;
    }
    controller_connectivity_view_init(view, line1, line2);
    if (!platform_task_post_to_ui(ui_connectivity_update_cb, view)) {
        free(view);
    }
}

static void ui_zone_name_cb(void *arg) {
    char *name = arg;
    if (!name) {
        return;
    }
    controller_presentation_set_zone_name(name);
    free(name);
}

static void ui_battery_cb(void *arg) {
    (void)arg;
    controller_presentation_update_battery();
}

static void post_ui_battery_update(void) {
    platform_task_post_to_ui(ui_battery_cb, NULL);
}

static void default_now_playing(struct now_playing_state *state) {
    if (!state) {
        return;
    }
    snprintf(state->line1, sizeof(state->line1), "Idle");
    state->line2[0] = '\0';
    state->line3[0] = '\0';
    state->is_playing = false;
    state->volume = 0.0f;
    state->volume_min = -80.0f;
    state->volume_max = 0.0f;
    state->volume_step = 0.0f;
    state->seek_position = 0;
    state->length = 0;
    state->image_key[0] = '\0';
    state->config_sha[0] = '\0';
    state->zones_sha[0] = '\0';
}

static bool json_bool_field(const char *json, const char *field,
                            bool fallback) {
    const char *p = json ? strstr(json, field) : NULL;
    if (!p) return fallback;
    p = strchr(p, ':');
    if (!p) return fallback;
    do { ++p; } while (*p && isspace((unsigned char)*p));
    if (strncmp(p, "true", 4) == 0) return true;
    if (strncmp(p, "false", 5) == 0) return false;
    return fallback;
}

static void post_ui_update(const struct now_playing_state *state) {
    if (!state) {
        return;
    }

    controller_media_view_t *view = bridge_external_alloc(sizeof(*view));
    if (!view) {
        return;
    }

    lock_state();
    bool force_refresh = s_force_artwork_refresh;
    if (force_refresh) {
        s_force_artwork_refresh = false;
        ++s_artwork_generation;
    }
    uint32_t artwork_generation = s_artwork_generation;
    unlock_state();

    controller_media_view_init(
        view,
        state->line1,
        state->line2,
        state->line3,
        state->is_playing,
        state->volume,
        state->volume_min,
        state->volume_max,
        state->volume_step,
        state->seek_position,
        state->length,
        state->image_key,
        artwork_generation);
    if (!platform_task_post_to_ui(ui_update_cb, view)) {
        if (force_refresh) {
            lock_state();
            s_force_artwork_refresh = true;
            unlock_state();
        }
        free(view);
    }
}

static void post_ui_status_copy(bool *status_copy) {
    if (!platform_task_post_to_ui(ui_status_cb, status_copy)) {
        free(status_copy);
    }
}

static void post_ui_status(bool online) {
    bool *copy = bridge_external_alloc(sizeof(*copy));
    if (!copy) {
        return;
    }
    *copy = online;
    post_ui_status_copy(copy);
}

static void post_ui_message_copy(char *msg_copy) {
    if (!platform_task_post_to_ui(ui_message_cb, msg_copy)) {
        free(msg_copy);
    }
}

static void post_ui_message(const char *msg) {
    if (!msg) {
        return;
    }
    char *copy = bridge_external_strdup(msg);
    if (!copy) {
        return;
    }
    post_ui_message_copy(copy);
}

static void post_ui_zone_name_copy(char *name_copy) {
    if (!platform_task_post_to_ui(ui_zone_name_cb, name_copy)) {
        free(name_copy);
    }
}

static void post_ui_zone_name(const char *name) {
    if (!name) {
        return;
    }
    char *copy = bridge_external_strdup(name);
    if (!copy) {
        return;
    }
    post_ui_zone_name_copy(copy);
}

static void wait_for_poll_interval(const platform_power_snapshot_t *power) {
    // Use longer delay when display is sleeping, on battery, or bridge unreachable
    uint32_t delay_ms;
    controller_connection_t connection;
    bridge_client_connection_snapshot(&connection);
    if (!controller_connection_ready(&connection)) {
        uint64_t now = platform_millis();
        uint64_t remaining = connection.next_attempt_ms > now ? connection.next_attempt_ms - now : 1000;
        delay_ms = (uint32_t)(remaining > 60000 ? 60000 : remaining);
    } else if (platform_display_is_sleeping()) {
        // When sleeping AND zone not playing, use extended poll interval from config
        rk_cfg_t cfg;
        uint16_t sleep_poll_stopped =
            bridge_config_snapshot(&cfg) ? cfg.sleep_poll_stopped_sec : 0;
        if (!s_last_is_playing && sleep_poll_stopped > 0) {
            delay_ms = sleep_poll_stopped * 1000;  // Config is in seconds
        } else {
            delay_ms = POLL_DELAY_SLEEPING_MS;  // Default 30s when playing
        }
    } else if (power && power->external_power) {
        delay_ms = POLL_DELAY_AWAKE_CHARGING_MS;  // Fast polling when plugged in
    } else {
        delay_ms = POLL_DELAY_AWAKE_BATTERY_MS;   // Slower on battery to save power
    }
    uint64_t start = platform_millis();
    while (atomic_load_explicit(&s_running, memory_order_acquire)) {
        if (s_trigger_poll) {
            s_trigger_poll = false;
            break;
        }
        if (platform_millis() - start >= delay_ms) {
            break;
        }
        platform_sleep_ms(50);
    }
}

// Strip trailing slashes from URL to prevent double-slash issues
static void strip_trailing_slashes(char *url) {
    if (!url) return;
    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/') {
        url[--len] = '\0';
    }
}

/* Probe without publishing zones or changing durable selection. */
static bool probe_connection(const char *base, controller_connection_t *connection) {
    char url[256], zone[64] = {0}, knob_id[16];
    bridge_client_get_current_zone_id(zone, sizeof(zone));
    platform_http_get_knob_id(knob_id, sizeof(knob_id));
    int written = snprintf(url, sizeof(url), "%s/zones?knob_id=%s", base, knob_id);
    if (written <= 0 || (size_t)written >= sizeof(url)) return false;
    char *response = NULL; size_t response_len = 0;
    int result = platform_http_get(url, &response, &response_len);
    bool valid = false, selected = false; int count = -1;
    if (result == 0 && response) {
        cJSON *root = cJSON_ParseWithLength(response, response_len);
        cJSON *zones = cJSON_GetObjectItemCaseSensitive(root, "zones");
        valid = cJSON_IsObject(root) && cJSON_IsArray(zones);
        if (valid) {
            count = cJSON_GetArraySize(zones);
            cJSON *item;
            cJSON_ArrayForEach(item, zones) {
                cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "zone_id");
                if (zone[0] && cJSON_IsString(id) && strcmp(zone, id->valuestring) == 0)
                    selected = true;
            }
        }
        cJSON_Delete(root);
    }
    platform_http_free(response);
    controller_connection_api(connection, valid, count, selected, platform_millis());
    return valid;
}

static bool update_connection(void) {
    controller_config_endpoint_token_t token;
    if (!controller_config_capture_endpoint_token(&token)) return false;
    controller_connection_t next;
    lock_state(); next = s_connection; unlock_state();
    controller_connection_select(&next, token.bridge_base,
                                 token.from_mdns || !token.bridge_base[0], token.generation);
    uint64_t now = platform_millis();
    next.offline = false;
    if (!controller_connection_due(&next, now)) return false;
    platform_mdns_observation_t observed = {0};
    if (next.automatic) platform_mdns_observe_bridge(token.bridge_base, &observed);
    next.discovered = observed.seen;
    next.ambiguous = observed.ambiguous;
    next.mdns_resolution_failed = false;
    next.resolved = false;
    next.resolver = CONNECTION_RESOLVER_NONE;
    next.endpoint[0] = 0;
    const char *identity = observed.seen ? observed.identity : token.bridge_base;
    if (!next.ambiguous && observed.endpoint[0]) {
        rk_strlcpy(next.endpoint, observed.endpoint, sizeof(next.endpoint));
        next.resolved = true; next.resolver = CONNECTION_RESOLVER_MDNS;
    } else if (!next.ambiguous && identity[0]) {
        bool dns = false, failed = false;
        next.resolved = platform_mdns_resolve_base_url(identity, next.endpoint,
                                sizeof(next.endpoint), &dns, &failed);
        next.mdns_resolution_failed = failed;
        if (next.resolved) {
            char host[64];
            bool literal = platform_mdns_url_host(identity, host, sizeof(host), NULL) &&
                           strspn(host, "0123456789.") == strlen(host);
            next.resolver = literal ? CONNECTION_RESOLVER_LITERAL :
                            dns ? CONNECTION_RESOLVER_DNS : CONNECTION_RESOLVER_MDNS;
        }
    }
    bool valid = next.resolved && probe_connection(next.endpoint, &next);
    if (!valid) controller_connection_api(&next, false, -1, false, platform_millis());
    if (valid) next.next_attempt_ms = platform_millis() + 60000;
    else controller_connection_schedule(&next, platform_millis(), false);
    /* Reject all evidence as well as writes if a manual decision raced I/O. */
    controller_config_endpoint_token_t current;
    if (!controller_config_capture_endpoint_token(&current) || current.generation != token.generation)
        return false;
    lock_state(); s_connection = next; unlock_state();
    char summary[128];
    controller_connection_summary(&next, summary, sizeof(summary));
    LOGI("Connection: %s (discovered=%d resolver=%d zones=%d retry_in_ms=%llu)",
         summary, next.discovered, next.resolver, next.zone_count,
         (unsigned long long)(next.next_attempt_ms - next.observed_ms));
    if (valid && observed.seen && !observed.ambiguous &&
        strcmp(token.bridge_base, observed.identity) != 0) {
        bool expected = false;
        if (atomic_compare_exchange_strong_explicit(&s_discovered_endpoint_commit_pending,
                &expected, true, memory_order_acq_rel, memory_order_acquire)) {
            s_discovered_endpoint_commit.token = token;
            rk_strlcpy(s_discovered_endpoint_commit.discovered, observed.identity,
                       sizeof(s_discovered_endpoint_commit.discovered));
            if (!platform_task_post_to_ui(commit_discovered_endpoint_on_ui, &s_discovered_endpoint_commit))
                atomic_store_explicit(&s_discovered_endpoint_commit_pending, false, memory_order_release);
        }
    }
    return valid;
}

static bool fetch_now_playing(struct now_playing_state *state,
                              const platform_power_snapshot_t *power) {
    if (!state || !power) {
        return false;
    }
    char bridge_base[sizeof(((rk_cfg_t *)0)->bridge_base)] = {0};
    char zone_id[sizeof(((rk_cfg_t *)0)->zone_id)] = {0};
    if (!bridge_endpoint_snapshot(bridge_base, sizeof(bridge_base), zone_id,
                                  sizeof(zone_id))) {
        return false;
    }

    if (bridge_base[0] == '\0' || zone_id[0] == '\0') {
        LOGI("fetch_now_playing: bridge_base or zone_id empty (bridge_base='%s', zone_id='%s')", bridge_base, zone_id);
        return false;
    }

    // Get battery status for reporting to bridge
    const int battery_level = power->battery_level;
    const bool battery_charging = power->external_power;

    // Get knob ID for config_sha lookup
    char knob_id[16];
    platform_http_get_knob_id(knob_id, sizeof(knob_id));

    char url[384];
    if (battery_level >= 0 && battery_level <= 100) {
        snprintf(url, sizeof(url),
                 "%s/now_playing?zone_id=%s&battery_level=%d&battery_charging=%d&knob_id=%s",
                 bridge_base, zone_id, battery_level, battery_charging ? 1 : 0,
                 knob_id);
    } else {
        /* Some mains-powered targets have no battery gauge.  Omitting the
         * optional field is valid; sending -1 makes UHC's u8 query parser
         * reject the whole request. */
        snprintf(url, sizeof(url), "%s/now_playing?zone_id=%s&battery_charging=%d&knob_id=%s",
                 bridge_base, zone_id, battery_charging ? 1 : 0, knob_id);
    }
    LOGD("now_playing request zone=%s url=%s", zone_id, url);
    char *resp = NULL;
    size_t resp_len = 0;
    int ret = platform_http_get(url, &resp, &resp_len);
    LOGD("now_playing response ret=%d bytes=%u", ret, (unsigned)resp_len);
    if (ret != 0 || !resp) {
        LOGW("now_playing request failed: ret=%d response=%s", ret,
             resp ? "present" : "missing");
        platform_http_free(resp);
        return false;
    }

    if (strstr(resp, "\"error\"") || resp_len == 0) {
        LOGW("now_playing response was empty or reported an error");
        platform_http_free(resp);
        return false;
    }

    const char *line1 = strstr(resp, "\"line1\"");
    if (line1) {
        extract_json_string(line1, "\"line1\"", state->line1, sizeof(state->line1));
    }
    const char *line2 = strstr(resp, "\"line2\"");
    if (line2) {
        extract_json_string(line2, "\"line2\"", state->line2, sizeof(state->line2));
    }
    const char *line3 = strstr(resp, "\"line3\"");
    if (line3) {
        extract_json_string(line3, "\"line3\"", state->line3, sizeof(state->line3));
    }
    /* Preserve the last good value if a transient/partial response omits the
     * field. Treating an incomplete payload as stopped makes the UI lie. */
    state->is_playing = json_bool_field(resp, "\"is_playing\"", state->is_playing);

    const char *vol_key = strstr(resp, "\"volume\"");
    if (vol_key) {
        const char *colon = strchr(vol_key, ':');
        if (colon) {
            state->volume = atof(colon + 1);
        }
    }

    const char *vol_min_key = strstr(resp, "\"volume_min\"");
    if (vol_min_key) {
        const char *colon = strchr(vol_min_key, ':');
        if (colon) {
            state->volume_min = atof(colon + 1);
        }
    }

    const char *vol_max_key = strstr(resp, "\"volume_max\"");
    if (vol_max_key) {
        const char *colon = strchr(vol_max_key, ':');
        if (colon) {
            state->volume_max = atof(colon + 1);
        }
    }

    state->volume_step = 1.0f;  // Default 1.0 dB step
    const char *step_key = strstr(resp, "\"volume_step\"");
    if (step_key) {
        const char *colon = strchr(step_key, ':');
        if (colon) {
            float parsed = atof(colon + 1);
            if (parsed > 0.0f) {
                state->volume_step = parsed;
            }
        }
    }

    const char *seek_key = strstr(resp, "\"seek_position\"");
    if (seek_key) {
        const char *colon = strchr(seek_key, ':');
        if (colon) {
            state->seek_position = atoi(colon + 1);
        }
    }
    const char *length_key = strstr(resp, "\"length\"");
    if (length_key) {
        const char *colon = strchr(length_key, ':');
        if (colon) {
            state->length = atoi(colon + 1);
        }
    }

    // Parse image_key for album artwork
    const char *image_key = strstr(resp, "\"image_key\"");
    if (image_key) {
        extract_json_string(image_key, "\"image_key\"", state->image_key, sizeof(state->image_key));
    }
    LOGD("now_playing parsed playing=%s image_key=%s zone=%s",
         state->is_playing ? "true" : "false", state->image_key, zone_id);
    /* A partial response may omit artwork metadata. Keep the last good key;
     * the bridge normally supplies a string (or an empty string) here. */

    // Parse config_sha for config change detection (silent - checked in poll loop)
    const char *config_sha_key = strstr(resp, "\"config_sha\"");
    if (config_sha_key) {
        extract_json_string(config_sha_key, "\"config_sha\"", state->config_sha, sizeof(state->config_sha));
    } else {
        state->config_sha[0] = '\0';
    }

    // Parse zones_sha for zone list change detection
    const char *zones_sha_key = strstr(resp, "\"zones_sha\"");
    if (zones_sha_key) {
        extract_json_string(zones_sha_key, "\"zones_sha\"", state->zones_sha, sizeof(state->zones_sha));
    } else {
        state->zones_sha[0] = '\0';
    }

    // Note: Don't parse zones from now_playing response - it doesn't have zone_name
    // Zones are parsed from /zones endpoint in refresh_zone_label()
    platform_http_free(resp);
    return true;
}

static bool refresh_zone_label(bool prefer_zone_id) {
    LOGI("refresh_zone_label: Called (prefer_zone_id=%s)", prefer_zone_id ? "true" : "false");
    rk_cfg_t cfg;
    if (!bridge_config_snapshot(&cfg)) {
        return false;
    }
    char bridge_base[sizeof(cfg.bridge_base)] = {0};
    char ignored_zone_id[sizeof(cfg.zone_id)] = {0};
    if (!bridge_endpoint_snapshot(bridge_base, sizeof(bridge_base),
                                  ignored_zone_id, sizeof(ignored_zone_id))) {
        return false;
    }
    if (bridge_base[0] == '\0') {
        LOGI("refresh_zone_label: bridge_base is empty, returning false");
        return false;
    }

    // Get knob ID for zone filtering
    char knob_id[16];
    platform_http_get_knob_id(knob_id, sizeof(knob_id));

    char url[256];
    snprintf(url, sizeof(url), "%s/zones?knob_id=%s", bridge_base, knob_id);
    LOGI("refresh_zone_label: Requesting %s", url);

    char *resp = NULL;
    size_t resp_len = 0;
    bool success = false;

    if (platform_http_get(url, &resp, &resp_len) != 0 || !resp) {
        LOGI("refresh_zone_label: HTTP request failed");
        parse_zones_from_response(NULL);
        platform_http_free(resp);
        return false;
    }

    LOGI("refresh_zone_label: Received %zu bytes", resp_len);
    parse_zones_from_response(resp);

    char zone_label_copy[MAX_ZONE_NAME] = {0};
    char selected_zone_id[sizeof(cfg.zone_id)] = {0};
    char preferred_zone_id[sizeof(cfg.zone_id)] = {0};
    bool persist_zone = false;
    lock_state();
    if (s_state.runtime_zone_pinned) {
        rk_strlcpy(preferred_zone_id, s_state.runtime_zone_id,
                   sizeof(preferred_zone_id));
    } else {
        rk_strlcpy(preferred_zone_id, cfg.zone_id,
                   sizeof(preferred_zone_id));
    }
    LOGI("refresh_zone_label: Parsed %d zones", s_state.zone_count);
    if (s_state.zone_count > 0) {
        for (int i = 0; i < s_state.zone_count; ++i) {
            bridge_zone_t *entry = &s_state.zones[i];
            if (prefer_zone_id && preferred_zone_id[0] &&
                strcmp(entry->id, preferred_zone_id) == 0) {
                rk_strlcpy(selected_zone_id, entry->id,
                           sizeof(selected_zone_id));
                rk_strlcpy(zone_label_copy, entry->name,
                           sizeof(zone_label_copy));
                break;
            }
            if (!preferred_zone_id[0]) {
                rk_strlcpy(selected_zone_id, entry->id,
                           sizeof(selected_zone_id));
                rk_strlcpy(zone_label_copy, entry->name,
                           sizeof(zone_label_copy));
                persist_zone = true;
                break;
            }
        }
        success = zone_label_copy[0] != '\0';
    }
    unlock_state();

    platform_http_free(resp);
    if (!success) {
        LOGI("refresh_zone_label: No zone selected (success=false)");
        return false;
    }
    if (persist_zone) {
        controller_config_snapshot_t committed;
        controller_config_write_result_t result = controller_config_set_zone(
            selected_zone_id, &committed);
        if (result == CONTROLLER_CONFIG_NOT_COMMITTED) {
            LOGW("Could not persist selected zone");
            return false;
        }
        if (result == CONTROLLER_CONFIG_COMMITTED_UNVERIFIED) {
            post_unverified_config_diagnostic("Selected zone");
            /* A candidate may survive reboot, but do not switch this running
             * controller to a selection whose durability is unverified. */
            pin_runtime_zone_selection(preferred_zone_id);
            return false;
        }
    }
    lock_state();
    rk_strlcpy(s_state.zone_label, zone_label_copy, sizeof(s_state.zone_label));
    rk_strlcpy(s_state.runtime_zone_id, selected_zone_id,
               sizeof(s_state.runtime_zone_id));
    s_state.runtime_zone_pinned = true;
    s_connection.selected_zone_available = true;
    unlock_state();
    LOGI("refresh_zone_label: Selected zone '%s', posting to UI",
         zone_label_copy);
    post_ui_zone_name(zone_label_copy);
    return success;
}

static void parse_zones_from_response(const char *resp) {
    char selected[64] = {0};
    bridge_client_get_current_zone_id(selected, sizeof(selected));
    cJSON *root = resp ? cJSON_Parse(resp) : NULL;
    cJSON *zones = cJSON_GetObjectItemCaseSensitive(root, "zones");
    bool valid = cJSON_IsObject(root) && cJSON_IsArray(zones);
    bool available = false;
    lock_state();
    s_state.zone_count = 0;
    if (valid) {
        cJSON *item;
        cJSON_ArrayForEach(item, zones) {
            cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "zone_id");
            cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "zone_name");
            if (!cJSON_IsString(id) || !cJSON_IsString(name)) continue;
            if (selected[0] && strcmp(selected, id->valuestring) == 0) available = true;
            if (s_state.zone_count == MAX_ZONES) continue;
            bridge_zone_t *entry = &s_state.zones[s_state.zone_count++];
            rk_strlcpy(entry->id, id->valuestring, sizeof(entry->id));
            rk_strlcpy(entry->name, name->valuestring, sizeof(entry->name));
        }
    }
    controller_connection_api(&s_connection, valid,
        valid ? cJSON_GetArraySize(zones) : -1, available, platform_millis());
    if (!valid) controller_connection_schedule(&s_connection, platform_millis(), false);
    unlock_state();
    cJSON_Delete(root);
}

static const char *extract_json_string(const char *start, const char *key, char *out, size_t len) {
    const char *key_pos = strstr(start, key);
    if (!key_pos) {
        return NULL;
    }
    const char *colon = strchr(key_pos, ':');
    if (!colon) {
        return NULL;
    }
    const char *quote_start = strchr(colon, '"');
    if (!quote_start) {
        return NULL;
    }
    quote_start++;
    const char *quote_end = strchr(quote_start, '"');
    if (!quote_end) {
        return NULL;
    }
    size_t copy_len = quote_end - quote_start;
    if (copy_len >= len) {
        copy_len = len - 1;
    }
    memcpy(out, quote_start, copy_len);
    out[copy_len] = '\0';
    return quote_end + 1;
}

static bool send_control_json(const char *json) {
    if (!json) {
        return false;
    }
    char bridge_base[sizeof(((rk_cfg_t *)0)->bridge_base)] = {0};
    char zone_id[sizeof(((rk_cfg_t *)0)->zone_id)] = {0};
    if (!bridge_endpoint_snapshot(bridge_base, sizeof(bridge_base), zone_id,
                                  sizeof(zone_id))) {
        return false;
    }
    if (bridge_base[0] == '\0' || zone_id[0] == '\0') {
        return false;
    }
    char url[256];
    snprintf(url, sizeof(url), "%s/control", bridge_base);
    char *resp = NULL;
    size_t resp_len = 0;
    int ret = platform_http_post_json(url, json, &resp, &resp_len);
    if (ret != 0) {
        platform_http_free(resp);
        return false;
    }
    if (resp && strstr(resp, "\"error\"")) {
        platform_http_free(resp);
        return false;
    }
    platform_http_free(resp);
    return true;
}

static void bridge_poll_thread(void *arg) {
    (void)arg;
    LOGI("Bridge poll thread started");
    struct now_playing_state state;
    default_now_playing(&state);
    while (atomic_load_explicit(&s_running, memory_order_acquire)) {
        platform_power_snapshot_t power = {
            .battery_level = -1,
            .external_power = true,
        };
        platform_power_snapshot(&power);

        // Skip HTTP requests if network is not ready yet (or in BLE mode)
        // In BLE mode, s_network_ready is false, so we just sleep without logging
        if (!atomic_load_explicit(&s_network_ready, memory_order_acquire)) {
            wait_for_poll_interval(&power);
            continue;
        }

        controller_connection_t connection;
        bridge_client_connection_snapshot(&connection);
        bool was_ready = controller_connection_ready(&connection);
        bool checked = update_connection();
        bridge_client_connection_snapshot(&connection);
        if (checked && connection.reachable) refresh_zone_label(true);
        bridge_client_connection_snapshot(&connection);
        bool ready = controller_connection_ready(&connection);
        bool ok = ready && fetch_now_playing(&state, &power);
        if (ok) {
            lock_state();
            controller_connection_api(&s_connection, true, s_state.zone_count, true, platform_millis());
            s_connection.failures = 0;
            unlock_state();
        } else if (ready) {
            lock_state();
            controller_connection_api(&s_connection, false, -1, false, platform_millis());
            controller_connection_schedule(&s_connection, platform_millis(), false);
            unlock_state();
        }
        bridge_client_connection_snapshot(&connection);
        post_ui_status(controller_connection_ready(&connection));

        // Track play state for extended sleep polling
        if (ok) {
            s_last_is_playing = state.is_playing;
        }

        // Check for config/zones changes (only when bridge is responding)
        if (ok) {
            check_config_sha(state.config_sha);
            check_zones_sha(state.zones_sha);
        }

        // Always check charging state (works in AP mode too)
        check_charging_state_change(power.external_power);

        // Present the connection owner's current evidence.
        if (ok) {
            // Bridge connected - show now playing data
            post_ui_update(&state);
            if (!was_ready) {
                // Readiness was established - restore the playback presentation.
                post_ui_message("Hi-Fi Control: Connected");
                post_ui_network_status("");
                // Restore zone name (was cleared during error display)
                lock_state();
                char zone_name_copy[MAX_ZONE_NAME];
                strncpy(zone_name_copy, s_state.zone_label, sizeof(zone_name_copy) - 1);
                zone_name_copy[sizeof(zone_name_copy) - 1] = '\0';
                unlock_state();
                if (zone_name_copy[0]) {
                    post_ui_zone_name(zone_name_copy);
                }
            }
        } else {
            controller_connection_t status;
            bridge_client_connection_snapshot(&status);
            char summary[128];
            controller_connection_summary(&status, summary, sizeof(summary));
            post_ui_connectivity_update("See Settings for details", summary);
            post_ui_network_status(summary);
        }
        wait_for_poll_interval(&power);
    }
}

void bridge_client_start(void) {
    rk_cfg_t cfg;
    if (!bridge_config_snapshot(&cfg)) {
        LOGW("Bridge start skipped: controller configuration unavailable");
        return;
    }
    bridge_json_allocator_init();
    platform_task_init();
    lock_state();
    strncpy(s_state.zone_label,
            cfg.zone_id[0] ? cfg.zone_id : "Choose a zone in device controls",
            sizeof(s_state.zone_label) - 1);
    s_state.zone_label[sizeof(s_state.zone_label) - 1] = '\0';
    char initial_zone_label[MAX_ZONE_NAME];
    rk_strlcpy(initial_zone_label, s_state.zone_label,
               sizeof(initial_zone_label));
    unlock_state();
    post_ui_zone_name(initial_zone_label);

    // Always apply config on startup (uses defaults if no saved config)
    // This ensures rotation is applied even on fresh devices
    LOGI("Applying config on startup: rot=%d/%d sha='%s'",
         cfg.rotation_charging, cfg.rotation_not_charging,
         cfg.config_sha[0] ? cfg.config_sha : "(none)");
    apply_knob_config(&cfg);

    start_bridge_poll_task();
}

bool bridge_client_execute_command(const controller_command_t *command) {
    if (!command) {
        return false;
    }

    if (command->kind == CONTROLLER_COMMAND_PREVIOUS_ZONE ||
        command->kind == CONTROLLER_COMMAND_NEXT_ZONE) {
        bridge_zone_t zones[BRIDGE_CLIENT_MAX_ZONES];
        const int count = bridge_client_get_zones(zones, BRIDGE_CLIENT_MAX_ZONES);
        if (count < 2) return false;
        char current[sizeof(zones[0].id)] = {};
        (void)bridge_client_get_current_zone_id(current, sizeof(current));
        int index = 0;
        for (int i = 0; i < count; ++i) {
            if (strcmp(zones[i].id, current) == 0) { index = i; break; }
        }
        const int delta = command->kind == CONTROLLER_COMMAND_NEXT_ZONE ? 1 : -1;
        const int next = (index + delta + count) % count;
        return bridge_client_set_zone(zones[next].id);
    }

    bridge_command_context_t context;
    bridge_command_plan_t plan;
    rk_cfg_t cfg;
    if (!bridge_config_snapshot(&cfg)) {
        return false;
    }
    controller_connection_t connection;
    bridge_client_connection_snapshot(&connection);
    char current_zone[64];
    bridge_client_get_current_zone_id(current_zone, sizeof(current_zone));
    lock_state();
    context.ready = controller_connection_ready(&connection);
    context.zone_id = current_zone;
    context.volume = s_last_known_volume;
    context.volume_min = s_last_known_volume_min;
    context.volume_max = s_last_known_volume_max;
    context.volume_step = s_last_known_volume_step;
    bool planned = bridge_command_plan_build(command, &context, &plan);
    if (planned && plan.accepted && plan.updates_volume) {
        s_last_known_volume = plan.predicted_volume;
    }
    unlock_state();

    if (!planned) {
        return false;
    }

    static const char *const feedback_text[] = {
        [BRIDGE_COMMAND_FEEDBACK_NONE] = NULL,
        [BRIDGE_COMMAND_FEEDBACK_NOT_READY] = "Not ready",
        [BRIDGE_COMMAND_FEEDBACK_PLAYBACK_FAILED] = "Play/pause failed",
        [BRIDGE_COMMAND_FEEDBACK_NEXT_FAILED] = "Next track failed",
        [BRIDGE_COMMAND_FEEDBACK_PREVIOUS_FAILED] = "Previous track failed",
        [BRIDGE_COMMAND_FEEDBACK_VOLUME_FAILED] = "Volume change failed",
    };

    if (!plan.accepted) {
        if (plan.rejection_feedback > BRIDGE_COMMAND_FEEDBACK_NONE &&
            plan.rejection_feedback <= BRIDGE_COMMAND_FEEDBACK_VOLUME_FAILED) {
            if (plan.rejection_feedback == BRIDGE_COMMAND_FEEDBACK_NOT_READY) {
                char summary[128];
                controller_connection_summary(&connection, summary, sizeof(summary));
                post_ui_message(summary);
            } else post_ui_message(feedback_text[plan.rejection_feedback]);
        }
        return false;
    }
    if (plan.no_op) {
        return true;
    }
    if (plan.updates_volume) {
        controller_presentation_show_volume_change(
            plan.predicted_volume, plan.volume_step);
    }

    bool sent = send_control_json(plan.json);
    if (!sent &&
        plan.failure_feedback > BRIDGE_COMMAND_FEEDBACK_NONE &&
        plan.failure_feedback <= BRIDGE_COMMAND_FEEDBACK_VOLUME_FAILED) {
        post_ui_message(feedback_text[plan.failure_feedback]);
    }
    return sent;
}

void bridge_client_set_network_ready(bool ready) {
    bool was_ready = atomic_exchange_explicit(&s_network_ready, ready,
                                               memory_order_acq_rel);

    if (ready && !atomic_load_explicit(&s_running, memory_order_acquire)) {
        unsigned attempts = atomic_load_explicit(&s_worker_start_attempts,
                                                  memory_order_relaxed);
        if (!was_ready && attempts < BRIDGE_POLL_TASK_START_ATTEMPTS) {
            LOGW("Retrying Unified Hi-Fi Control polling task startup");
            start_bridge_poll_task();
            attempts = atomic_load_explicit(&s_worker_start_attempts,
                                            memory_order_relaxed);
        }
        if (!atomic_load_explicit(&s_running, memory_order_acquire)) {
            LOGE("Unified Hi-Fi Control polling unavailable after %u attempt(s)",
                 attempts);
            post_ui_connectivity_update("Restart device",
                                        "Hi-Fi Control unavailable");
            post_ui_network_status(
                "Hi-Fi Control unavailable - restart device");
            return;
        }
    }

    if (ready == was_ready) return;
    lock_state();
    s_connection.offline = !ready;
    s_connection.reachable = false;
    s_connection.zones_current = false;
    s_connection.next_attempt_ms = 0;
    s_trigger_poll = true;
    unlock_state();
    char summary[128];
    bridge_client_connection_status(summary, sizeof(summary), NULL, 0);
    post_ui_network_status(summary);
}

const char* bridge_client_get_artwork_url(char *url_buf, size_t buf_len, int width, int height) {
    return bridge_client_get_artwork_url_for_format(url_buf, buf_len, width,
                                                    height, 0, "rgb565");
}

const char* bridge_client_get_artwork_url_for_format(char *url_buf, size_t buf_len,
                                                     int width, int height,
                                                     int clip_radius,
                                                     const char *format) {
    return bridge_client_get_artwork_url_for_format_and_scale(
        url_buf, buf_len, width, height, clip_radius, format, "fit", 0);
}

const char* bridge_client_get_artwork_url_for_format_and_scale(
    char *url_buf, size_t buf_len, int width, int height, int clip_radius,
    const char *format, const char *scale, int crop_limit_percent) {
    if (!url_buf || buf_len == 0) {
        return NULL;
    }

    if (!format || !format[0]) {
        format = "rgb565";
    }
    if (!scale || !scale[0]) {
        scale = "fit";
    }

    char bridge_base[sizeof(((rk_cfg_t *)0)->bridge_base)] = {0};
    char zone_id[sizeof(((rk_cfg_t *)0)->zone_id)] = {0};
    if (!bridge_endpoint_snapshot(bridge_base, sizeof(bridge_base), zone_id,
                                  sizeof(zone_id)) ||
        !bridge_base[0] || !zone_id[0]) {
        return NULL;
    }

    int written = 0;
    if (clip_radius > 0 && crop_limit_percent > 0) {
        written = snprintf(url_buf, buf_len,
                           "%s/now_playing/image?zone_id=%s&scale=%s&crop_limit=%d&width=%d&height=%d&format=%s&clip_radius=%d",
                           bridge_base, zone_id, scale, crop_limit_percent,
                           width, height, format, clip_radius);
    } else if (clip_radius > 0) {
        written = snprintf(url_buf, buf_len,
                           "%s/now_playing/image?zone_id=%s&scale=%s&width=%d&height=%d&format=%s&clip_radius=%d",
                           bridge_base, zone_id, scale, width, height, format,
                           clip_radius);
    } else if (crop_limit_percent > 0) {
        written = snprintf(url_buf, buf_len,
                           "%s/now_playing/image?zone_id=%s&scale=%s&crop_limit=%d&width=%d&height=%d&format=%s",
                           bridge_base, zone_id, scale, crop_limit_percent,
                           width, height, format);
    } else {
        written = snprintf(url_buf, buf_len,
                           "%s/now_playing/image?zone_id=%s&scale=%s&width=%d&height=%d&format=%s",
                           bridge_base, zone_id, scale, width, height, format);
    }
    if (written < 0 || (size_t)written >= buf_len) {
        url_buf[0] = '\0';
        LOGE("Artwork URL exceeds the caller buffer");
        return NULL;
    }
    return url_buf;
}

int bridge_client_fetch_artwork(const char *image_key, int width, int height, const char *format,
                                char **out, size_t *out_len) {
    if (!out || width <= 0 || height <= 0) return -1;
    *out = NULL;
    if (out_len) *out_len = 0;
    char url[512];
    if (!bridge_client_get_artwork_url_for_format(url, sizeof(url), width,
                                                   height, 0, format)) {
        return -1;
    }
    uint32_t cache_key = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)(image_key ? image_key : ""); *p; ++p) {
        cache_key ^= *p;
        cache_key *= 16777619u;
    }
    size_t len = strlen(url);
    if (len + 20 < sizeof(url)) {
        snprintf(url + len, sizeof(url) - len, "&cache_bust=%08x",
                 (unsigned)cache_key);
    }
    return platform_http_get_image(url, out, out_len);
}

int bridge_client_stream_artwork(const char *image_key, int width, int height,
                                 const char *format, size_t max_bytes,
                                 platform_http_stream_callback_t callback,
                                 void *ctx, size_t *out_len) {
    if (width <= 0 || height <= 0 || max_bytes == 0 || !callback) return -1;

    char url[512];
    if (!bridge_client_get_artwork_url_for_format(url, sizeof(url), width,
                                                   height, 0, format)) {
        return -1;
    }

    uint32_t cache_key = 2166136261u;
    for (const unsigned char *p =
             (const unsigned char *)(image_key ? image_key : ""); *p; ++p) {
        cache_key ^= *p;
        cache_key *= 16777619u;
    }
    size_t len = strlen(url);
    if (len + 20 >= sizeof(url)) return -1;
    snprintf(url + len, sizeof(url) - len, "&cache_bust=%08x",
             (unsigned)cache_key);
    return platform_http_stream(url, max_bytes, callback, ctx, out_len);
}

bool bridge_client_is_ready_for_art_mode(void) {
    controller_connection_t connection;
    bridge_client_connection_snapshot(&connection);
    return controller_connection_ready(&connection);
}

bool bridge_client_get_bridge_url(char *buf, size_t len) {
    if (!buf || len == 0) {
        return false;
    }
    rk_cfg_t cfg;
    bool has_bridge = bridge_config_snapshot(&cfg) && cfg.bridge_base[0] != '\0';
    if (has_bridge) {
        strncpy(buf, cfg.bridge_base, len - 1);
        buf[len - 1] = '\0';
    } else {
        buf[0] = '\0';
    }
    return has_bridge;
}

void bridge_client_connection_snapshot(controller_connection_t *out) {
    controller_config_endpoint_token_t token;
    lock_state(); *out = s_connection; unlock_state();
    if (controller_config_capture_endpoint_token(&token))
        controller_connection_select(out, token.bridge_base,
            token.from_mdns || !token.bridge_base[0], token.generation);
    else memset(out, 0, sizeof(*out));
    controller_connection_expire(out, platform_millis());
    out->offline = !atomic_load_explicit(&s_network_ready, memory_order_acquire);
    if (out->offline) {
        out->reachable = false; out->zones_current = false;
    }
}
bool bridge_client_is_bridge_mdns(void) {
    rk_cfg_t cfg;
    return bridge_config_snapshot(&cfg) && cfg.bridge_from_mdns != 0;
}

int bridge_client_get_zones(bridge_zone_t *out, int max) {
    if (!out || max <= 0) {
        return 0;
    }

    lock_state();
    int count = s_state.zone_count < max ? s_state.zone_count : max;
    for (int i = 0; i < count; ++i) {
        strncpy(out[i].id, s_state.zones[i].id, sizeof(out[i].id) - 1);
        out[i].id[sizeof(out[i].id) - 1] = '\0';
        strncpy(out[i].name, s_state.zones[i].name, sizeof(out[i].name) - 1);
        out[i].name[sizeof(out[i].name) - 1] = '\0';
    }
    unlock_state();
    return count;
}

bool bridge_client_get_current_zone_id(char *out, size_t len) {
    if (!out || len == 0) {
        return false;
    }

    char ignored_bridge_base[sizeof(((rk_cfg_t *)0)->bridge_base)] = {0};
    if (!bridge_endpoint_snapshot(ignored_bridge_base,
                                  sizeof(ignored_bridge_base), out, len)) {
        out[0] = '\0';
        return false;
    }
    return out[0] != '\0';
}

bool bridge_client_visit_zones(bridge_zone_list_visitor_t visitor, void *ctx) {
    if (!visitor) {
        return false;
    }

    /*
     * The zone storage is borrowed only for the duration of this synchronous
     * callback. The visitor must copy anything it retains and must not call
     * bridge_client APIs while the state lock is held.
     */
    rk_cfg_t cfg;
    if (!bridge_config_snapshot(&cfg)) {
        return false;
    }
    lock_state();
    const char *current_zone_id = s_state.runtime_zone_pinned
                                      ? s_state.runtime_zone_id
                                      : cfg.zone_id;
    visitor(s_state.zones, s_state.zone_count, current_zone_id, ctx);
    unlock_state();
    return true;
}

bridge_zone_selection_result_t bridge_client_select_zone_value(
    const char *zone_id) {
    bridge_zone_selection_result_t result = {0};
    if (!zone_id || !zone_id[0]) {
        return result;
    }

    char selected_name[MAX_ZONE_NAME] = {0};
    lock_state();
    for (int i = 0; i < s_state.zone_count; ++i) {
        bridge_zone_t *entry = &s_state.zones[i];
        if (strcmp(entry->id, zone_id) != 0) {
            continue;
        }
        rk_strlcpy(selected_name, entry->name, sizeof(selected_name));
        result.found = true;
        break;
    }
    unlock_state();

    if (!result.found) {
        return result;
    }
    rk_cfg_t prior_config;
    if (!bridge_config_snapshot(&prior_config)) {
        return result;
    }
    controller_config_snapshot_t committed;
    controller_config_write_result_t write_result = controller_config_set_zone(
        zone_id, &committed);
    if (write_result == CONTROLLER_CONFIG_NOT_COMMITTED) {
        return result;
    }
    if (write_result == CONTROLLER_CONFIG_COMMITTED_UNVERIFIED) {
        post_unverified_config_diagnostic("Zone selection");
        /* Keep the pre-transaction runtime zone even though the owner must
         * publish its candidate to avoid a RAM/reboot split. */
        lock_state();
        bool already_pinned = s_state.runtime_zone_pinned;
        unlock_state();
        if (!already_pinned) {
            pin_runtime_zone_selection(prior_config.zone_id);
        }
        return result;
    }

    lock_state();
    rk_strlcpy(s_state.zone_label, selected_name, sizeof(s_state.zone_label));
    rk_strlcpy(s_state.runtime_zone_id, zone_id,
               sizeof(s_state.runtime_zone_id));
    s_state.runtime_zone_pinned = true;
    rk_strlcpy(result.zone_name, s_state.zone_label, sizeof(result.zone_name));
    bool was_ready = controller_connection_ready(&s_connection);
    s_connection.selected_zone_available = true;
    result.became_ready = !was_ready && controller_connection_ready(&s_connection);
    s_trigger_poll = true;
    s_force_artwork_refresh = true;
    result.persisted = true;
    unlock_state();
    return result;
}

bool bridge_client_set_zone(const char *zone_id) {
    if (!zone_id || !zone_id[0]) {
        return false;
    }

    bridge_zone_selection_result_t result =
        bridge_client_select_zone_value(zone_id);

    if (!result.found) {
        LOGW("Zone selection: zone id '%s' not found", zone_id);
        return false;
    }

    if (!result.persisted) {
        LOGW("Zone selection: could not persist zone '%s'", zone_id);
        return false;
    }
    post_ui_zone_name(result.zone_name);
    post_ui_message("Loading zone...");
    return true;
}

// Config fetch and apply implementation

// Data passed to UI thread for config application
struct apply_config_ui_data {
    uint16_t rotation;
    bool is_charging;
    rk_cfg_t cfg;  // Copy of config for timeout updates
};

// Called on UI thread to apply config safely
static void apply_config_on_ui_thread(void *arg) {
    struct apply_config_ui_data *data = (struct apply_config_ui_data *)arg;
    if (!data) return;

    platform_display_set_rotation(data->rotation);

    platform_display_apply_config(&data->cfg, data->is_charging);

    LOGI("Config applied on UI thread: rotation=%d", data->rotation);
    free(data);
}

static void apply_knob_config(const rk_cfg_t *cfg) {
    if (!cfg) {
        return;
    }

    // Get current charging state
    platform_power_snapshot_t power = {
        .battery_level = -1,
        .external_power = true,
    };
    platform_power_snapshot(&power);
    bool is_charging = power.external_power;
    uint16_t rotation = rk_cfg_get_rotation(cfg, is_charging);

    LOGI("Config apply requested: name='%s' rotation=%d (charging=%s)",
         cfg->knob_name[0] ? cfg->knob_name : "(unnamed)",
         rotation, is_charging ? "yes" : "no");

    // Post to UI thread since LVGL is not thread-safe
    struct apply_config_ui_data *data = bridge_external_alloc(sizeof(*data));
    if (data) {
        data->rotation = rotation;
        data->is_charging = is_charging;
        data->cfg = *cfg;
        if (!platform_task_post_to_ui(apply_config_on_ui_thread, data)) {
            free(data);
        }
    }
}

static void check_config_sha(const char *new_sha) {
    if (!new_sha || !new_sha[0]) {
        return;
    }

    rk_cfg_t cfg;
    if (!bridge_config_snapshot(&cfg)) {
        return;
    }
    bool sha_changed = strcmp(cfg.config_sha, new_sha) != 0;

    if (sha_changed) {
        LOGI("Config SHA changed: '%s' -> '%s', fetching new config",
             cfg.config_sha[0] ? cfg.config_sha : "(empty)", new_sha);
        fetch_knob_config();
    }
}

static void check_zones_sha(const char *new_sha) {
    // Skip if no SHA provided (backward compatibility with old bridges)
    if (!new_sha || !new_sha[0]) {
        return;
    }

    // Check if zones SHA changed
    bool sha_changed = (strcmp(s_last_zones_sha, new_sha) != 0);

    if (sha_changed) {
        LOGI("Zones SHA changed: '%s' -> '%s', refreshing zone list",
             s_last_zones_sha[0] ? s_last_zones_sha : "(empty)", new_sha);

        // Update cached SHA
        strncpy(s_last_zones_sha, new_sha, sizeof(s_last_zones_sha) - 1);
        s_last_zones_sha[sizeof(s_last_zones_sha) - 1] = '\0';

        // Re-fetch zones from bridge
        refresh_zone_label(true);
    }
}

static bool parse_optional_string(cJSON *object, const char *key, char *out,
                                  size_t out_size, uint32_t present_bit,
                                  controller_remote_preferences_t *prefs) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!value) {
        return true;
    }
    if (!cJSON_IsString(value) || !value->valuestring ||
        strlen(value->valuestring) >= out_size) {
        return false;
    }
    rk_strlcpy(out, value->valuestring, out_size);
    prefs->present |= present_bit;
    return true;
}

static bool parse_optional_number(cJSON *object, const char *key,
                                  uint32_t *out, uint32_t present_bit,
                                  controller_remote_preferences_t *prefs) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!value) {
        return true;
    }
    if (!cJSON_IsNumber(value) || value->valuedouble < 0 ||
        value->valuedouble > UINT32_MAX ||
        value->valuedouble != (double)value->valueint) {
        return false;
    }
    *out = (uint32_t)value->valueint;
    prefs->present |= present_bit;
    return true;
}

static bool parse_optional_bool(cJSON *object, const char *key, uint8_t *out,
                                uint32_t present_bit,
                                controller_remote_preferences_t *prefs) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!value) {
        return true;
    }
    if (!cJSON_IsBool(value)) {
        return false;
    }
    *out = cJSON_IsTrue(value) ? 1 : 0;
    prefs->present |= present_bit;
    return true;
}

static bool parse_enabled_timeout(cJSON *config, const char *key,
                                  uint8_t *enabled, uint32_t *timeout,
                                  uint32_t enabled_bit, uint32_t timeout_bit,
                                  controller_remote_preferences_t *prefs) {
    cJSON *object = cJSON_GetObjectItemCaseSensitive(config, key);
    if (!object) {
        return true;
    }
    return cJSON_IsObject(object) &&
           parse_optional_bool(object, "enabled", enabled, enabled_bit, prefs) &&
           parse_optional_number(object, "timeout_sec", timeout, timeout_bit,
                                 prefs);
}

static bool parse_remote_preferences(cJSON *root,
                                     controller_remote_preferences_t *prefs) {
    if (!root || !prefs || !cJSON_IsObject(root)) {
        return false;
    }
    *prefs = (controller_remote_preferences_t){0};
    cJSON *config = cJSON_GetObjectItemCaseSensitive(root, "config");
    if (!cJSON_IsObject(config) ||
        !parse_optional_string(root, "config_sha", prefs->config_sha,
                               sizeof(prefs->config_sha),
                               CONTROLLER_REMOTE_PREFERENCE_CONFIG_SHA, prefs) ||
        !parse_optional_string(config, "name", prefs->knob_name,
                               sizeof(prefs->knob_name),
                               CONTROLLER_REMOTE_PREFERENCE_KNOB_NAME, prefs) ||
        !parse_optional_number(config, "rotation_charging",
                               &prefs->rotation_charging,
                               CONTROLLER_REMOTE_PREFERENCE_ROTATION_CHARGING,
                               prefs) ||
        !parse_optional_number(config, "rotation_not_charging",
                               &prefs->rotation_not_charging,
                               CONTROLLER_REMOTE_PREFERENCE_ROTATION_NOT_CHARGING,
                               prefs) ||
        !parse_enabled_timeout(config, "art_mode_charging",
                               &prefs->art_mode_charging_enabled,
                               &prefs->art_mode_charging_timeout_sec,
                               CONTROLLER_REMOTE_PREFERENCE_ART_MODE_CHARGING_ENABLED,
                               CONTROLLER_REMOTE_PREFERENCE_ART_MODE_CHARGING_TIMEOUT,
                               prefs) ||
        !parse_enabled_timeout(config, "art_mode_battery",
                               &prefs->art_mode_battery_enabled,
                               &prefs->art_mode_battery_timeout_sec,
                               CONTROLLER_REMOTE_PREFERENCE_ART_MODE_BATTERY_ENABLED,
                               CONTROLLER_REMOTE_PREFERENCE_ART_MODE_BATTERY_TIMEOUT,
                               prefs) ||
        !parse_enabled_timeout(config, "dim_charging",
                               &prefs->dim_charging_enabled,
                               &prefs->dim_charging_timeout_sec,
                               CONTROLLER_REMOTE_PREFERENCE_DIM_CHARGING_ENABLED,
                               CONTROLLER_REMOTE_PREFERENCE_DIM_CHARGING_TIMEOUT,
                               prefs) ||
        !parse_enabled_timeout(config, "dim_battery",
                               &prefs->dim_battery_enabled,
                               &prefs->dim_battery_timeout_sec,
                               CONTROLLER_REMOTE_PREFERENCE_DIM_BATTERY_ENABLED,
                               CONTROLLER_REMOTE_PREFERENCE_DIM_BATTERY_TIMEOUT,
                               prefs) ||
        !parse_enabled_timeout(config, "sleep_charging",
                               &prefs->sleep_charging_enabled,
                               &prefs->sleep_charging_timeout_sec,
                               CONTROLLER_REMOTE_PREFERENCE_SLEEP_CHARGING_ENABLED,
                               CONTROLLER_REMOTE_PREFERENCE_SLEEP_CHARGING_TIMEOUT,
                               prefs) ||
        !parse_enabled_timeout(config, "sleep_battery",
                               &prefs->sleep_battery_enabled,
                               &prefs->sleep_battery_timeout_sec,
                               CONTROLLER_REMOTE_PREFERENCE_SLEEP_BATTERY_ENABLED,
                               CONTROLLER_REMOTE_PREFERENCE_SLEEP_BATTERY_TIMEOUT,
                               prefs) ||
        !parse_enabled_timeout(config, "deep_sleep_charging",
                               &prefs->deep_sleep_charging_enabled,
                               &prefs->deep_sleep_charging_timeout_sec,
                               CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_CHARGING_ENABLED,
                               CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_CHARGING_TIMEOUT,
                               prefs) ||
        !parse_enabled_timeout(config, "deep_sleep_battery",
                               &prefs->deep_sleep_battery_enabled,
                               &prefs->deep_sleep_battery_timeout_sec,
                               CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_BATTERY_ENABLED,
                               CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_BATTERY_TIMEOUT,
                               prefs) ||
        !parse_optional_bool(config, "wifi_power_save_enabled",
                             &prefs->wifi_power_save_enabled,
                             CONTROLLER_REMOTE_PREFERENCE_WIFI_POWER_SAVE_ENABLED,
                             prefs) ||
        !parse_optional_bool(config, "cpu_freq_scaling_enabled",
                             &prefs->cpu_freq_scaling_enabled,
                             CONTROLLER_REMOTE_PREFERENCE_CPU_FREQ_SCALING_ENABLED,
                             prefs) ||
        !parse_optional_number(config, "sleep_poll_stopped_sec",
                               &prefs->sleep_poll_stopped_sec,
                               CONTROLLER_REMOTE_PREFERENCE_SLEEP_POLL_STOPPED,
                               prefs)) {
        return false;
    }
    return prefs->present != 0;
}

static bool fetch_knob_config(void) {
    controller_config_endpoint_token_t token;
    if (!controller_config_capture_endpoint_token(&token)) {
        LOGW("fetch_knob_config: Controller configuration unavailable");
        return false;
    }
    char bridge_base[sizeof(token.bridge_base)] = {0};
    bridge_client_get_request_base(bridge_base, sizeof(bridge_base));
    if (bridge_base[0] == '\0') {
        LOGW("fetch_knob_config: No bridge configured");
        return false;
    }

    char knob_id[16];
    platform_http_get_knob_id(knob_id, sizeof(knob_id));
    char url[256];
    snprintf(url, sizeof(url), "%s/config/%s", bridge_base, knob_id);
    LOGI("Fetching config from %s", url);

    char *resp = NULL;
    size_t resp_len = 0;
    if (platform_http_get(url, &resp, &resp_len) != 0 || !resp) {
        LOGW("fetch_knob_config: HTTP request failed");
        platform_http_free(resp);
        return false;
    }
    cJSON *root = cJSON_Parse(resp);
    platform_http_free(resp);
    if (!root) {
        LOGW("fetch_knob_config: JSON parse failed");
        return false;
    }

    controller_remote_preferences_t preferences;
    bool parsed = parse_remote_preferences(root, &preferences);
    cJSON_Delete(root);
    if (!parsed) {
        LOGW("fetch_knob_config: rejected malformed remote preferences");
        return false;
    }

    controller_config_snapshot_t committed;
    controller_config_write_result_t result =
        controller_config_merge_remote_preferences_if_endpoint_current(
            &token, &preferences, &committed);
    if (result == CONTROLLER_CONFIG_NOT_COMMITTED) {
        LOGI("Ignoring stale or uncommitted remote preferences response");
        return false;
    }
    if (result == CONTROLLER_CONFIG_COMMITTED_UNVERIFIED) {
        post_unverified_config_diagnostic("Remote preferences");
    }
    apply_knob_config(&committed.value);
    LOGI("Config fetch %s: sha='%s'",
         result == CONTROLLER_CONFIG_COMMITTED_VERIFIED ? "complete"
                                                        : "applied unverified",
         committed.value.config_sha);
    return result == CONTROLLER_CONFIG_COMMITTED_VERIFIED;
}

// Check for charging state changes and reapply config if needed
static void check_charging_state_change(bool current_charging) {
    if (current_charging != s_last_charging_state) {
        LOGI("Charging state changed: %s -> %s",
             s_last_charging_state ? "charging" : "battery",
             current_charging ? "charging" : "battery");
        s_last_charging_state = current_charging;

        // Update battery indicator immediately (thread-safe post to UI task)
        post_ui_battery_update();

        // Reapply the authoritative copied configuration with new charging state.
        rk_cfg_t cfg;
        if (bridge_config_snapshot(&cfg)) {
            apply_knob_config(&cfg);
        }
    }
}

void bridge_client_connection_status(char *summary, size_t summary_len, char *details, size_t details_len) {
    controller_connection_t status;
    bridge_client_connection_snapshot(&status);
    if (summary && summary_len) controller_connection_summary(&status, summary, summary_len);
    if (details && details_len) controller_connection_details(&status, platform_millis(), details, details_len);
}

bool bridge_client_get_request_base(char *out, size_t len) {
    char zone[64];
    return bridge_endpoint_snapshot(out, len, zone, sizeof(zone)) && out[0];
}
