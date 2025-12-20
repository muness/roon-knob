#include "app.h"
#include "platform/platform_input.h"
#include "platform/platform_time.h"
#include "ui.h"

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"

#include <stdint.h>
#include <stdio.h>

#define SCREEN_SIZE 240

// Settings UI stubs for PC simulator
void ui_show_settings(void) {
    printf("[PC] Settings: Long-press detected (settings not implemented in simulator)\n");
}

void ui_hide_settings(void) {
}

bool ui_is_settings_visible(void) {
    return false;
}

// Display sleep stub - PC simulator never sleeps
bool platform_display_is_sleeping(void) {
    return false;
}

// Display rotation stub - PC simulator ignores rotation
void platform_display_set_rotation(uint16_t degrees) {
    printf("[PC] Display rotation set to %d degrees (ignored in simulator)\n", degrees);
}

// Battery stubs - PC simulator always reports charging (USB powered)
bool platform_battery_is_charging(void) {
    return true;
}

int platform_battery_get_level(void) {
    return 100;  // PC always "fully charged"
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    lv_init();
    lv_display_t *disp = lv_sdl_window_create(SCREEN_SIZE, SCREEN_SIZE);
    lv_display_set_default(disp);
    lv_sdl_mouse_create();

    ui_init();
    platform_input_init();
    app_entry();

    while (true) {
        ui_loop_iter();
        platform_sleep_ms(5);
    }

    return 0;
}
