#include "ui_jpeg.h"

#ifdef ESP_PLATFORM
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "UI_JPEG";

// Pre-allocated global artwork buffer with 20% safety margin
// Base: 360x360x2 = 259,200 bytes
// With 20% margin: 311,040 bytes (~304 KB)
#define ARTWORK_MAX_W  360
#define ARTWORK_MAX_H  360
#define ARTWORK_BPP    2
#define ARTWORK_SAFETY_MARGIN  1.2f  // 20% extra for future larger images

static uint8_t *s_artwork_buf = NULL;
static size_t s_artwork_buf_size = 0;

// Initialize global artwork buffer (call once at startup)
static void ui_jpeg_buffer_init(void)
{
    if (s_artwork_buf) {
        return;  // Already initialized
    }

    // Allocate with 20% safety margin for future larger images
    size_t base_size = ARTWORK_MAX_W * ARTWORK_MAX_H * ARTWORK_BPP;
    s_artwork_buf_size = (size_t)(base_size * ARTWORK_SAFETY_MARGIN);

    // Try PSRAM first
    s_artwork_buf = heap_caps_aligned_calloc(16, 1, s_artwork_buf_size,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_artwork_buf) {
        ESP_LOGI(TAG, "Artwork buffer (%u bytes) allocated in PSRAM", (unsigned)s_artwork_buf_size);
        return;
    }

    // Fallback to internal RAM
    ESP_LOGW(TAG, "PSRAM allocation failed, trying internal RAM");
    s_artwork_buf = heap_caps_aligned_calloc(16, 1, s_artwork_buf_size,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_artwork_buf) {
        ESP_LOGI(TAG, "Artwork buffer (%u bytes) allocated in internal RAM", (unsigned)s_artwork_buf_size);
        return;
    }

    ESP_LOGE(TAG, "Failed to allocate artwork buffer (%u bytes)", (unsigned)s_artwork_buf_size);
}

bool ui_jpeg_decode_to_lvgl(const uint8_t *jpeg_data,
                            int jpeg_len,
                            int max_w,
                            int max_h,
                            ui_jpeg_image_t *out_img)
{
    if (!jpeg_data || jpeg_len <= 0 || !out_img) {
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

    jpeg_error_t ret = JPEG_ERR_OK;
    jpeg_dec_handle_t dec = NULL;

    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;   // RGB565 little endian (matches LVGL native)
    cfg.rotate = JPEG_ROTATE_0D;                     // no rotation

    jpeg_dec_io_t *io = calloc(1, sizeof(jpeg_dec_io_t));
    jpeg_dec_header_info_t *info = calloc(1, sizeof(jpeg_dec_header_info_t));
    if (!io || !info) {
        ESP_LOGE(TAG, "calloc for decoder structs failed");
        free(io);
        free(info);
        return false;
    }

    io->inbuf = (uint8_t *)jpeg_data;
    io->inbuf_len = jpeg_len;

    ret = jpeg_dec_open(&cfg, &dec);
    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_open failed, ret=%d", ret);
        free(io);
        free(info);
        return false;
    }

    // Parse header to get width and height
    ret = jpeg_dec_parse_header(dec, io, info);
    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_parse_header failed, ret=%d", ret);
        jpeg_dec_close(dec);
        free(io);
        free(info);
        return false;
    }

    int w = info->width;
    int h = info->height;

    if (w <= 0 || h <= 0 || w > ARTWORK_MAX_W || h > ARTWORK_MAX_H) {
        ESP_LOGE(TAG, "JPEG size %dx%d out of bounds (max %dx%d)",
                 w, h, ARTWORK_MAX_W, ARTWORK_MAX_H);
        jpeg_dec_close(dec);
        free(io);
        free(info);
        return false;
    }

    // Use pre-allocated global buffer
    io->outbuf = s_artwork_buf;

    // Decode into the output buffer
    ret = jpeg_dec_process(dec, io);
    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_process failed, ret=%d", ret);
        jpeg_dec_close(dec);
        free(io);
        free(info);
        return false;
    }

    // Fill the LVGL image descriptor
    memset(out_img, 0, sizeof(*out_img));

    // Point to global buffer (NOT owned by this image)
    out_img->pixel_buf = s_artwork_buf;
    out_img->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    out_img->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    out_img->dsc.header.w = w;
    out_img->dsc.header.h = h;
    out_img->dsc.data = s_artwork_buf;
    out_img->dsc.data_size = (size_t)w * h * 2;  // Actual decoded size

    jpeg_dec_close(dec);
    free(io);
    free(info);

    ESP_LOGI(TAG, "Decoded JPEG to %dx%d RGB565 (%lu bytes)", w, h, (unsigned long)out_img->dsc.data_size);
    return true;
}

void ui_jpeg_free(ui_jpeg_image_t *img)
{
    if (!img) {
        return;
    }

    // Don't free pixel_buf - it's the global buffer, not owned by this image
    // Just clear the descriptor so LVGL doesn't try to use stale data
    memset(img, 0, sizeof(*img));
}

#endif  // ESP_PLATFORM
