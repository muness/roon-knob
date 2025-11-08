#pragma once

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
void ui_update(const char *line1, const char *line2, bool playing, int volume);
void ui_set_status(bool online);
void ui_set_input_handler(ui_input_cb_t handler);
void ui_loop_iter(void);
void ui_set_zone_name(const char *zone_name);

#ifdef __cplusplus
}
#endif
