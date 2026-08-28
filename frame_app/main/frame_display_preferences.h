#pragma once

#include <stdbool.h>

/* Frame-only presentation preferences intentionally stay outside rk_cfg_t. */
void frame_display_preferences_init(void);
bool frame_display_preferences_show_ip(void);
bool frame_display_preferences_set_show_ip(bool show);
