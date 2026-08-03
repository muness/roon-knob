#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t column_start;
    uint8_t column_end;
    uint8_t page_start;
    uint8_t page_end;
    uint16_t framebuffer_byte_start;
    uint16_t bytes_per_page;
    uint16_t page_count;
    size_t transfer_size;
} rlcd_st7305_window_t;

/* Convert landscape display coordinates to the ST7305's packed 2x12-pixel
 * address units. Bounds are inclusive. */
bool rlcd_st7305_window_from_dirty(int x1, int y1, int x2, int y2,
                                   rlcd_st7305_window_t *window);
