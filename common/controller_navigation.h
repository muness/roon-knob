#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {
  CONTROLLER_SCREEN_ARTWORK,
  CONTROLLER_SCREEN_NOW_PLAYING,
  CONTROLLER_SCREEN_LIBRARY,
  CONTROLLER_SCREEN_ZONES,
  CONTROLLER_SCREEN_SETTINGS,
  CONTROLLER_SCREEN_MENU,
  CONTROLLER_SCREEN_SHORTCUTS,
  CONTROLLER_SCREEN_COUNT
} controller_screen_t;
bool controller_navigation_encoder_uses_volume(controller_screen_t screen);
