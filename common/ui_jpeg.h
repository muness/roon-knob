#pragma once

#include "lvgl.h"

#ifdef ESP_PLATFORM
#include "esp_jpeg_dec.h"

typedef struct {
    lv_image_dsc_t dsc;      // LVGL image descriptor
    uint8_t *pixel_buf;      // owned pixel buffer (RGB565)
} ui_jpeg_image_t;

// Decode one JPEG in memory to an LVGL image descriptor.
// max_w / max_h are limits for sanity, not scaling in this simple version.
bool ui_jpeg_decode_to_lvgl(const uint8_t *jpeg_data,
                            int jpeg_len,
                            int max_w,
                            int max_h,
                            ui_jpeg_image_t *out_img);

// Free the pixel buffer inside ui_jpeg_image_t
void ui_jpeg_free(ui_jpeg_image_t *img);

#endif  // ESP_PLATFORM
