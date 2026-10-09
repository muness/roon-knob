#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RLCD_WIDTH 400
#define RLCD_HEIGHT 300
#define RLCD_FRAMEBUFFER_BYTES ((RLCD_WIDTH * RLCD_HEIGHT) / 8)

bool rlcd_display_init(void);
/** Put the ST7305 into its minimum-power Sleep In state. */
bool rlcd_display_prepare_for_sleep(void);
void rlcd_display_set_rgb565(uint16_t x, uint16_t y, uint16_t rgb565);
void rlcd_display_clear(bool white);
void rlcd_display_request_full_refresh(void);
void rlcd_display_refresh(void);
