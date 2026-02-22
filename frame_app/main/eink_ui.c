// E-ink now-playing UI renderer for hiphi frame
// Replaces LVGL-based ui.c — renders directly to e-ink framebuffer

#include "eink_ui.h"
#include "eink_display.h"
#include "eink_dither.h"
#include "eink_font.h"
#include "bridge_client.h"
#include "platform/platform_http.h"
#include "platform/platform_time.h"
#include "platform/platform_log.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "eink_ui";

// ── UI state ────────────────────────────────────────────────────────────────

// Landscape layout: 800 wide x 480 tall
// Device mounted with long axis horizontal
#define ART_SIZE       440   // Artwork square size
#define HEADER_H        30   // Header bar height
#define FOOTER_H        20   // Footer bar height
#define ART_X            0   // Artwork left edge
#define ART_Y           HEADER_H   // Artwork top edge
#define TEXT_X         (ART_SIZE + 10)  // Text area right of artwork
#define TEXT_Y         (HEADER_H + 20)  // Text area top
#define TEXT_W         (EINK_WIDTH - TEXT_X - 10)  // Text area width

// Debounce: wait 5s after last state change before rendering
#define RENDER_DEBOUNCE_MS 5000
// Minimum 180s between refreshes (Waveshare recommendation for this panel)
#define RENDER_COOLDOWN_MS 180000

static struct {
    char zone_name[64];
    char track[128];
    char artist[128];
    char album[128];
    char message[128];
    char network_status[128];
    char image_key[128];
    float volume;
    float volume_step;
    bool playing;
    bool online;

    // Dirty flags
    bool dirty;             // Any state changed — needs re-render
    bool art_dirty;         // New artwork needs download
    uint64_t last_change;   // Timestamp of last state change (for debounce)
    uint64_t last_render;   // Timestamp of last completed render (for cooldown)
    bool initial_draw_done; // First render after boot
} s_ui;

// ── Artwork download + dither ───────────────────────────────────────────────

static void render_artwork(void) {
    if (!s_ui.image_key[0]) return;

    // Build artwork URL from bridge
    char url[256];
    const char *art_url = bridge_client_get_artwork_url(url, sizeof(url), ART_SIZE, ART_SIZE, 0);
    if (!art_url || !art_url[0]) {
        ESP_LOGW(TAG, "No artwork URL available");
        return;
    }

    ESP_LOGI(TAG, "Downloading artwork: %s", art_url);

    // Download RGB565 image
    char *img_data = NULL;
    size_t img_len = 0;
    if (platform_http_get_image(art_url, &img_data, &img_len) != 0 || !img_data) {
        ESP_LOGE(TAG, "Artwork download failed");
        return;
    }

    // Determine if this is RGB565 data
    int expected_rgb565 = ART_SIZE * ART_SIZE * 2;
    int pixel_count = ART_SIZE * ART_SIZE;

    // Allocate RGB888 buffer in PSRAM
    uint8_t *rgb888 = heap_caps_malloc(pixel_count * 3, MALLOC_CAP_SPIRAM);
    if (!rgb888) {
        ESP_LOGE(TAG, "Failed to allocate RGB888 buffer");
        platform_http_free(img_data);
        return;
    }

    if ((int)img_len == expected_rgb565) {
        // Convert RGB565 -> RGB888
        eink_rgb565_to_rgb888((const uint8_t *)img_data, rgb888, ART_SIZE, ART_SIZE);
    } else {
        // Assume raw RGB888
        int expected_rgb888 = pixel_count * 3;
        if ((int)img_len >= expected_rgb888) {
            memcpy(rgb888, img_data, expected_rgb888);
        } else {
            ESP_LOGW(TAG, "Unexpected image size: %d (expected %d or %d)",
                     (int)img_len, expected_rgb565, expected_rgb888);
            heap_caps_free(rgb888);
            platform_http_free(img_data);
            return;
        }
    }
    platform_http_free(img_data);

    // Allocate dithered output buffer
    uint8_t *dithered = heap_caps_malloc(pixel_count * 3, MALLOC_CAP_SPIRAM);
    if (!dithered) {
        ESP_LOGE(TAG, "Failed to allocate dither buffer");
        heap_caps_free(rgb888);
        return;
    }

    // Floyd-Steinberg dither to 4-color palette
    ESP_LOGI(TAG, "Dithering %dx%d artwork...", ART_SIZE, ART_SIZE);
    eink_dither_rgb888(rgb888, dithered, ART_SIZE, ART_SIZE);
    heap_caps_free(rgb888);

    // Write dithered pixels to framebuffer
    for (int y = 0; y < ART_SIZE; y++) {
        for (int x = 0; x < ART_SIZE; x++) {
            int idx = (y * ART_SIZE + x) * 3;
            uint8_t r = dithered[idx + 0];
            uint8_t g = dithered[idx + 1];
            uint8_t b = dithered[idx + 2];
            uint8_t color = eink_nearest_color(r, g, b);
            uint16_t px = ART_X + x;
            uint16_t py = ART_Y + y;
            if (px < EINK_WIDTH && py < EINK_HEIGHT) {
                eink_display_set_pixel(px, py, color);
            }
        }
    }

    heap_caps_free(dithered);
    ESP_LOGI(TAG, "Artwork rendered to framebuffer");
}

// ── Text rendering helpers ──────────────────────────────────────────────────

static void draw_hline(uint16_t x, uint16_t y, uint16_t w, uint8_t color) {
    for (uint16_t i = 0; i < w; i++) {
        eink_display_set_pixel(x + i, y, color);
    }
}

static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color) {
    for (uint16_t j = 0; j < h; j++) {
        for (uint16_t i = 0; i < w; i++) {
            eink_display_set_pixel(x + i, y + j, color);
        }
    }
}

// Truncate string to fit width, adding "..." if needed
static void truncate_to_fit(const char *src, char *dst, size_t dst_len,
                            int max_width, const eink_font_t *font) {
    int len = strlen(src);
    int w = eink_font_string_width(src, font);
    if (w <= max_width) {
        snprintf(dst, dst_len, "%s", src);
        return;
    }
    // Binary search for max chars that fit with "..."
    int ellipsis_w = eink_font_string_width("...", font);
    int fit_w = max_width - ellipsis_w;
    int chars = 0;
    for (int i = 0; i < len && i < (int)dst_len - 4; i++) {
        if ((i + 1) * font->width > fit_w) break;
        chars = i + 1;
    }
    memcpy(dst, src, chars);
    dst[chars] = '\0';
    strncat(dst, "...", dst_len - chars - 1);
}

// ── Full screen render ──────────────────────────────────────────────────────

static void render_full_screen(void) {
    ESP_LOGI(TAG, "Rendering full screen...");

    // Clear framebuffer to white
    eink_display_clear(EINK_WHITE);

    // ── Header bar (30px) ───────────────────────────────────────────────
    draw_hline(0, HEADER_H - 1, EINK_WIDTH, EINK_BLACK);

    // Zone name (left)
    if (s_ui.zone_name[0]) {
        char trunc[48];
        truncate_to_fit(s_ui.zone_name, trunc, sizeof(trunc), 500, &eink_font_16);
        eink_font_draw_string(5, 7, trunc, &eink_font_16, EINK_BLACK, 0xFF);
    }

    // Playing status (right side of header)
    {
        const char *status = s_ui.playing ? "> Playing" : "|| Paused";
        if (!s_ui.online) status = "-- Offline";
        if (s_ui.message[0]) status = s_ui.message;
        int sw = eink_font_string_width(status, &eink_font_16);
        eink_font_draw_string(EINK_WIDTH - sw - 5, 7, status, &eink_font_16, EINK_BLACK, 0xFF);
    }

    // ── Artwork area (left side, 440x440) ───────────────────────────────
    if (s_ui.art_dirty && s_ui.image_key[0]) {
        render_artwork();
        s_ui.art_dirty = false;
    } else if (!s_ui.image_key[0]) {
        // No artwork — draw placeholder
        fill_rect(ART_X, ART_Y, ART_SIZE, ART_SIZE, EINK_WHITE);
        draw_hline(ART_X, ART_Y, ART_SIZE, EINK_BLACK);
        draw_hline(ART_X, ART_Y + ART_SIZE - 1, ART_SIZE, EINK_BLACK);
        for (int i = 0; i < ART_SIZE; i++) {
            eink_display_set_pixel(ART_X, ART_Y + i, EINK_BLACK);
            eink_display_set_pixel(ART_X + ART_SIZE - 1, ART_Y + i, EINK_BLACK);
        }
        eink_font_draw_string(ART_X + 140, ART_Y + 200, "No Artwork",
                              &eink_font_24, EINK_BLACK, 0xFF);
    }

    // ── Text area (right side) ──────────────────────────────────────────

    // Track title (large)
    if (s_ui.track[0]) {
        char trunc[80];
        truncate_to_fit(s_ui.track, trunc, sizeof(trunc), TEXT_W, &eink_font_32);
        eink_font_draw_string(TEXT_X, TEXT_Y, trunc, &eink_font_32, EINK_BLACK, 0xFF);
    }

    // Artist
    if (s_ui.artist[0]) {
        char trunc[80];
        truncate_to_fit(s_ui.artist, trunc, sizeof(trunc), TEXT_W, &eink_font_24);
        eink_font_draw_string(TEXT_X, TEXT_Y + 45, trunc, &eink_font_24, EINK_BLACK, 0xFF);
    }

    // Album (blue)
    if (s_ui.album[0]) {
        char trunc[80];
        truncate_to_fit(s_ui.album, trunc, sizeof(trunc), TEXT_W, &eink_font_24);
        eink_font_draw_string(TEXT_X, TEXT_Y + 80, trunc, &eink_font_24, EINK_BLUE, 0xFF);
    }

    // ── Separator + transport hint ──────────────────────────────────────
    draw_hline(TEXT_X, ART_Y + ART_SIZE - 80, TEXT_W, EINK_BLACK);
    eink_font_draw_string(TEXT_X + 10, ART_Y + ART_SIZE - 60,
                          "BOOT:Prev  GP4:Play  PWR:Next",
                          &eink_font_16, EINK_BLACK, 0xFF);

    // Volume
    if (s_ui.volume > -999.0f) {
        char vol_str[32];
        snprintf(vol_str, sizeof(vol_str), "Vol: %.0f dB", s_ui.volume);
        int vw = eink_font_string_width(vol_str, &eink_font_16);
        eink_font_draw_string(TEXT_X + TEXT_W - vw, ART_Y + ART_SIZE - 100,
                              vol_str, &eink_font_16, EINK_BLACK, 0xFF);
    }

    // ── Network status banner ───────────────────────────────────────────
    if (s_ui.network_status[0]) {
        int ns_y = ART_Y + 160;
        fill_rect(TEXT_X, ns_y, TEXT_W, 30, EINK_YELLOW);
        eink_font_draw_string(TEXT_X + 5, ns_y + 7, s_ui.network_status,
                              &eink_font_16, EINK_BLACK, 0xFF);
    }

    // ── Footer bar ──────────────────────────────────────────────────────
    int footer_y = EINK_HEIGHT - FOOTER_H;
    draw_hline(0, footer_y, EINK_WIDTH, EINK_BLACK);
    eink_font_draw_string(5, footer_y + 2, "hiphi frame v0.1",
                          &eink_font_16, EINK_BLACK, 0xFF);

    // Refresh the physical display
    eink_display_refresh();
    ESP_LOGI(TAG, "Full screen render complete");
}

// ── Public API (called from bridge_client via dispatch macros) ───────────────

void eink_ui_init(void) {
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.volume = -999.0f;  // Sentinel: no volume yet

    // Don't draw a boot screen — each refresh takes ~16s of flickering.
    // Wait for real UI state to arrive, then do ONE render.
    s_ui.dirty = true;
    s_ui.initial_draw_done = false;
    s_ui.last_render = 0;  // Allow first render immediately after debounce
    s_ui.last_change = platform_millis();

    ESP_LOGI(TAG, "E-ink UI initialized");
}

void eink_ui_set_status(bool online) {
    if (s_ui.online != online) {
        s_ui.online = online;
        s_ui.dirty = true;
        s_ui.last_change = platform_millis();
    }
}

void eink_ui_set_message(const char *msg) {
    if (!msg) msg = "";
    if (strcmp(s_ui.message, msg) != 0) {
        snprintf(s_ui.message, sizeof(s_ui.message), "%s", msg);
        s_ui.dirty = true;
        s_ui.last_change = platform_millis();
    }
}

void eink_ui_set_zone_name(const char *name) {
    if (!name) name = "";
    if (strcmp(s_ui.zone_name, name) != 0) {
        snprintf(s_ui.zone_name, sizeof(s_ui.zone_name), "%s", name);
        s_ui.dirty = true;
        s_ui.last_change = platform_millis();
    }
}

void eink_ui_set_network_status(const char *status) {
    if (!status) status = "";
    if (strcmp(s_ui.network_status, status) != 0) {
        snprintf(s_ui.network_status, sizeof(s_ui.network_status), "%s", status);
        s_ui.dirty = true;
        s_ui.last_change = platform_millis();
    }
}

void eink_ui_set_artwork(const char *image_key) {
    if (!image_key) image_key = "";
    if (strcmp(s_ui.image_key, image_key) != 0) {
        snprintf(s_ui.image_key, sizeof(s_ui.image_key), "%s", image_key);
        s_ui.art_dirty = true;
        s_ui.dirty = true;
        s_ui.last_change = platform_millis();
    }
}

void eink_ui_show_volume_change(float vol, float vol_step) {
    if (fabsf(s_ui.volume - vol) >= 1.0f) {
        s_ui.volume = vol;
        s_ui.volume_step = vol_step;
        s_ui.dirty = true;
        s_ui.last_change = platform_millis();
    }
}

void eink_ui_update(const char *line1, const char *line2, bool playing,
                    float volume, float volume_min, float volume_max,
                    float volume_step, int seek_position, int length) {
    (void)volume_min; (void)volume_max; (void)seek_position; (void)length;

    bool changed = false;

    if (line1 && strcmp(s_ui.track, line1) != 0) {
        snprintf(s_ui.track, sizeof(s_ui.track), "%s", line1);
        changed = true;
    }
    if (line2 && strcmp(s_ui.artist, line2) != 0) {
        snprintf(s_ui.artist, sizeof(s_ui.artist), "%s", line2);
        changed = true;
    }
    if (s_ui.playing != playing) {
        s_ui.playing = playing;
        changed = true;
    }
    if (fabsf(s_ui.volume - volume) >= 1.0f) {
        s_ui.volume = volume;
        changed = true;
    }

    if (changed) {
        s_ui.dirty = true;
        s_ui.last_change = platform_millis();
    }
}

// Zone picker stubs (simplified for e-ink — full implementation later)
void eink_ui_show_zone_picker(void) {}
void eink_ui_hide_zone_picker(void) {}
bool eink_ui_is_zone_picker_visible(void) { return false; }
void eink_ui_zone_picker_scroll(int delta) { (void)delta; }
void eink_ui_zone_picker_get_selected_id(char *out, size_t len) {
    if (out && len > 0) out[0] = '\0';
}
bool eink_ui_zone_picker_is_current_selection(void) { return true; }

void eink_ui_process(void) {
    if (!s_ui.dirty) return;

    uint64_t now = platform_millis();

    // Debounce: wait for state to settle before refreshing
    if (now - s_ui.last_change < RENDER_DEBOUNCE_MS) return;

    // Cooldown: don't refresh too often (e-ink is slow)
    // Skip cooldown for the very first render after boot
    if (s_ui.initial_draw_done && now - s_ui.last_render < RENDER_COOLDOWN_MS) return;

    s_ui.dirty = false;
    s_ui.initial_draw_done = true;
    render_full_screen();
    s_ui.last_render = platform_millis();
}

// Input handler registration
static ui_input_cb_t s_input_handler = NULL;

// Defined in platform_input_frame.c
extern void platform_input_set_handler(ui_input_cb_t cb);

void eink_ui_set_input_handler(ui_input_cb_t handler) {
    s_input_handler = handler;
    platform_input_set_handler(handler);
}

// Battery display refresh — marks dirty so next process cycle redraws
void eink_ui_update_battery(void) {
    s_ui.dirty = true;
    s_ui.last_change = platform_millis();
}

// Settings panel — noop for e-ink (no LVGL settings screen)
void eink_ui_show_settings(void) {}

// Provide symbols that bridge_client.c and app_main.c call directly
// (not through dispatch macros)
void ui_update_battery(void) { eink_ui_update_battery(); }
void ui_show_settings(void) { eink_ui_show_settings(); }
