#include "roon_client.h"

#include "controller_mode.h"
#include "platform/platform_display.h"
#include "platform/platform_http.h"
#include "platform/platform_log.h"
#include "platform/platform_mdns.h"
#include "platform/platform_storage.h"
#include "platform/platform_task.h"
#include "platform/platform_time.h"
#include "os_mutex.h"
#include "ui.h"

#include <ctype.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 128
#define MAX_ZONE_NAME 64
#define MAX_ZONES 32
#define POLL_DELAY_AWAKE_MS 1000     // 1 second when display is on
#define POLL_DELAY_SLEEPING_MS 30000  // 30 seconds when display is sleeping

// Special zone picker options (not actual zones)
#define ZONE_ID_BACK "__back__"
#define ZONE_ID_SETTINGS "__settings__"

struct now_playing_state {
    char line1[MAX_LINE];
    char line2[MAX_LINE];
    bool is_playing;
    int volume;
    int volume_min;
    int volume_max;
    int volume_step;
    int seek_position;
    int length;
    char image_key[128];  // For tracking album artwork changes
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
static int s_last_known_volume = 0;   // Cached volume for optimistic UI updates

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
static bool host_is_numeric_ip(const char *url);
static bool host_is_numeric_ip(const char *url);
static void maybe_update_bridge_base(void);
static void post_ui_update(const struct now_playing_state *state);
static void post_ui_status(bool online);
static void post_ui_zone_name(const char *name);
static void post_ui_message(const char *msg);
static void post_ui_message_copy(char *msg_copy);
static void post_ui_status_copy(bool *status_copy);
static void post_ui_zone_name_copy(char *name_copy);

static void ui_update_cb(void *arg) {
    struct now_playing_state *state = arg;
    if (!state) {
        LOGI("ui_update_cb: state is NULL!");
        return;
    }
    // Cache volume for optimistic UI updates
    s_last_known_volume = state->volume;
    ui_update(state->line1, state->line2, state->is_playing, state->volume, state->volume_min, state->volume_max, state->seek_position, state->length);

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

static bool host_is_numeric_ip(const char *url) {
    if (!url || !url[0]) {
        return false;
    }
    const char *host = url;
    const char *scheme = strstr(url, "://");
    if (scheme) {
        host = scheme + 3;
    }
    const char *end = host;
    while (*end && *end != ':' && *end != '/' && *end != '\0') {
        ++end;
    }
    if (end == host) {
        return false;
    }
    bool has_digit = false;
    for (const char *p = host; p < end; ++p) {
        if (*p == '.') {
            continue;
        }
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
        has_digit = true;
    }
    return has_digit;
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
    state->volume = 0;
    state->volume_min = -80;
    state->volume_max = 0;
    state->volume_step = 0;
    state->seek_position = 0;
    state->length = 0;
    state->image_key[0] = '\0';
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
    // Use longer delay when display is sleeping to save power
    uint32_t delay_ms = platform_display_is_sleeping() ? POLL_DELAY_SLEEPING_MS : POLL_DELAY_AWAKE_MS;
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

static void maybe_update_bridge_base(void) {
    char discovered[sizeof(s_state.cfg.bridge_base)];
    bool mdns_ok = platform_mdns_discover_base_url(discovered, sizeof(discovered));

    if (mdns_ok && host_is_numeric_ip(discovered)) {
        // mDNS found a bridge - update if different
        lock_state();
        bool is_new = (strcmp(s_state.cfg.bridge_base, discovered) != 0);
        if (is_new) {
            LOGI("mDNS discovered bridge: %s", discovered);
            strncpy(s_state.cfg.bridge_base, discovered, sizeof(s_state.cfg.bridge_base) - 1);
            s_state.cfg.bridge_base[sizeof(s_state.cfg.bridge_base) - 1] = '\0';
            platform_storage_save(&s_state.cfg);
        }
        unlock_state();
        if (is_new) {
            post_ui_message("Bridge: Found");
        }
        return;
    }

    // mDNS failed or returned non-numeric host
    if (mdns_ok) {
        LOGW("mDNS returned non-numeric host: %s (ignoring)", discovered);
    }

    // If no bridge is configured yet, check for compile-time default
    lock_state();
    bool need_default = (s_state.cfg.bridge_base[0] == '\0');
    unlock_state();

    if (need_default) {
        // Check if there's a compile-time fallback configured
        if (CONFIG_RK_DEFAULT_BRIDGE_BASE[0] != '\0') {
            LOGI("mDNS discovery failed, using fallback: %s", CONFIG_RK_DEFAULT_BRIDGE_BASE);
            lock_state();
            strncpy(s_state.cfg.bridge_base, CONFIG_RK_DEFAULT_BRIDGE_BASE, sizeof(s_state.cfg.bridge_base) - 1);
            s_state.cfg.bridge_base[sizeof(s_state.cfg.bridge_base) - 1] = '\0';
            // Don't save the fallback - let mDNS retry on next poll
            unlock_state();
        } else {
            // No fallback configured - user needs to configure bridge manually
            LOGW("mDNS discovery failed and no fallback configured - use Settings to configure bridge");
        }
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

    char url[256];
    snprintf(url, sizeof(url), "%s/now_playing?zone_id=%s", bridge_base, zone_id);

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
            state->volume = atoi(colon + 1);
        }
    }

    const char *vol_min_key = strstr(resp, "\"volume_min\"");
    if (vol_min_key) {
        const char *colon = strchr(vol_min_key, ':');
        if (colon) {
            state->volume_min = atoi(colon + 1);
        }
    }

    const char *vol_max_key = strstr(resp, "\"volume_max\"");
    if (vol_max_key) {
        const char *colon = strchr(vol_max_key, ':');
        if (colon) {
            state->volume_max = atoi(colon + 1);
        }
    }

    state->volume_step = state->volume_step > 0 ? state->volume_step : 2;
    const char *step_key = strstr(resp, "\"volume_step\"");
    if (step_key) {
        const char *colon = strchr(step_key, ':');
        if (colon) {
            int parsed = atoi(colon + 1);
            if (parsed > 0) {
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

    char url[256];
    snprintf(url, sizeof(url), "%s/zones", bridge_base);
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

        maybe_update_bridge_base();
        if (!s_state.zone_resolved) {
            refresh_zone_label(true);
        }
        bool ok = fetch_now_playing(&state);
        post_ui_update(&state);
        post_ui_status(ok);

        // Show meaningful status messages for state transitions
        if (ok && !s_last_net_ok) {
            // Just connected to bridge - clear network status
            post_ui_message("Bridge: Connected");
            ui_set_network_status(NULL);  // Clear persistent status on success
        } else if (!ok && s_last_net_ok) {
            // Lost connection to bridge
            post_ui_message("Bridge: Offline");
            ui_set_network_status("Bridge: Offline - check connection");
        } else if (!ok && !s_last_net_ok) {
            // Still trying to connect - check if we have a bridge URL
            lock_state();
            bool has_bridge = (s_state.cfg.bridge_base[0] != '\0');
            char bridge_url[128];
            if (has_bridge) {
                strncpy(bridge_url, s_state.cfg.bridge_base, sizeof(bridge_url) - 1);
                bridge_url[sizeof(bridge_url) - 1] = '\0';
            }
            unlock_state();
            if (!has_bridge) {
                post_ui_message("Bridge: Searching...");
                ui_set_network_status("Bridge: Searching via mDNS...");
            } else {
                // Bridge URL configured but not responding
                // Show helpful message (truncate bridge URL if needed)
                static int offline_count = 0;
                offline_count++;
                if (offline_count == 5) {  // After ~5 seconds of failures
                    char msg[128];
                    // Truncate bridge URL to fit in message
                    char short_url[64];
                    strncpy(short_url, bridge_url, sizeof(short_url) - 1);
                    short_url[sizeof(short_url) - 1] = '\0';
                    snprintf(msg, sizeof(msg), "Bridge offline: %.60s", short_url);
                    ui_set_network_status(msg);
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
    strncpy(s_state.zone_label, cfg->zone_id[0] ? cfg->zone_id : "Press knob to select zone", sizeof(s_state.zone_label) - 1);
    s_state.zone_label[sizeof(s_state.zone_label) - 1] = '\0';
    unlock_state();
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

            // Check for special options first
            if (strcmp(selected_id, ZONE_ID_BACK) == 0) {
                LOGI("Zone picker: Back selected");
                ui_hide_zone_picker();
                return;
            }
            if (strcmp(selected_id, ZONE_ID_SETTINGS) == 0) {
                LOGI("Zone picker: Settings selected");
                ui_hide_zone_picker();
                ui_show_settings();
                return;
            }

            // Check if Bluetooth was selected
            if (controller_mode_is_bluetooth_zone(selected_id)) {
                LOGI("Zone picker: switching to Bluetooth mode");
                // Hide picker FIRST to ensure it closes before mode change callbacks
                ui_hide_zone_picker();
                lock_state();
                strncpy(s_state.cfg.zone_id, ZONE_ID_BLUETOOTH, sizeof(s_state.cfg.zone_id) - 1);
                s_state.cfg.zone_id[sizeof(s_state.cfg.zone_id) - 1] = '\0';
                unlock_state();
                platform_storage_save(&s_state.cfg);
                controller_mode_set(CONTROLLER_MODE_BLUETOOTH);
                return;
            }

            // Regular Roon zone selection
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
                    // Switch back to Roon mode if we were in BT mode
                    if (controller_mode_get() == CONTROLLER_MODE_BLUETOOTH) {
                        controller_mode_set(CONTROLLER_MODE_ROON);
                    }
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
        const char *names[MAX_ZONES + 4];  /* +4 for Back, Bluetooth, Settings, margin */
        const char *ids[MAX_ZONES + 4];
        static const char *back_name = "< Back";
        static const char *back_id = ZONE_ID_BACK;
        static const char *bt_name = "Bluetooth";
        static const char *bt_id = ZONE_ID_BLUETOOTH;
        static const char *settings_name = "Settings...";
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
        /* Add Bluetooth option if available */
        if (controller_mode_bluetooth_available() && count < MAX_ZONES + 3) {
            names[count] = bt_name;
            ids[count] = bt_id;
            /* Select Bluetooth if currently in BT mode */
            if (controller_mode_is_bluetooth_zone(s_state.cfg.zone_id)) {
                selected = count;
            }
            count++;
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
    case UI_INPUT_VOL_DOWN:
        lock_state();
        snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"vol_rel\",\"value\":%d}",
            s_state.cfg.zone_id, -2);
        unlock_state();
        // Show volume overlay immediately with predicted value (optimistic UI)
        ui_show_volume_change(s_last_known_volume - 2);
        if (!send_control_json(body)) {
            post_ui_message("Volume change failed");
        }
        break;
    case UI_INPUT_VOL_UP:
        lock_state();
        snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"vol_rel\",\"value\":%d}",
            s_state.cfg.zone_id, 2);
        unlock_state();
        // Show volume overlay immediately with predicted value (optimistic UI)
        ui_show_volume_change(s_last_known_volume + 2);
        if (!send_control_json(body)) {
            post_ui_message("Volume change failed");
        }
        break;
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
