#include "platform/platform_input.h"

#include "ui.h"

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_keyboard.h"

#include <stdint.h>

static lv_indev_t *s_keyboard;

static void dispatch_key(ui_input_event_t event) {
    ui_dispatch_input(event);
}

static void keyboard_event_cb(lv_event_t *e) {
    if (!e) {
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_KEY) {
        return;
    }
    uint32_t key = lv_event_get_key(e);
    switch (key) {
    case LV_KEY_UP:
    case LV_KEY_RIGHT:
        dispatch_key(UI_INPUT_VOL_UP);
        break;
    case LV_KEY_DOWN:
    case LV_KEY_LEFT:
        dispatch_key(UI_INPUT_VOL_DOWN);
        break;
    case LV_KEY_ENTER:
    case ' ':
        dispatch_key(UI_INPUT_PLAY_PAUSE);
        break;
    case 'z':
    case 'm':
        dispatch_key(UI_INPUT_MENU);
        break;
    default:
        break;
    }
}

void platform_input_init(void) {
    s_keyboard = lv_sdl_keyboard_create();
    if (!s_keyboard) {
        return;
    }
    lv_obj_t *screen = lv_scr_act();
    if (!screen) {
        return;
    }
    lv_group_t *group = lv_group_create();
    if (!group) {
        return;
    }
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_group_add_obj(group, screen);
    lv_group_focus_obj(screen);
    lv_indev_set_group(s_keyboard, group);
    lv_obj_add_event_cb(screen, keyboard_event_cb, LV_EVENT_KEY, NULL);
}

void platform_input_process_events(void) {
    // PC simulator dispatches events directly from LVGL callbacks
    // No queued events to process
}

void platform_input_shutdown(void) {
    (void)s_keyboard;
}
