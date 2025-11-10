#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_INPUT_VOL_DOWN = -1,
    UI_INPUT_NONE = 0,
    UI_INPUT_VOL_UP = 1,
    UI_INPUT_PLAY_PAUSE = 2,
    UI_INPUT_MENU = 3,
} ui_input_event_t;

typedef void (*ui_input_cb_t)(ui_input_event_t event);

void ui_init(void);
void ui_loop_iter(void);
void ui_update(const char *line1, const char *line2, bool playing, int volume, int seek_position, int length);
void ui_set_status(bool online);
void ui_set_message(const char *msg);
void ui_set_input_handler(ui_input_cb_t handler);
void ui_dispatch_input(ui_input_event_t ev);
void ui_set_zone_name(const char *zone_name);
void ui_show_zone_picker(const char **zone_names, int zone_count, int selected_idx);
void ui_hide_zone_picker(void);
bool ui_is_zone_picker_visible(void);
int ui_zone_picker_get_selected(void);
void ui_zone_picker_scroll(int delta);
void ui_set_battery_status(bool present, int percentage, int voltage_mv, bool charging);

#ifdef __cplusplus
}
#endif
