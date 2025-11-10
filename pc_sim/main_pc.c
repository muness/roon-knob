#include "app.h"
#include "platform/platform_input.h"
#include "platform/platform_power.h"
#include "platform/platform_time.h"
#include "ui.h"

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"

#define SCREEN_SIZE 240

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    lv_init();
    lv_display_t *disp = lv_sdl_window_create(SCREEN_SIZE, SCREEN_SIZE);
    lv_display_set_default(disp);
    lv_sdl_mouse_create();

    ui_init();
    platform_input_init();
    platform_power_init();

    struct platform_power_status status;
    if (platform_power_get_status(&status)) {
        ui_set_battery_status(status.present, status.percentage, status.voltage_mv, status.charging);
    } else {
        ui_set_battery_status(false, -1, 0, false);
    }

    app_entry();

    while (true) {
        ui_loop_iter();
        platform_sleep_ms(5);
    }

    return 0;
}
