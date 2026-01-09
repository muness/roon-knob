#pragma once

#include "lvgl.h"

#ifdef ESP_PLATFORM

typedef struct {
    lv_image_dsc_t dsc;      // LVGL image descriptor
    uint8_t *pixel_buf;      // owned pixel buffer (RGB565)
} ui_jpeg_image_t;

// Free the pixel buffer inside ui_jpeg_image_t
void ui_jpeg_free(ui_jpeg_image_t *img);

// Load raw RGB565 data into LVGL image descriptor (copies to global buffer)
bool ui_rgb565_from_buffer(const uint8_t *rgb565_data,
                           int width,
                           int height,
                           ui_jpeg_image_t *out_img);

#endif  // ESP_PLATFORM
