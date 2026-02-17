#include "ui_jpeg.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "UI_RGB565";

// Pre-allocated global artwork buffer for RGB565 display
// 510x510 to fill round display (360 × √2). Bridge clips to circle server-side.
#define ARTWORK_MAX_W 510
#define ARTWORK_MAX_H 510
#define ARTWORK_BPP 2

static uint8_t *s_artwork_buf = NULL;
static size_t s_artwork_buf_size = 0;

// Initialize global artwork buffer (call once at startup)
static void ui_jpeg_buffer_init(void) {
  if (s_artwork_buf) {
    return; // Already initialized
  }

  // Allocate exactly what we need for RGB565 display
  s_artwork_buf_size = ARTWORK_MAX_W * ARTWORK_MAX_H * ARTWORK_BPP;

  // Try PSRAM first
  s_artwork_buf = heap_caps_aligned_calloc(16, 1, s_artwork_buf_size,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_artwork_buf) {
    ESP_LOGI(TAG, "Artwork buffer (%u bytes) allocated in PSRAM",
             (unsigned)s_artwork_buf_size);
    return;
  }

  // Fallback to internal RAM
  ESP_LOGW(TAG, "PSRAM allocation failed, trying internal RAM");
  s_artwork_buf = heap_caps_aligned_calloc(
      16, 1, s_artwork_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (s_artwork_buf) {
    ESP_LOGI(TAG, "Artwork buffer (%u bytes) allocated in internal RAM",
             (unsigned)s_artwork_buf_size);
    return;
  }

  ESP_LOGE(TAG, "Failed to allocate artwork buffer (%u bytes)",
           (unsigned)s_artwork_buf_size);
}

void ui_jpeg_free(ui_jpeg_image_t *img) {
  if (!img) {
    return;
  }

  // Don't free pixel_buf - it's the global buffer, not owned by this image
  // Just clear the descriptor so LVGL doesn't try to use stale data
  memset(img, 0, sizeof(*img));
}

bool ui_rgb565_from_buffer(const uint8_t *rgb565_data, int width, int height,
                           ui_jpeg_image_t *out_img) {
  if (!rgb565_data || width <= 0 || height <= 0 || !out_img) {
    return false;
  }

  // Ensure buffer is initialized
  if (!s_artwork_buf) {
    ui_jpeg_buffer_init();
    if (!s_artwork_buf) {
      ESP_LOGE(TAG, "Artwork buffer not available");
      return false;
    }
  }

  // Validate size
  size_t data_size = (size_t)width * height * 2;
  if (width > ARTWORK_MAX_W || height > ARTWORK_MAX_H ||
      data_size > s_artwork_buf_size) {
    ESP_LOGE(TAG,
             "RGB565 data size %dx%d (%zu bytes) exceeds buffer capacity (%zu "
             "bytes)",
             width, height, data_size, s_artwork_buf_size);
    return false;
  }

  // Copy to global buffer (maintains ownership model)
  memcpy(s_artwork_buf, rgb565_data, data_size);

  // Fill the LVGL image descriptor (same structure as JPEG decode)
  memset(out_img, 0, sizeof(*out_img));
  out_img->pixel_buf = s_artwork_buf;
  out_img->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  out_img->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  out_img->dsc.header.w = width;
  out_img->dsc.header.h = height;
  out_img->dsc.data = s_artwork_buf;
  out_img->dsc.data_size = data_size;

  ESP_LOGI(TAG, "Loaded raw RGB565 %dx%d (%zu bytes) into global buffer", width,
           height, data_size);
  return true;
}

#endif // ESP_PLATFORM
