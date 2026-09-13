// Mirrors tests/test_controller_presentation_frame.c's structure exactly,
// against touch_ui.h's (larger) contract instead of eink_ui.h's.

#include "controller_presentation.h"
#include "controller_config.h"
#include "touch_ui.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int s_update_calls;
static const char *s_line1;
static const char *s_line2;
static const char *s_line3;
static bool s_playing;
static float s_volume;
static float s_volume_min;
static float s_volume_max;
static float s_volume_step;
static int s_seek_position;
static int s_length;
static int s_status_calls;
static int s_message_calls;
static int s_zone_calls;
static int s_network_calls;
static char s_network_status[128];
static bool s_network_status_is_null;
static controller_config_durability_t s_config_durability =
    CONTROLLER_CONFIG_DURABILITY_DURABLE;
static int s_artwork_calls;
static int s_volume_calls;
static int s_battery_calls;
static int s_picker_show_calls;
static int s_picker_hide_calls;
static int s_picker_scroll_delta;
static int s_settings_calls;
static bool s_picker_visible = true;
static bool s_picker_current;

void touch_ui_init(void) {}
void touch_ui_process(void) {}
void touch_ui_set_status(bool online) {
    assert(online);
    ++s_status_calls;
}
void touch_ui_set_message(const char *msg) {
    assert(strcmp(msg, "message") == 0);
    ++s_message_calls;
}
void touch_ui_set_zone_name(const char *name) {
    assert(strcmp(name, "zone") == 0);
    ++s_zone_calls;
}
void touch_ui_set_network_status(const char *status) {
    s_network_status_is_null = status == NULL;
    snprintf(s_network_status, sizeof(s_network_status), "%s",
             status ? status : "");
    ++s_network_calls;
}
void touch_ui_post_zone_name(const char *name) {
    (void)name;
}
void touch_ui_post_network_status(const char *status) {
    (void)status;
}
void touch_ui_set_artwork(const char *image_key) {
    assert(strcmp(image_key, "art") == 0);
    ++s_artwork_calls;
}
void touch_ui_show_volume_change(float vol, float vol_step) {
    assert(vol == 42.0f);
    assert(vol_step == 1.0f);
    ++s_volume_calls;
}
void touch_ui_update(const char *line1, const char *line2, const char *line3,
                     bool playing, float volume, float volume_min,
                     float volume_max, float volume_step, int seek_position,
                     int length) {
    ++s_update_calls;
    s_line1 = line1;
    s_line2 = line2;
    s_line3 = line3;
    s_playing = playing;
    s_volume = volume;
    s_volume_min = volume_min;
    s_volume_max = volume_max;
    s_volume_step = volume_step;
    s_seek_position = seek_position;
    s_length = length;
}
void touch_ui_show_zone_picker(const char **zone_names, const char **zone_ids,
                               int zone_count, int selected_idx) {
    (void)zone_names;
    (void)zone_ids;
    (void)zone_count;
    (void)selected_idx;
    ++s_picker_show_calls;
}
void touch_ui_hide_zone_picker(void) {
    ++s_picker_hide_calls;
}
bool touch_ui_is_zone_picker_visible(void) {
    return s_picker_visible;
}
void touch_ui_zone_picker_scroll(int delta) {
    s_picker_scroll_delta = delta;
}
void touch_ui_zone_picker_get_selected_id(char *out, size_t len) {
    snprintf(out, len, "%s", "tough-id");
}
bool touch_ui_zone_picker_is_current_selection(void) {
    return s_picker_current;
}
void touch_ui_update_battery(void) {
    ++s_battery_calls;
}
void touch_ui_show_settings(void) {
    ++s_settings_calls;
}
bool controller_config_snapshot(controller_config_snapshot_t *out) {
    assert(out);
    memset(out, 0, sizeof(*out));
    out->durability = s_config_durability;
    return true;
}

int main(void) {
    const char *names[] = {"one", "two"};
    const char *ids[] = {"id-one", "id-two"};
    char selected_id[16] = {0};

    controller_presentation_update("track", "artist", "album", true, 42.0f,
                                   2.0f, 98.0f, 0.5f, 13, 241);
    controller_presentation_set_status(true);
    controller_presentation_set_message("message");
    controller_presentation_set_zone_name("zone");
    controller_presentation_set_network_status("network");
    assert(strcmp(s_network_status, "network") == 0);
    s_config_durability = CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT;
    controller_presentation_set_network_status("WiFi: Connecting...");
    assert(strcmp(s_network_status,
                  "Settings saved but could\nnot be verified") == 0);
    s_config_durability = CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY;
    controller_presentation_set_network_status(NULL);
    assert(strcmp(s_network_status,
                  "Settings storage unavailable;\nchanges may not survive") == 0);
    s_config_durability = CONTROLLER_CONFIG_DURABILITY_DURABLE;
    controller_presentation_set_network_status(NULL);
    assert(s_network_status_is_null);
    controller_presentation_set_artwork("art");
    controller_presentation_show_volume_change(42.0f, 1.0f);
    controller_presentation_update_battery();
    controller_presentation_show_zone_picker(names, ids, 2, 1);
    controller_presentation_zone_picker_scroll(1);
    controller_presentation_zone_picker_get_selected_id(selected_id,
                                                        sizeof(selected_id));
    controller_presentation_hide_zone_picker();
    controller_presentation_show_settings();

    assert(s_update_calls == 1);
    assert(strcmp(s_line1, "track") == 0);
    assert(strcmp(s_line2, "artist") == 0);
    assert(strcmp(s_line3, "album") == 0);
    assert(s_playing);
    assert(s_volume == 42.0f);
    assert(s_volume_min == 2.0f);
    assert(s_volume_max == 98.0f);
    assert(s_volume_step == 0.5f);
    assert(s_seek_position == 13);
    assert(s_length == 241);
    assert(s_status_calls == 1);
    assert(s_message_calls == 1);
    assert(s_zone_calls == 1);
    assert(s_network_calls == 4);
    assert(s_artwork_calls == 1);
    assert(s_volume_calls == 1);
    assert(s_battery_calls == 1);
    assert(s_picker_show_calls == 1);
    assert(s_picker_hide_calls == 1);
    assert(s_picker_scroll_delta == 1);
    assert(controller_presentation_is_zone_picker_visible());
    assert(!controller_presentation_zone_picker_is_current_selection());
    assert(strcmp(selected_id, "tough-id") == 0);
    assert(s_settings_calls == 1);

    controller_presentation_update(NULL, NULL, NULL, false, -1.0f, -2.0f,
                                   -3.0f, -4.0f, -5, -6);
    assert(s_update_calls == 2);
    assert(s_line1 == NULL);
    assert(s_line2 == NULL);
    assert(s_line3 == NULL);
    assert(!s_playing);
    assert(s_volume == -1.0f);
    assert(s_volume_min == -2.0f);
    assert(s_volume_max == -3.0f);
    assert(s_volume_step == -4.0f);
    assert(s_seek_position == -5);
    assert(s_length == -6);

    puts("Tough presentation adapter contracts passed");
    return 0;
}
