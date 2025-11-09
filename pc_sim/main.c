#include <ctype.h>
#include <strings.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "os_thread.h"
#include "os_time.h"
#include "storage.h"
#include "ui.h"
#include "components/net_client/curl_client.h"

#define POLL_INTERVAL_SECONDS 3
#define MAX_LINE 128
#define MAX_ZONES 32

static char bridge_base[256] = "http://127.0.0.1:8088";
static char zone_id[64] = "";
static bool net_ok = false;
static int net_volume_step = 2;
static volatile bool run_threads = true;
static char zone_label[64] = "Loading zone";
static bool zone_resolved = false;

struct zone_entry {
    char zone_id[64];
    char zone_name[64];
};

static struct zone_entry cached_zones[MAX_ZONES];
static int cached_zone_count = 0;
static bool show_zone_picker = false;

struct now_playing {
    char line1[MAX_LINE];
    char line2[MAX_LINE];
    bool is_playing;
    int volume;
    int volume_step;
    int seek_position;
    int length;
};

static void default_now_playing(struct now_playing *state) {
    snprintf(state->line1, sizeof(state->line1), "%s", "Waiting for data");
    state->line2[0] = '\0';
    state->is_playing = false;
    state->volume = 0;
    state->volume_step = net_volume_step;
    state->seek_position = 0;
    state->length = 0;
}

static void curl_string_copy(const char *data, const char *key, char *out, size_t len) {
    const char *pos = strstr(data, key);
    if (!pos) return;
    const char *colon = strchr(pos, ':');
    if (!colon) return;
    const char *start = strchr(colon, '"');
    if (!start) return;
    start++;
    const char *end = strchr(start, '"');
    if (!end) return;
    size_t copy_len = end - start;
    if (copy_len >= len) copy_len = len - 1;
    memcpy(out, start, copy_len);
    out[copy_len] = '\0';
}

static bool refresh_zone_label(void);
static void log_msg(const char *fmt, ...);
static const char *extract_json_string(const char *start, const char *key, char *out, size_t len);
static void load_zone_from_store(void);
static void persist_zone_to_store(const char *id, const char *name);
static void parse_zones_from_response(const char *resp);

static bool fetch_now_playing(struct now_playing *state) {
    if (!zone_resolved) {
        refresh_zone_label();
        if (!zone_resolved) {
            log_msg("cannot resolve zone; skipping poll");
            return false;
        }
    }
    if (zone_id[0] == '\0') {
        refresh_zone_label();
        if (zone_id[0] == '\0') {
            log_msg("zone_id still empty after refresh");
            return false;
        }
    }
    char url[512];
    snprintf(url, sizeof(url), "%s/now_playing?zone_id=%s", bridge_base, zone_id);
    char *resp = NULL;
    size_t resp_len = 0;
    int ret = http_get(url, &resp, &resp_len);
    if (ret != 0 || !resp) {
        net_ok = false;
        log_msg("now_playing request failed (ret=%d)", ret);
        http_free(resp);
        return false;
    }
    if (resp_len == 0 || strstr(resp, "\"error\"")) {
        net_ok = false;
        zone_resolved = false;
        log_msg("now_playing returned error or empty payload: %.*s", (int)resp_len, resp ? resp : "");
        http_free(resp);
        return false;
    }

    curl_string_copy(resp, "\"line1\"", state->line1, sizeof(state->line1));
    curl_string_copy(resp, "\"line2\"", state->line2, sizeof(state->line2));
    if (strstr(resp, "\"is_playing\":true")) {
        state->is_playing = true;
    } else {
        state->is_playing = false;
    }
    const char *vol_key = strstr(resp, "\"volume\"");
    if (vol_key) {
        const char *colon = strchr(vol_key, ':');
        if (colon) {
            state->volume = atoi(colon + 1);
        }
    }

    state->volume_step = net_volume_step;
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
    net_volume_step = state->volume_step;

    // Parse seek_position and length
    state->seek_position = 0;
    state->length = 0;
    const char *seek_key = strstr(resp, "\"seek_position\"");
    if (seek_key) {
        const char *colon = strchr(seek_key, ':');
        if (colon) {
            state->seek_position = atoi(colon + 1);
        }
    }
    const char *len_key = strstr(resp, "\"length\"");
    if (len_key) {
        const char *colon = strchr(len_key, ':');
        if (colon) {
            state->length = atoi(colon + 1);
        }
    }

    parse_zones_from_response(resp);

    net_ok = true;
    http_free(resp);
    return true;
}

static bool send_control_json(const char *json) {
    char url[512];
    snprintf(url, sizeof(url), "%s/control", bridge_base);
    char *resp = NULL;
    size_t resp_len = 0;
    if (http_post_json(url, json, &resp, &resp_len) != 0) {
        log_msg("control failed payload=%s", json);
        http_free(resp);
        return false;
    }
    if (resp && strstr(resp, "\"error\"")) {
        log_msg("control replied error: %.*s", (int)resp_len, resp);
        http_free(resp);
        return false;
    }
    http_free(resp);
    return true;
}

static void handle_input(ui_input_event_t ev) {
    bool picker_visible = ui_is_zone_picker_visible();

    if (picker_visible) {
        // Zone picker is open - handle navigation
        switch (ev) {
            case UI_INPUT_VOL_DOWN:
                ui_zone_picker_scroll(1);
                break;
            case UI_INPUT_VOL_UP:
                ui_zone_picker_scroll(-1);
                break;
            case UI_INPUT_PLAY_PAUSE: {
                // Select zone
                int selected = ui_zone_picker_get_selected();
                if (selected >= 0 && selected < cached_zone_count) {
                    snprintf(zone_id, sizeof(zone_id), "%s", cached_zones[selected].zone_id);
                    snprintf(zone_label, sizeof(zone_label), "%s", cached_zones[selected].zone_name);
                    ui_set_zone_name(zone_label);
                    persist_zone_to_store(zone_id, zone_label);
                    zone_resolved = true;
                    log_msg("selected zone: %s (%s)", zone_label, zone_id);
                    ui_set_message("Loading zone...");
                    ui_set_status(false);
                }
                ui_hide_zone_picker();
                break;
            }
            case UI_INPUT_MENU:
                // Close picker without selecting
                ui_hide_zone_picker();
                break;
            default:
                break;
        }
    } else {
        // Normal mode - handle playback controls
        switch (ev) {
            case UI_INPUT_VOL_DOWN: {
                int step = net_volume_step > 0 ? net_volume_step : 2;
                char body[256];
                snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"vol_rel\",\"value\":%d}", zone_id, -step);
                if (!send_control_json(body)) {
                    ui_set_message("Volume change failed");
                }
                break;
            }
            case UI_INPUT_VOL_UP: {
                int step = net_volume_step > 0 ? net_volume_step : 2;
                char body[256];
                snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"vol_rel\",\"value\":%d}", zone_id, step);
                if (!send_control_json(body)) {
                    ui_set_message("Volume change failed");
                }
                break;
            }
            case UI_INPUT_PLAY_PAUSE: {
                char body[256];
                snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"play_pause\"}", zone_id);
                if (!send_control_json(body)) {
                    ui_set_message("Play/pause failed");
                }
                break;
            }
            case UI_INPUT_MENU: {
                // Open zone picker
                if (cached_zone_count > 0) {
                    const char *zone_names[MAX_ZONES];
                    for (int i = 0; i < cached_zone_count; i++) {
                        zone_names[i] = cached_zones[i].zone_name;
                    }
                    int current_idx = 0;
                    for (int i = 0; i < cached_zone_count; i++) {
                        if (strcmp(cached_zones[i].zone_id, zone_id) == 0) {
                            current_idx = i;
                            break;
                        }
                    }
                    ui_show_zone_picker(zone_names, cached_zone_count, current_idx);
                } else {
                    ui_set_message("No zones available");
                }
                break;
            }
            default:
                break;
        }
    }
}

static void *poll_thread(void *arg) {
    struct now_playing state;
    default_now_playing(&state);
    while (run_threads) {
        bool ok = fetch_now_playing(&state);
        if (ok) {
            ui_update(state.line1, state.line2, state.is_playing, state.volume, state.seek_position, state.length);
            ui_set_status(true);
            ui_set_message("Connected");
        } else {
            ui_set_status(false);
            ui_set_message("Waiting for data...");
        }
        os_sleep_sec(POLL_INTERVAL_SECONDS);
    }
    return NULL;
}

static bool refresh_zone_label(void) {
    char url[512];
    snprintf(url, sizeof(url), "%s/zones", bridge_base);
    char *resp = NULL;
    size_t resp_len = 0;
    if (http_get(url, &resp, &resp_len) != 0 || !resp) {
        ui_set_zone_name(zone_label);
        http_free(resp);
        zone_resolved = false;
        return false;
    }
    const char *cursor = resp;
    char first_id[64] = "";
    char first_name[64] = "";
    bool found = false;
    bool want_any = zone_id[0] == '\0';
    while ((cursor = strstr(cursor, "\"zone_id\""))) {
        char current_id[64];
        const char *after_id = extract_json_string(cursor, "\"zone_id\"", current_id, sizeof(current_id));
        if (!after_id) break;

        char current_name[64];
        const char *after_name = extract_json_string(after_id, "\"zone_name\"", current_name, sizeof(current_name));
        if (!after_name) {
            cursor = after_id;
            continue;
        }

        if (first_id[0] == '\0') {
            snprintf(first_id, sizeof(first_id), "%s", current_id);
            snprintf(first_name, sizeof(first_name), "%s", current_name);
        }

        bool id_match = zone_id[0] && strcmp(current_id, zone_id) == 0;
        bool name_match = zone_id[0] && strcasecmp(current_name, zone_id) == 0;
        if (want_any || id_match || name_match) {
            snprintf(zone_id, sizeof(zone_id), "%s", current_id);
            snprintf(zone_label, sizeof(zone_label), "%s", current_name);
            ui_set_zone_name(zone_label);
            found = true;
            zone_resolved = true;
            persist_zone_to_store(zone_id, zone_label);
            break;
        }
        cursor = after_name;
    }

    if (!found && first_id[0]) {
        snprintf(zone_id, sizeof(zone_id), "%s", first_id);
        snprintf(zone_label, sizeof(zone_label), "%s", first_name);
        ui_set_zone_name(zone_label);
        zone_resolved = true;
        persist_zone_to_store(zone_id, zone_label);
        log_msg("zone fallback -> id=%s name=%s", zone_id, zone_label);
    } else if (!found) {
        ui_set_zone_name(zone_label);
        zone_resolved = false;
        log_msg("zones fetch did not resolve any zone");
    }
    http_free(resp);
    return zone_resolved;
}
int main(int argc, char **argv) {
    storage_init();
    load_zone_from_store();
    const char *env_base = getenv("ROON_BRIDGE_BASE");
    if (env_base && env_base[0]) {
        snprintf(bridge_base, sizeof(bridge_base), "%s", env_base);
    }
    const char *env_zone = getenv("ZONE_ID");
    if (env_zone && env_zone[0]) {
        snprintf(zone_id, sizeof(zone_id), "%s", env_zone);
        snprintf(zone_label, sizeof(zone_label), "%s", env_zone);
    } else {
        zone_id[0] = '\0';
        snprintf(zone_label, sizeof(zone_label), "%s", "Loading zone");
    }
    ui_init();
    ui_set_zone_name(zone_label);
    ui_set_input_handler(handle_input);
    refresh_zone_label();

    os_thread_t net_thread;
    os_thread_create(&net_thread, poll_thread, NULL);

    while (true) {
        ui_loop_iter();
        os_sleep_us(5000);
    }
}

static void log_msg(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[pc_sim] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static const char *extract_json_string(const char *start, const char *key, char *out, size_t len) {
    const char *key_pos = strstr(start, key);
    if (!key_pos) return NULL;
    const char *colon = strchr(key_pos, ':');
    if (!colon) return NULL;
    const char *quote_start = strchr(colon, '"');
    if (!quote_start) return NULL;
    quote_start++;
    const char *quote_end = strchr(quote_start, '"');
    if (!quote_end) return NULL;
    size_t copy_len = quote_end - quote_start;
    if (copy_len >= len) copy_len = len - 1;
    memcpy(out, quote_start, copy_len);
    out[copy_len] = '\0';
    return quote_end + 1;
}

static void load_zone_from_store(void) {
    char stored_id[64];
    char stored_label[64];
    if (storage_get("zone_id", stored_id, sizeof(stored_id)) == 0 && stored_id[0]) {
        snprintf(zone_id, sizeof(zone_id), "%s", stored_id);
        if (storage_get("zone_name", stored_label, sizeof(stored_label)) == 0 && stored_label[0]) {
            snprintf(zone_label, sizeof(zone_label), "%s", stored_label);
        } else {
            snprintf(zone_label, sizeof(zone_label), "%s", stored_id);
        }
        log_msg("loaded stored zone id=%s", zone_id);
    }
}

static void persist_zone_to_store(const char *id, const char *name) {
    if (!id || !id[0]) return;
    if (storage_set("zone_id", id) != 0) {
        log_msg("failed to persist zone id=%s", id);
    }
    if (name && name[0]) {
        storage_set("zone_name", name);
    }
}

static void parse_zones_from_response(const char *resp) {
    if (!resp) return;

    const char *zones_array = strstr(resp, "\"zones\":[");
    if (!zones_array) return;

    cached_zone_count = 0;
    const char *cursor = zones_array;

    while (cached_zone_count < MAX_ZONES && (cursor = strstr(cursor, "\"zone_id\""))) {
        char id[64] = {0};
        char name[64] = {0};

        const char *after_id = extract_json_string(cursor, "\"zone_id\"", id, sizeof(id));
        if (!after_id || !id[0]) break;

        const char *after_name = extract_json_string(after_id, "\"zone_name\"", name, sizeof(name));
        if (!after_name || !name[0]) {
            cursor = after_id;
            continue;
        }

        snprintf(cached_zones[cached_zone_count].zone_id, sizeof(cached_zones[0].zone_id), "%s", id);
        snprintf(cached_zones[cached_zone_count].zone_name, sizeof(cached_zones[0].zone_name), "%s", name);
        cached_zone_count++;

        cursor = after_name;
    }

    log_msg("parsed %d zones from response", cached_zone_count);
}
