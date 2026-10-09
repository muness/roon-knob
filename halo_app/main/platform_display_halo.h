#pragma once
/* HALO TOUCH V2 display + touch platform (ST77916 360x360 round QSPI panel, CST816 touch, LEDC backlight).
 * Always on: no dim/sleep. The I2C bus created here is the single port-0 bus for the board. */
#include <stdbool.h>
#include <stdint.h>
#include <driver/i2c_master.h>

bool platform_display_init(void);                 /* SPI/panel/backlight/I2C/touch hardware; before lv_init() */
bool platform_display_register_lvgl_driver(void); /* after lv_init(): buffers, flush cb, touch indev, tick timer */
bool platform_display_is_ready(void);
void platform_display_process_pending(void);      /* UI loop: deferred art-mode gesture handling */
void platform_display_set_brightness(int percent);/* 0..100 -> LEDC duty (floor of 5% so it never goes black) */
i2c_master_bus_handle_t platform_display_halo_i2c_bus(void); /* shared port-0 bus (haptic/INA later) */
