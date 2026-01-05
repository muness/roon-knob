#include "roon_client.h"

#include "platform/platform_display.h"
#include "platform/platform_http.h"
#include "platform/platform_log.h"
#include "platform/platform_mdns.h"
#include "platform/platform_storage.h"
#include "platform/platform_task.h"
#include "platform/platform_time.h"
#include "os_mutex.h"
#include "ui.h"

#ifdef ESP_PLATFORM
#include "display_sleep.h"
#endif

#include <ctype.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>

// Forward declarations for config handling
static bool fetch_knob_config(void);
static void apply_knob_config(const rk_cfg_t *cfg);
static void check_config_sha(const char *new_sha);
static void check_charging_state_change(void);

#define MAX_LINE 128
#define MAX_ZONE_NAME 64
#define MAX_ZONES 32
#define POLL_DELAY_AWAKE_CHARGING_MS 2000   // 2 seconds when charging and display on
#define POLL_DELAY_AWAKE_BATTERY_MS 5000   // 5 seconds on battery to save power
#define POLL_DELAY_SLEEPING_MS 30000       // 30 seconds when display is sleeping
#define POLL_DELAY_SLEEPING_STOPPED_MS 60000  // 60 seconds when sleeping AND zone stopped
#define POLL_DELAY_BRIDGE_ERROR_MS 10000   // 10 seconds when bridge unreachable

// Special zone picker options (not actual zones)
#define ZONE_ID_BACK "__back__"
#define ZONE_ID_SETTINGS "__settings__"

struct now_playing_state {
    char line1[MAX_LINE];
    char line2[MAX_LINE];
    bool is_playing;
    float volume;
    float volume_min;
    float volume_max;
    float volume_step;
    int seek_position;
    int length;
    char image_key[128];  // For tracking album artwork changes
    char config_sha[9];   // Config SHA for change detection
};

struct zone_entry {
    char id[MAX_ZONE_NAME];
    char name[MAX_ZONE_NAME];
};

struct roon_state {
    rk_cfg_t cfg;
    struct zone_entry zones[MAX_ZONES];
    int zone_count;
    char zone_label[MAX_ZONE_NAME];
    bool zone_resolved;
    bool net_connected;
};

static struct roon_state s_state;
static os_mutex_t s_state_lock = OS_MUTEX_INITIALIZER;
static bool s_running;
static bool s_trigger_poll;
static bool s_last_net_ok;
static bool s_network_ready;
static bool s_force_artwork_refresh;  // Force artwork reload on zone change
static float s_last_known_volume = 0.0f;   // Cached volume for optimistic UI updates
static float s_last_known_volume_min = -80.0f;  // Cached volume min for clamping
static float s_last_known_volume_max = 0.0f;    // Cached volume max for clamping
static float s_last_known_volume_step = 1.0f;  // Cached volume step
static bool s_bridge_verified = false;  // True after bridge found AND responded successfully
static uint32_t s_last_mdns_check_ms = 0;  // Timestamp of last mDNS check
static bool s_last_charging_state = true;  // Track charging state for config reapply
static bool s_last_is_playing = false;     // Track play state for extended sleep polling
#define MDNS_RECHECK_INTERVAL_MS (3600 * 1000)  // Re-check mDNS every hour if bridge stops responding

// Bridge connection retry tracking (mirrors WiFi retry pattern)
#define BRIDGE_FAIL_THRESHOLD 5  // Show recovery info after this many consecutive failures
#define MDNS_FAIL_THRESHOLD 10   // Show recovery info after this many mDNS failures (~30s)
static int s_bridge_fail_count = 0;
static int s_mdns_fail_count = 0;
static char s_device_ip[16] = {0};  // Device IP for recovery messages

static void lock_state(void) {
    os_mutex_lock(&s_state_lock);
}

static void unlock_state(void) {
    os_mutex_unlock(&s_state_lock);
}

static bool fetch_now_playing(struct now_playing_state *state);
static bool refresh_zone_label(bool prefer_zone_id);
static void parse_zones_from_response(const char *resp);
static const char *extract_json_string(const char *start, const char *key, char *out, size_t len);
static bool send_control_json(const char *json);
static void default_now_playing(struct now_playing_state *state);
static void wait_for_poll_interval(void);
static void roon_poll_thread(void *arg);
static bool host_is_valid(const char *url);
static void maybe_update_bridge_base(void);
static void post_ui_update(const struct now_playing_state *state);
static void post_ui_status(bool online);
static void post_ui_zone_name(const char *name);
static void post_ui_message(const char *msg);
static void post_ui_message_copy(char *msg_copy);
static void strip_trailing_slashes(char *url);
static void post_ui_status_copy(bool *status_copy);
static void post_ui_zone_name_copy(char *name_copy);
static void reset_bridge_fail_count(void);
static void increment_bridge_fail_count(void);

static void ui_update_cb(void *arg) {
    struct now_playing_state *state = arg;
    if (!state) {
        LOGI("ui_update_cb: state is NULL!");
        return;
    }
    // Cache volume for optimistic UI updates
    s_last_known_volume = state->volume;
    s_last_known_volume_min = state->volume_min;
    s_last_known_volume_max = state->volume_max;
    s_last_known_volume_step = state->volume_step;
    ui_update(state->line1, state->line2, state->is_playing, state->volume, state->volume_min, state->volume_max, state->volume_step, state->seek_position, state->length);

    // Update artwork if image_key changed or forced refresh
    static char last_image_key[128] = "";
    bool force_refresh = s_force_artwork_refresh;
    if (force_refresh) {
        s_force_artwork_refresh = false;
        last_image_key[0] = '\0';  // Clear cache to force reload
    }
    if (force_refresh || strcmp(state->image_key, last_image_key) != 0) {
        ui_set_artwork(state->image_key);
        strncpy(last_image_key, state->image_key, sizeof(last_image_key) - 1);
        last_image_key[sizeof(last_image_key) - 1] = '\0';
    }

    free(state);
}

static bool host_is_valid(const char *url) {
    // Accept any URL with a non-empty hostname (IP or mDNS name like rooExtend.localdomain)
    if (!url || !url[0]) return false;
    const char *host = url;
    const char *scheme = strstr(url, "://");
    if (scheme) host = scheme + 3;
    const char *end = host;
    while (*end && *end != ':' && *end != '/') ++end;
    return (end > host);
}

static void ui_status_cb(void *arg) {
    bool *online = arg;
    if (!online) {
        return;
    }
    ui_set_status(*online);
    free(online);
}

static void ui_message_cb(void *arg) {
    char *msg = arg;
    if (!msg) {
        return;
    }
    ui_set_message(msg);
    free(msg);
}

static void ui_zone_name_cb(void *arg) {
    char *name = arg;
    if (!name) {
        return;
    }
    ui_set_zone_name(name);
    free(name);
}

static void default_now_playing(struct now_playing_state *state) {
    if (!state) {
        return;
    }
    snprintf(state->line1, sizeof(state->line1), "Idle");
    state->line2[0] = '\0';
    state->is_playing = false;
    state->volume = 0.0f;
    state->volume_min = -80.0f;
    state->volume_max = 0.0f;
    state->volume_step = 0.0f;
    state->seek_position = 0;
    state->length = 0;
    state->image_key[0] = '\0';
    state->config_sha[0] = '\0';
}

static void post_ui_update(const struct now_playing_state *state) {
    struct now_playing_state *copy = malloc(sizeof(*copy));
    if (!copy || !state) {
        free(copy);
        return;
    }
    *copy = *state;
    platform_task_post_to_ui(ui_update_cb, copy);
}

static void post_ui_status_copy(bool *status_copy) {
    platform_task_post_to_ui(ui_status_cb, status_copy);
}

static void post_ui_status(bool online) {
    bool *copy = malloc(sizeof(*copy));
    if (!copy) {
        return;
    }
    *copy = online;
    post_ui_status_copy(copy);
}

static void post_ui_message_copy(char *msg_copy) {
    platform_task_post_to_ui(ui_message_cb, msg_copy);
}

static void post_ui_message(const char *msg) {
    if (!msg) {
        return;
    }
    char *copy = strdup(msg);
    if (!copy) {
        return;
    }
    post_ui_message_copy(copy);
}

static void post_ui_zone_name_copy(char *name_copy) {
    platform_task_post_to_ui(ui_zone_name_cb, name_copy);
}

static void post_ui_zone_name(const char *name) {
    if (!name) {
        return;
    }
    char *copy = strdup(name);
    if (!copy) {
        return;
    }
    post_ui_zone_name_copy(copy);
}

static void wait_for_poll_interval(void) {
    // Use longer delay when display is sleeping, on battery, or bridge unreachable
    uint32_t delay_ms;
    if (s_bridge_fail_count >= BRIDGE_FAIL_THRESHOLD) {
        delay_ms = POLL_DELAY_BRIDGE_ERROR_MS;  // Slow down when bridge unreachable
    } else if (platform_display_is_sleeping()) {
        // When sleeping AND zone not playing, use extended poll interval from config
        lock_state();
        uint16_t sleep_poll_stopped = s_state.cfg.sleep_poll_stopped_sec;
        unlock_state();
        if (!s_last_is_playing && sleep_poll_stopped > 0) {
            delay_ms = sleep_poll_stopped * 1000;  // Config is in seconds
        } else {
            delay_ms = POLL_DELAY_SLEEPING_MS;  // Default 30s when playing
        }
    } else if (platform_battery_is_charging()) {
        delay_ms = POLL_DELAY_AWAKE_CHARGING_MS;  // Fast polling when plugged in
    } else {
        delay_ms = POLL_DELAY_AWAKE_BATTERY_MS;   // Slower on battery to save power
    }
    uint64_t start = platform_millis();
    while (s_running) {
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

// Fallback bridge URL when mDNS discovery fails and no bridge is stored
#ifndef CONFIG_RK_DEFAULT_BRIDGE_BASE
#define CONFIG_RK_DEFAULT_BRIDGE_BASE "http://127.0.0.1:8088"
#endif

// Strip trailing slashes from URL to prevent double-slash issues
static void strip_trailing_slashes(char *url) {
    if (!url) return;
    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/') {
        url[--len] = '\0';
    }
}

static void maybe_update_bridge_base(void) {
    // Only use mDNS when no bridge URL is configured.
    // This respects user-set URLs (via web config) and allows Clear to trigger fresh discovery.
    lock_state();
    bool need_discovery = (s_state.cfg.bridge_base[0] == '\0');
    unlock_state();

    if (!need_discovery) {
        return;  // Bridge URL already configured - don't overwrite with mDNS
    }

    // Bridge is empty - try mDNS discovery
    char discovered[sizeof(s_state.cfg.bridge_base)];
    bool mdns_ok = platform_mdns_discover_base_url(discovered, sizeof(discovered));

    if (mdns_ok && host_is_valid(discovered)) {
        // mDNS found a bridge - save it
        s_mdns_fail_count = 0;
        lock_state();
        LOGI("mDNS discovered bridge: %s", discovered);
        strncpy(s_state.cfg.bridge_base, discovered, sizeof(s_state.cfg.bridge_base) - 1);
        s_state.cfg.bridge_base[sizeof(s_state.cfg.bridge_base) - 1] = '\0';
        strip_trailing_slashes(s_state.cfg.bridge_base);
        s_state.cfg.bridge_from_mdns = 1;  // Persist mDNS source
        platform_storage_save(&s_state.cfg);
        unlock_state();
        post_ui_message("Bridge: Found");
        return;
    }

    // mDNS failed - try compile-time default fallback
    if (CONFIG_RK_DEFAULT_BRIDGE_BASE[0] != '\0') {
        LOGI("mDNS discovery failed, using fallback: %s", CONFIG_RK_DEFAULT_BRIDGE_BASE);
        lock_state();
        strncpy(s_state.cfg.bridge_base, CONFIG_RK_DEFAULT_BRIDGE_BASE, sizeof(s_state.cfg.bridge_base) - 1);
        s_state.cfg.bridge_base[sizeof(s_state.cfg.bridge_base) - 1] = '\0';
        strip_trailing_slashes(s_state.cfg.bridge_base);
        // Don't save the fallback - let mDNS retry on next poll
        unlock_state();
    } else {
        // No fallback configured - increment mDNS failure counter
        if (s_mdns_fail_count < MDNS_FAIL_THRESHOLD) {
            s_mdns_fail_count++;
        }
        LOGW("mDNS discovery failed (%d/%d) - use Settings to configure bridge",
             s_mdns_fail_count, MDNS_FAIL_THRESHOLD);
    }
}

static bool fetch_now_playing(struct now_playing_state *state) {
    if (!state) {
        return false;
    }
    lock_state();
    char bridge_base[sizeof(s_state.cfg.bridge_base)];
    char zone_id[sizeof(s_state.cfg.zone_id)];
    strncpy(bridge_base, s_state.cfg.bridge_base, sizeof(bridge_base) - 1);
    strncpy(zone_id, s_state.cfg.zone_id, sizeof(zone_id) - 1);
    unlock_state();

    if (bridge_base[0] == '\0' || zone_id[0] == '\0') {
        LOGI("fetch_now_playing: bridge_base or zone_id empty (bridge_base='%s', zone_id='%s')", bridge_base, zone_id);
        return false;
    }

    // Get battery status for reporting to bridge
    int battery_level = platform_battery_get_level();
    bool battery_charging = platform_battery_is_charging();

    // Get knob ID for config_sha lookup
    char knob_id[16];
    platform_http_get_knob_id(knob_id, sizeof(knob_id));

    char url[384];
    snprintf(url, sizeof(url), "%s/now_playing?zone_id=%s&battery_level=%d&battery_charging=%d&knob_id=%s",
             bridge_base, zone_id, battery_level, battery_charging ? 1 : 0, knob_id);

    char *resp = NULL;
    size_t resp_len = 0;
    int ret = platform_http_get(url, &resp, &resp_len);
    if (ret != 0 || !resp) {
        platform_http_free(resp);
        return false;
    }

    if (strstr(resp, "\"error\"") || resp_len == 0) {
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
    state->is_playing = strstr(resp, "\"is_playing\":true") != NULL;

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
    } else {
        state->image_key[0] = '\0';  // No artwork available
    }

    // Parse config_sha for config change detection (silent - checked in poll loop)
    const char *config_sha_key = strstr(resp, "\"config_sha\"");
    if (config_sha_key) {
        extract_json_string(config_sha_key, "\"config_sha\"", state->config_sha, sizeof(state->config_sha));
    } else {
        state->config_sha[0] = '\0';
    }

    // Note: Don't parse zones from now_playing response - it doesn't have zone_name
    // Zones are parsed from /zones endpoint in refresh_zone_label()
    platform_http_free(resp);
    return true;
}

static bool refresh_zone_label(bool prefer_zone_id) {
    LOGI("refresh_zone_label: Called (prefer_zone_id=%s)", prefer_zone_id ? "true" : "false");
    lock_state();
    char bridge_base[sizeof(s_state.cfg.bridge_base)];
    strncpy(bridge_base, s_state.cfg.bridge_base, sizeof(bridge_base) - 1);
    unlock_state();
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
        platform_http_free(resp);
        return false;
    }

    LOGI("refresh_zone_label: Received %zu bytes", resp_len);
    parse_zones_from_response(resp);

    char zone_label_copy[MAX_ZONE_NAME] = {0};
    lock_state();
    LOGI("refresh_zone_label: Parsed %d zones", s_state.zone_count);
    if (s_state.zone_count > 0) {
        bool found = false;
        bool should_sync = false;
        for (int i = 0; i < s_state.zone_count; ++i) {
            struct zone_entry *entry = &s_state.zones[i];
            if (prefer_zone_id && s_state.cfg.zone_id[0] && strcmp(entry->id, s_state.cfg.zone_id) == 0) {
                strncpy(s_state.zone_label, entry->name, sizeof(s_state.zone_label) - 1);
                s_state.zone_label[sizeof(s_state.zone_label) - 1] = '\0';
                strncpy(zone_label_copy, s_state.zone_label, sizeof(zone_label_copy) - 1);
                found = true;
                should_sync = true;
                break;
            }
            if (!s_state.cfg.zone_id[0]) {
                strncpy(s_state.cfg.zone_id, entry->id, sizeof(s_state.cfg.zone_id) - 1);
                s_state.cfg.zone_id[sizeof(s_state.cfg.zone_id) - 1] = '\0';
                strncpy(s_state.zone_label, entry->name, sizeof(s_state.zone_label) - 1);
                s_state.zone_label[sizeof(s_state.zone_label) - 1] = '\0';
                strncpy(zone_label_copy, s_state.zone_label, sizeof(zone_label_copy) - 1);
                found = true;
                should_sync = true;
                break;
            }
        }
        if (!found && s_state.zone_count > 0) {
            struct zone_entry *entry = &s_state.zones[0];
            strncpy(s_state.cfg.zone_id, entry->id, sizeof(s_state.cfg.zone_id) - 1);
            s_state.cfg.zone_id[sizeof(s_state.cfg.zone_id) - 1] = '\0';
            strncpy(s_state.zone_label, entry->name, sizeof(s_state.zone_label) - 1);
            s_state.zone_label[sizeof(s_state.zone_label) - 1] = '\0';
            strncpy(zone_label_copy, s_state.zone_label, sizeof(zone_label_copy) - 1);
            should_sync = true;
        }
        s_state.zone_resolved = true;
        success = should_sync && zone_label_copy[0] != '\0';
    }
    unlock_state();

    platform_http_free(resp);
    if (success) {
        LOGI("refresh_zone_label: Selected zone '%s', posting to UI", zone_label_copy);
        platform_storage_save(&s_state.cfg);
        post_ui_zone_name(zone_label_copy);
    } else {
        LOGI("refresh_zone_label: No zone selected (success=false)");
    }
    return success;
}

static void parse_zones_from_response(const char *resp) {
    if (!resp) {
        return;
    }
    lock_state();
    s_state.zone_count = 0;
    const char *cursor = resp;
    while (s_state.zone_count < MAX_ZONES && (cursor = strstr(cursor, "\"zone_id\""))) {
        char id[MAX_ZONE_NAME] = {0};
        char name[MAX_ZONE_NAME] = {0};
        const char *next = extract_json_string(cursor, "\"zone_id\"", id, sizeof(id));
        if (!next) {
            break;
        }
        const char *after_name = extract_json_string(next, "\"zone_name\"", name, sizeof(name));
        if (!after_name) {
            cursor = next;
            continue;
        }
        strncpy(s_state.zones[s_state.zone_count].id, id, sizeof(s_state.zones[0].id) - 1);
        strncpy(s_state.zones[s_state.zone_count].name, name, sizeof(s_state.zones[0].name) - 1);
        s_state.zones[s_state.zone_count].id[sizeof(s_state.zones[0].id) - 1] = '\0';
        s_state.zones[s_state.zone_count].name[sizeof(s_state.zones[0].name) - 1] = '\0';
        s_state.zone_count++;
        cursor = after_name;
    }
    unlock_state();
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
    lock_state();
    char bridge_base[sizeof(s_state.cfg.bridge_base)];
    char zone_id[sizeof(s_state.cfg.zone_id)];
    strncpy(bridge_base, s_state.cfg.bridge_base, sizeof(bridge_base) - 1);
    strncpy(zone_id, s_state.cfg.zone_id, sizeof(zone_id) - 1);
    unlock_state();
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

static void roon_poll_thread(void *arg) {
    (void)arg;
    LOGI("Roon polling thread started");
    struct now_playing_state state;
    default_now_playing(&state);
    while (s_running) {
        // Skip HTTP requests if network is not ready yet (or in BLE mode)
        // In BLE mode, s_network_ready is false, so we just sleep without logging
        if (!s_network_ready) {
            wait_for_poll_interval();
            continue;
        }

        // Only run mDNS discovery if:
        // 1. We haven't verified a working bridge yet, OR
        // 2. It's been over an hour since last check (in case bridge IP changed)
        uint32_t now_ms = (uint32_t)platform_millis();
        bool should_check_mdns = !s_bridge_verified ||
            (now_ms - s_last_mdns_check_ms > MDNS_RECHECK_INTERVAL_MS);
        if (should_check_mdns) {
            maybe_update_bridge_base();
            s_last_mdns_check_ms = now_ms;
        }

        if (!s_state.zone_resolved) {
            refresh_zone_label(true);
        }
        bool ok = fetch_now_playing(&state);
        post_ui_status(ok);

        // Track play state for extended sleep polling
        if (ok) {
            s_last_is_playing = state.is_playing;
        }

        // Check for config changes and charging state (only when bridge is responding)
        if (ok) {
            check_config_sha(state.config_sha);
            check_charging_state_change();
        }

        // Handle bridge connection status (mirrors WiFi retry pattern)
        if (ok) {
            // Bridge connected - show now playing data
            post_ui_update(&state);
            if (!s_last_net_ok) {
                // Just connected - clear status, restore zone name, mark verified
                reset_bridge_fail_count();
                post_ui_message("Bridge: Connected");
                ui_set_network_status(NULL);
                s_bridge_verified = true;
                // Restore zone name (was cleared during error display)
                lock_state();
                char zone_name_copy[MAX_ZONE_NAME];
                strncpy(zone_name_copy, s_state.zone_label, sizeof(zone_name_copy) - 1);
                zone_name_copy[sizeof(zone_name_copy) - 1] = '\0';
                unlock_state();
                if (zone_name_copy[0]) {
                    ui_set_zone_name(zone_name_copy);
                }
            }
        } else if (!ok && s_last_net_ok) {
            // Just lost connection to bridge - start retry tracking
            // line1=main content (bottom), line2=header (top)
            increment_bridge_fail_count();
            s_bridge_verified = false;
            char line1_msg[64];
            snprintf(line1_msg, sizeof(line1_msg), "Attempt %d of %d...",
                     s_bridge_fail_count, BRIDGE_FAIL_THRESHOLD);
            ui_set_zone_name("");  // Clear zone name to avoid overlay
            ui_update(line1_msg, "Testing Bridge", false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
            ui_set_network_status("Bridge: Offline - retrying...");
        } else if (!ok && !s_last_net_ok) {
            // Still trying to connect - check if we have a bridge URL
            lock_state();
            bool has_bridge = (s_state.cfg.bridge_base[0] != '\0');
            unlock_state();

            if (!has_bridge) {
                // No bridge URL - searching via mDNS
                // Show retry progress or recovery info based on failure count
                char line1_msg[64];
                char line2_msg[64];
                char status_msg[96];
                ui_set_zone_name("");  // Clear zone name to avoid overlay

                if (s_mdns_fail_count >= MDNS_FAIL_THRESHOLD) {
                    // mDNS search exhausted - show recovery info
                    if (s_device_ip[0]) {
                        snprintf(line1_msg, sizeof(line1_msg), "http://%s", s_device_ip);
                        snprintf(line2_msg, sizeof(line2_msg), "Set Bridge URL at:");
                        snprintf(status_msg, sizeof(status_msg),
                                 "mDNS failed. Set Bridge at http://%s", s_device_ip);
                    } else {
                        snprintf(line1_msg, sizeof(line1_msg), "Use zone menu > Settings");
                        snprintf(line2_msg, sizeof(line2_msg), "Bridge Not Found");
                        snprintf(status_msg, sizeof(status_msg),
                                 "mDNS failed. Configure Bridge in Settings.");
                    }
                    ui_update(line1_msg, line2_msg, false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
                    ui_set_network_status(status_msg);
                } else {
                    // Still searching - show progress
                    snprintf(line1_msg, sizeof(line1_msg), "Attempt %d of %d...",
                             s_mdns_fail_count + 1, MDNS_FAIL_THRESHOLD);
                    ui_update(line1_msg, "Searching for Bridge", false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
                    snprintf(status_msg, sizeof(status_msg), "mDNS: %d/%d",
                             s_mdns_fail_count + 1, MDNS_FAIL_THRESHOLD);
                    ui_set_network_status(status_msg);
                }
            } else {
                // Bridge URL configured but not responding - show retry progress
                increment_bridge_fail_count();
                char line1_msg[64];
                char status_msg[96];

                if (s_bridge_fail_count >= BRIDGE_FAIL_THRESHOLD) {
                    // Max retries reached - show recovery info with device IP
                    // line1=main content (bottom), line2=header (top)
                    char line2_msg[64];
                    if (s_device_ip[0]) {
                        snprintf(line1_msg, sizeof(line1_msg),
                                 "http://%s", s_device_ip);
                        snprintf(line2_msg, sizeof(line2_msg), "Update Bridge at:");
                        snprintf(status_msg, sizeof(status_msg),
                                 "Bridge unreachable after %d attempts", BRIDGE_FAIL_THRESHOLD);
                    } else {
                        snprintf(line1_msg, sizeof(line1_msg), "Use zone menu > Settings");
                        snprintf(line2_msg, sizeof(line2_msg), "Bridge Unreachable");
                        snprintf(status_msg, sizeof(status_msg),
                                 "Bridge unreachable. Check Settings.");
                    }
                    ui_set_zone_name("");  // Clear zone name to avoid overlay
                    ui_update(line1_msg, line2_msg, false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
                    ui_set_network_status(status_msg);
                } else {
                    // Still retrying - show progress on main display
                    // line1=main content (bottom), line2=header (top)
                    snprintf(line1_msg, sizeof(line1_msg), "Attempt %d of %d...",
                             s_bridge_fail_count, BRIDGE_FAIL_THRESHOLD);
                    ui_set_zone_name("");  // Clear zone name to avoid overlay
                    ui_update(line1_msg, "Testing Bridge", false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
                    snprintf(status_msg, sizeof(status_msg), "Bridge: Retry %d/%d",
                             s_bridge_fail_count, BRIDGE_FAIL_THRESHOLD);
                    ui_set_network_status(status_msg);
                }
            }
        }
        s_last_net_ok = ok;
        wait_for_poll_interval();
    }
}

void roon_client_start(const rk_cfg_t *cfg) {
    if (!cfg) {
        return;
    }
    platform_task_init();
    lock_state();
    s_state.cfg = *cfg;
    strncpy(s_state.zone_label, cfg->zone_id[0] ? cfg->zone_id : "Tap here to select zone", sizeof(s_state.zone_label) - 1);
    s_state.zone_label[sizeof(s_state.zone_label) - 1] = '\0';
    unlock_state();

    // Always apply config on startup (uses defaults if no saved config)
    // This ensures rotation is applied even on fresh devices
    LOGI("Applying config on startup: rot=%d/%d sha='%s'",
         cfg->rotation_charging, cfg->rotation_not_charging,
         cfg->config_sha[0] ? cfg->config_sha : "(none)");
    apply_knob_config(cfg);

    s_running = true;
    platform_task_start(roon_poll_thread, NULL);
}

void roon_client_handle_input(ui_input_event_t event) {
    if (ui_is_zone_picker_visible()) {
        if (event == UI_INPUT_VOL_UP) {
            ui_zone_picker_scroll(1);
            return;
        }
        if (event == UI_INPUT_VOL_DOWN) {
            ui_zone_picker_scroll(-1);
            return;
        }
        if (event == UI_INPUT_PLAY_PAUSE) {
            // Get the selected zone ID directly from the picker
            char selected_id[MAX_ZONE_NAME] = {0};
            ui_zone_picker_get_selected_id(selected_id, sizeof(selected_id));
            LOGI("Zone picker: selected zone id '%s'", selected_id);

            // Check for Back (always a no-op, just closes picker)
            if (strcmp(selected_id, ZONE_ID_BACK) == 0) {
                LOGI("Zone picker: Back selected (no-op)");
                ui_hide_zone_picker();
                return;
            }

            // Check for Settings
            if (strcmp(selected_id, ZONE_ID_SETTINGS) == 0) {
                LOGI("Zone picker: Settings selected");
                ui_hide_zone_picker();
                ui_show_settings();
                return;
            }

            // Check if user selected the same zone they started with (no-op)
            if (ui_zone_picker_is_current_selection()) {
                LOGI("Zone picker: Same zone selected (no-op)");
                ui_hide_zone_picker();
                return;
            }

            // Zone selection
            char label_copy[MAX_ZONE_NAME] = {0};
            bool updated = false;
            lock_state();
            // Find the zone by ID to get its name
            for (int i = 0; i < s_state.zone_count; ++i) {
                struct zone_entry *entry = &s_state.zones[i];
                if (strcmp(entry->id, selected_id) == 0) {
                    LOGI("Zone picker: switching to zone '%s' (id=%s)", entry->name, entry->id);
                    strncpy(s_state.cfg.zone_id, entry->id, sizeof(s_state.cfg.zone_id) - 1);
                    s_state.cfg.zone_id[sizeof(s_state.cfg.zone_id) - 1] = '\0';
                    strncpy(s_state.zone_label, entry->name, sizeof(s_state.zone_label) - 1);
                    s_state.zone_label[sizeof(s_state.zone_label) - 1] = '\0';
                    strncpy(label_copy, s_state.zone_label, sizeof(label_copy) - 1);
                    s_state.zone_resolved = true;
                    s_trigger_poll = true;
                    s_force_artwork_refresh = true;  // Force artwork reload for new zone
                    updated = true;
                    break;
                }
            }
            if (!updated) {
                LOGW("Zone picker: zone id '%s' not found in zone list", selected_id);
            }
            unlock_state();
            // Hide picker FIRST to ensure it closes before any async operations
            ui_hide_zone_picker();
            if (updated) {
                platform_storage_save(&s_state.cfg);
                post_ui_zone_name(label_copy);
                post_ui_message("Loading zone...");
            }
            return;
        }
        if (event == UI_INPUT_MENU) {
            ui_hide_zone_picker();
            return;
        }
        return;
    }

    if (event == UI_INPUT_MENU) {
        const char *names[MAX_ZONES + 3];  /* +3 for Back, Settings, margin */
        const char *ids[MAX_ZONES + 3];
        static const char *back_name = "Back";
        static const char *back_id = ZONE_ID_BACK;
        static const char *settings_name = "Settings";
        static const char *settings_id = ZONE_ID_SETTINGS;
        int selected = 1;  /* Default to first zone after Back */
        int count = 0;

        /* Add Back as first option */
        names[count] = back_name;
        ids[count] = back_id;
        count++;

        lock_state();
        if (s_state.zone_count > 0) {
            for (int i = 0; i < s_state.zone_count && count < MAX_ZONES + 2; ++i) {
                names[count] = s_state.zones[i].name;
                ids[count] = s_state.zones[i].id;
                if (strcmp(s_state.zones[i].id, s_state.cfg.zone_id) == 0) {
                    selected = count;
                }
                count++;
            }
        }
        unlock_state();

        /* Add Settings as last option */
        names[count] = settings_name;
        ids[count] = settings_id;
        count++;

        ui_show_zone_picker(names, ids, count, selected);
        return;
    }

    char body[256];
    switch (event) {
    case UI_INPUT_VOL_DOWN: {
        lock_state();
        float predicted_down = s_last_known_volume - s_last_known_volume_step;
        if (predicted_down < s_last_known_volume_min) {
            predicted_down = s_last_known_volume_min;
        }
        s_last_known_volume = predicted_down;
        snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"vol_abs\",\"value\":%.10g}",
            s_state.cfg.zone_id, predicted_down);
        unlock_state();
        ui_show_volume_change(predicted_down, s_last_known_volume_step);
        if (!send_control_json(body)) {
            post_ui_message("Volume change failed");
        }
        break;
    }
    case UI_INPUT_VOL_UP: {
        lock_state();
        float predicted_up = s_last_known_volume + s_last_known_volume_step;
        if (predicted_up > s_last_known_volume_max) {
            predicted_up = s_last_known_volume_max;
        }
        s_last_known_volume = predicted_up;
        snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"vol_abs\",\"value\":%.10g}",
            s_state.cfg.zone_id, predicted_up);
        unlock_state();
        ui_show_volume_change(predicted_up, s_last_known_volume_step);
        if (!send_control_json(body)) {
            post_ui_message("Volume change failed");
        }
        break;
    }
    case UI_INPUT_PLAY_PAUSE:
        lock_state();
        snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"play_pause\"}", s_state.cfg.zone_id);
        unlock_state();
        if (!send_control_json(body)) {
            post_ui_message("Play/pause failed");
        }
        break;
    case UI_INPUT_NEXT_TRACK:
        lock_state();
        snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"next\"}", s_state.cfg.zone_id);
        unlock_state();
        if (!send_control_json(body)) {
            post_ui_message("Next track failed");
        }
        break;
    case UI_INPUT_PREV_TRACK:
        lock_state();
        snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"prev\"}", s_state.cfg.zone_id);
        unlock_state();
        if (!send_control_json(body)) {
            post_ui_message("Previous track failed");
        }
        break;
    default:
        break;
    }
}

// Velocity-sensitive volume rotation handler
// Maps encoder tick count over 50ms window to step size:
//   1 tick = slow (step 1)
//   2 ticks = medium (step 3)
//   3+ ticks = fast (step 5)
void roon_client_handle_volume_rotation(int ticks) {
    if (ticks == 0) return;

    // Determine velocity multiplier (applied to zone's step)
    int abs_ticks = ticks < 0 ? -ticks : ticks;
    int step_multiplier;
    if (abs_ticks >= 3) {
        step_multiplier = 5;  // Fast rotation
    } else if (abs_ticks >= 2) {
        step_multiplier = 3;  // Medium rotation
    } else {
        step_multiplier = 1;  // Slow rotation (fine-grained control)
    }

    // Calculate optimistic new volume with clamping (inside lock for consistency)
    lock_state();
    float delta = step_multiplier * s_last_known_volume_step;
    float predicted_vol = s_last_known_volume + (ticks > 0 ? delta : -delta);
    if (predicted_vol < s_last_known_volume_min) {
        predicted_vol = s_last_known_volume_min;
    }
    if (predicted_vol > s_last_known_volume_max) {
        predicted_vol = s_last_known_volume_max;
    }

    // Update cached volume immediately for next rotation (optimistic tracking)
    s_last_known_volume = predicted_vol;

    // Send volume request to Roon (absolute value for exact match with optimistic UI)
    char body[256];
    snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"vol_abs\",\"value\":%.1f}",
        s_state.cfg.zone_id, predicted_vol);
    unlock_state();

    // Show volume overlay immediately with predicted value (optimistic UI)
    ui_show_volume_change(predicted_vol, s_last_known_volume_step);

    if (!send_control_json(body)) {
        post_ui_message("Volume change failed");
    }
}

void roon_client_set_network_ready(bool ready) {
    s_network_ready = ready;
    if (ready) {
        LOGI("Network ready - HTTP requests enabled");
        s_trigger_poll = true;  // Trigger immediate poll when network becomes ready
    } else {
        LOGI("Network not ready - HTTP requests disabled");
    }
}

const char* roon_client_get_artwork_url(char *url_buf, size_t buf_len, int width, int height) {
    if (!url_buf || buf_len < 256) {
        return NULL;
    }

    lock_state();
    const char *bridge_base = s_state.cfg.bridge_base;
    const char *zone_id = s_state.cfg.zone_id;

    if (!bridge_base || !bridge_base[0] || !zone_id || !zone_id[0]) {
        unlock_state();
        return NULL;
    }

    snprintf(url_buf, buf_len,
             "%s/now_playing/image?zone_id=%s&scale=fit&width=%d&height=%d",
             bridge_base, zone_id, width, height);
    unlock_state();

    return url_buf;
}

bool roon_client_is_ready_for_art_mode(void) {
    lock_state();
    bool ready = s_state.zone_count > 0;
    unlock_state();
    return ready;
}

// Bridge retry tracking functions
static void reset_bridge_fail_count(void) {
    s_bridge_fail_count = 0;
}

static void increment_bridge_fail_count(void) {
    if (s_bridge_fail_count < BRIDGE_FAIL_THRESHOLD) {
        s_bridge_fail_count++;
    }
}

void roon_client_set_device_ip(const char *ip) {
    if (ip && ip[0]) {
        strncpy(s_device_ip, ip, sizeof(s_device_ip) - 1);
        s_device_ip[sizeof(s_device_ip) - 1] = '\0';
    } else {
        s_device_ip[0] = '\0';
    }
}

int roon_client_get_bridge_retry_count(void) {
    return s_bridge_fail_count;
}

int roon_client_get_bridge_retry_max(void) {
    return BRIDGE_FAIL_THRESHOLD;
}

bool roon_client_get_bridge_url(char *buf, size_t len) {
    if (!buf || len == 0) {
        return false;
    }
    lock_state();
    bool has_bridge = (s_state.cfg.bridge_base[0] != '\0');
    if (has_bridge) {
        strncpy(buf, s_state.cfg.bridge_base, len - 1);
        buf[len - 1] = '\0';
    } else {
        buf[0] = '\0';
    }
    unlock_state();
    return has_bridge;
}

bool roon_client_is_bridge_connected(void) {
    return s_last_net_ok;
}

bool roon_client_is_bridge_mdns(void) {
    lock_state();
    bool from_mdns = (s_state.cfg.bridge_from_mdns != 0);
    unlock_state();
    return from_mdns;
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

#ifdef ESP_PLATFORM
    display_update_timeouts(&data->cfg, data->is_charging);
    display_update_power_settings(&data->cfg);
#endif

    LOGI("Config applied on UI thread: rotation=%d", data->rotation);
    free(data);
}

static void apply_knob_config(const rk_cfg_t *cfg) {
    if (!cfg) {
        return;
    }

    // Get current charging state
    bool is_charging = platform_battery_is_charging();
    uint16_t rotation = rk_cfg_get_rotation(cfg, is_charging);

    LOGI("Config apply requested: name='%s' rotation=%d (charging=%s)",
         cfg->knob_name[0] ? cfg->knob_name : "(unnamed)",
         rotation, is_charging ? "yes" : "no");

    // Post to UI thread since LVGL is not thread-safe
    struct apply_config_ui_data *data = malloc(sizeof(*data));
    if (data) {
        data->rotation = rotation;
        data->is_charging = is_charging;
        data->cfg = *cfg;
        platform_task_post_to_ui(apply_config_on_ui_thread, data);
    }
}

static void check_config_sha(const char *new_sha) {
    if (!new_sha || !new_sha[0]) {
        return;
    }

    lock_state();
    bool sha_changed = (strcmp(s_state.cfg.config_sha, new_sha) != 0);
    unlock_state();

    if (sha_changed) {
        LOGI("Config SHA changed: '%s' -> '%s', fetching new config",
             s_state.cfg.config_sha[0] ? s_state.cfg.config_sha : "(empty)", new_sha);
        fetch_knob_config();
    }
}

static bool fetch_knob_config(void) {
    lock_state();
    char bridge_base[sizeof(s_state.cfg.bridge_base)];
    strncpy(bridge_base, s_state.cfg.bridge_base, sizeof(bridge_base) - 1);
    bridge_base[sizeof(bridge_base) - 1] = '\0';
    unlock_state();

    if (bridge_base[0] == '\0') {
        LOGW("fetch_knob_config: No bridge configured");
        return false;
    }

    // Get knob ID
    char knob_id[16];
    platform_http_get_knob_id(knob_id, sizeof(knob_id));

    // Build URL
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

    // Parse JSON response using cJSON
    cJSON *root = cJSON_Parse(resp);
    platform_http_free(resp);

    if (!root) {
        LOGW("fetch_knob_config: JSON parse failed");
        return false;
    }

    // Extract config fields
    lock_state();
    rk_cfg_t *cfg = &s_state.cfg;

    // config_sha (at root level)
    cJSON *sha = cJSON_GetObjectItem(root, "config_sha");
    if (cJSON_IsString(sha) && sha->valuestring) {
        strncpy(cfg->config_sha, sha->valuestring, sizeof(cfg->config_sha) - 1);
        cfg->config_sha[sizeof(cfg->config_sha) - 1] = '\0';
    }

    // Get nested config object - all config fields are inside "config"
    cJSON *config_obj = cJSON_GetObjectItem(root, "config");
    if (!cJSON_IsObject(config_obj)) {
        LOGW("fetch_knob_config: missing 'config' object in response");
        unlock_state();
        cJSON_Delete(root);
        return false;
    }

    // name (inside config object)
    cJSON *name = cJSON_GetObjectItem(config_obj, "name");
    if (cJSON_IsString(name) && name->valuestring) {
        strncpy(cfg->knob_name, name->valuestring, sizeof(cfg->knob_name) - 1);
        cfg->knob_name[sizeof(cfg->knob_name) - 1] = '\0';
    }

    // rotation
    cJSON *rot_charging = cJSON_GetObjectItem(config_obj, "rotation_charging");
    if (cJSON_IsNumber(rot_charging)) {
        cfg->rotation_charging = (uint16_t)rot_charging->valueint;
    }
    cJSON *rot_battery = cJSON_GetObjectItem(config_obj, "rotation_not_charging");
    if (cJSON_IsNumber(rot_battery)) {
        cfg->rotation_not_charging = (uint16_t)rot_battery->valueint;
    }

    // art_mode_charging
    cJSON *art_charging = cJSON_GetObjectItem(config_obj, "art_mode_charging");
    if (cJSON_IsObject(art_charging)) {
        cJSON *enabled = cJSON_GetObjectItem(art_charging, "enabled");
        if (cJSON_IsBool(enabled)) {
            cfg->art_mode_charging_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
        }
        cJSON *timeout = cJSON_GetObjectItem(art_charging, "timeout_sec");
        if (cJSON_IsNumber(timeout)) {
            cfg->art_mode_charging_timeout_sec = (uint16_t)timeout->valueint;
        }
    }

    // art_mode_battery
    cJSON *art_battery = cJSON_GetObjectItem(config_obj, "art_mode_battery");
    if (cJSON_IsObject(art_battery)) {
        cJSON *enabled = cJSON_GetObjectItem(art_battery, "enabled");
        if (cJSON_IsBool(enabled)) {
            cfg->art_mode_battery_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
        }
        cJSON *timeout = cJSON_GetObjectItem(art_battery, "timeout_sec");
        if (cJSON_IsNumber(timeout)) {
            cfg->art_mode_battery_timeout_sec = (uint16_t)timeout->valueint;
        }
    }

    // dim_charging
    cJSON *dim_charging = cJSON_GetObjectItem(config_obj, "dim_charging");
    if (cJSON_IsObject(dim_charging)) {
        cJSON *enabled = cJSON_GetObjectItem(dim_charging, "enabled");
        if (cJSON_IsBool(enabled)) {
            cfg->dim_charging_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
        }
        cJSON *timeout = cJSON_GetObjectItem(dim_charging, "timeout_sec");
        if (cJSON_IsNumber(timeout)) {
            cfg->dim_charging_timeout_sec = (uint16_t)timeout->valueint;
        }
    }

    // dim_battery
    cJSON *dim_battery = cJSON_GetObjectItem(config_obj, "dim_battery");
    if (cJSON_IsObject(dim_battery)) {
        cJSON *enabled = cJSON_GetObjectItem(dim_battery, "enabled");
        if (cJSON_IsBool(enabled)) {
            cfg->dim_battery_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
        }
        cJSON *timeout = cJSON_GetObjectItem(dim_battery, "timeout_sec");
        if (cJSON_IsNumber(timeout)) {
            cfg->dim_battery_timeout_sec = (uint16_t)timeout->valueint;
        }
    }

    // sleep_charging
    cJSON *sleep_charging = cJSON_GetObjectItem(config_obj, "sleep_charging");
    if (cJSON_IsObject(sleep_charging)) {
        cJSON *enabled = cJSON_GetObjectItem(sleep_charging, "enabled");
        if (cJSON_IsBool(enabled)) {
            cfg->sleep_charging_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
        }
        cJSON *timeout = cJSON_GetObjectItem(sleep_charging, "timeout_sec");
        if (cJSON_IsNumber(timeout)) {
            cfg->sleep_charging_timeout_sec = (uint16_t)timeout->valueint;
        }
    }

    // sleep_battery
    cJSON *sleep_battery = cJSON_GetObjectItem(config_obj, "sleep_battery");
    if (cJSON_IsObject(sleep_battery)) {
        cJSON *enabled = cJSON_GetObjectItem(sleep_battery, "enabled");
        if (cJSON_IsBool(enabled)) {
            cfg->sleep_battery_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
        }
        cJSON *timeout = cJSON_GetObjectItem(sleep_battery, "timeout_sec");
        if (cJSON_IsNumber(timeout)) {
            cfg->sleep_battery_timeout_sec = (uint16_t)timeout->valueint;
        }
    }

    // Power management settings
    cJSON *wifi_ps = cJSON_GetObjectItem(config_obj, "wifi_power_save_enabled");
    if (cJSON_IsBool(wifi_ps)) {
        cfg->wifi_power_save_enabled = cJSON_IsTrue(wifi_ps) ? 1 : 0;
    }
    cJSON *cpu_freq = cJSON_GetObjectItem(config_obj, "cpu_freq_scaling_enabled");
    if (cJSON_IsBool(cpu_freq)) {
        cfg->cpu_freq_scaling_enabled = cJSON_IsTrue(cpu_freq) ? 1 : 0;
    }
    cJSON *sleep_poll = cJSON_GetObjectItem(config_obj, "sleep_poll_stopped_sec");
    if (cJSON_IsNumber(sleep_poll)) {
        cfg->sleep_poll_stopped_sec = (uint16_t)sleep_poll->valueint;
    }

    // Log parsed config values
    LOGI("Config parsed: rot=%d/%d art=%d/%ds|%d/%ds dim=%d/%ds|%d/%ds sleep=%d/%ds|%d/%ds",
         cfg->rotation_charging, cfg->rotation_not_charging,
         cfg->art_mode_charging_enabled, cfg->art_mode_charging_timeout_sec,
         cfg->art_mode_battery_enabled, cfg->art_mode_battery_timeout_sec,
         cfg->dim_charging_enabled, cfg->dim_charging_timeout_sec,
         cfg->dim_battery_enabled, cfg->dim_battery_timeout_sec,
         cfg->sleep_charging_enabled, cfg->sleep_charging_timeout_sec,
         cfg->sleep_battery_enabled, cfg->sleep_battery_timeout_sec);
    LOGI("Power config: wifi_ps=%d cpu_scale=%d sleep_poll_stopped=%ds",
         cfg->wifi_power_save_enabled, cfg->cpu_freq_scaling_enabled,
         cfg->sleep_poll_stopped_sec);

    // Make a copy for apply and save
    rk_cfg_t cfg_copy = *cfg;
    unlock_state();

    cJSON_Delete(root);

    // Save to NVS
    platform_storage_save(&cfg_copy);

    // Apply the config
    apply_knob_config(&cfg_copy);

    LOGI("Config fetch complete: sha='%s'", cfg_copy.config_sha);
    return true;
}

// Check for charging state changes and reapply config if needed
static void check_charging_state_change(void) {
    bool current_charging = platform_battery_is_charging();
    if (current_charging != s_last_charging_state) {
        LOGI("Charging state changed: %s -> %s",
             s_last_charging_state ? "charging" : "battery",
             current_charging ? "charging" : "battery");
        s_last_charging_state = current_charging;

        // Reapply config with new charging state
        lock_state();
        rk_cfg_t cfg_copy = s_state.cfg;
        unlock_state();
        apply_knob_config(&cfg_copy);
    }
}
