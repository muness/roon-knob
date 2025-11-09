#include "config_store.h"
#include "mdns_client.h"
#include "ui.h"
#include "http_client.h"
#include "encoder_input.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define POLL_DELAY_MS 1000
#define MAX_LINE 128

static char bridge_base[256] = "http://127.0.0.1:8088";
static char zone_id[64] = "office";

struct now_playing {
    char line1[MAX_LINE];
    char line2[MAX_LINE];
    bool is_playing;
    int volume;
};

static void copy_value(const char *data, const char *key, char *out, size_t len) {
    const char *found = strstr(data, key);
    if (!found) {
        return;
    }
    const char *start = strchr(found, '"');
    if (!start) {
        return;
    }
    start++;
    const char *end = strchr(start, '"');
    if (!end) {
        return;
    }
    size_t copy_len = end - start;
    if (copy_len >= len) {
        copy_len = len - 1;
    }
    memcpy(out, start, copy_len);
    out[copy_len] = '\0';
}

static bool fetch_now_playing(struct now_playing *state) {
    char url[512];
    snprintf(url, sizeof(url), "%s/now_playing?zone_id=%s", bridge_base, zone_id);
    char *resp = NULL;
    size_t resp_len = 0;
    if (http_get(url, &resp, &resp_len) != 0 || !resp) {
        return false;
    }

    copy_value(resp, "\"line1\"", state->line1, sizeof(state->line1));
    copy_value(resp, "\"line2\"", state->line2, sizeof(state->line2));
    state->is_playing = strstr(resp, "\"is_playing\":true") != NULL;
    const char *vol_key = strstr(resp, "\"volume\"");
    if (vol_key) {
        const char *colon = strchr(vol_key, ':');
        if (colon) {
            state->volume = atoi(colon + 1);
        }
    }
    http_free(resp);
    return true;
}

void app_main(void) {
    ui_init();
    encoder_input_init();
    config_store_init();
    char stored_base[256];
    if (config_store_get_bridge_base(stored_base, sizeof(stored_base)) == 0 && stored_base[0]) {
        snprintf(bridge_base, sizeof(bridge_base), "%s", stored_base);
    } else if (mdns_client_init() == 0) {
        if (mdns_client_query_bridge(stored_base, sizeof(stored_base)) == 0 && stored_base[0]) {
            snprintf(bridge_base, sizeof(bridge_base), "%s", stored_base);
            config_store_set_bridge_base(bridge_base);
        }
    }
    char stored_zone[64];
    if (config_store_get_zone_id(stored_zone, sizeof(stored_zone)) == 0 && stored_zone[0]) {
        snprintf(zone_id, sizeof(zone_id), "%s", stored_zone);
    }

    struct now_playing state = {0};
    strncpy(state.line1, "Waiting for bridge", sizeof(state.line1));

    while (true) {
        bool ok = fetch_now_playing(&state);
        ui_update(state.line1, state.line2, state.is_playing, state.volume);
        ui_set_status(ok);
        vTaskDelay(pdMS_TO_TICKS(POLL_DELAY_MS));
    }
}
