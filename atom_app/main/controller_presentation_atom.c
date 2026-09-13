#include "controller_presentation.h"
#include "controller_config.h"
#include "touch_ui.h"

static const char *warning(void) {
    controller_config_snapshot_t c;
    if (!controller_config_snapshot(&c)) return NULL;
    if (c.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT)
        return "Settings saved but could\nnot be verified";
    if (c.durability == CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY)
        return "Settings storage unavailable";
    return NULL;
}

void controller_presentation_update(const char *a, const char *b, const char *c,
                                    bool playing, float volume, float min,
                                    float max, float step, int pos, int length) {
    touch_ui_update(a, b, c, playing, volume, min, max, step, pos, length);
}
void controller_presentation_set_status(bool v) { touch_ui_set_status(v); }
void controller_presentation_set_message(const char *v) { touch_ui_set_message(v); }
void controller_presentation_set_zone_name(const char *v) { touch_ui_set_zone_name(v); }
void controller_presentation_set_network_status(const char *v) { touch_ui_set_network_status(warning() ? warning() : v); }
void controller_presentation_set_artwork(const char *v) { touch_ui_post_artwork(v); }
void controller_presentation_show_volume_change(float v, float s) { touch_ui_show_volume_change(v, s); }
void controller_presentation_update_battery(void) { touch_ui_update_battery(); }
void controller_presentation_show_zone_picker(const char **n, const char **i, int c, int s) { touch_ui_show_zone_picker(n, i, c, s); }
void controller_presentation_hide_zone_picker(void) { touch_ui_hide_zone_picker(); }
bool controller_presentation_is_zone_picker_visible(void) { return touch_ui_is_zone_picker_visible(); }
void controller_presentation_zone_picker_scroll(int d) { touch_ui_zone_picker_scroll(d); }
bool controller_presentation_zone_picker_is_current_selection(void) { return touch_ui_zone_picker_is_current_selection(); }
void controller_presentation_zone_picker_get_selected_id(char *o, size_t l) { touch_ui_zone_picker_get_selected_id(o, l); }
void controller_presentation_show_settings(void) { touch_ui_show_settings(); }
