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

// Art-forward layout: 800 wide x 480 tall
// Large centered artwork, slim text bar at bottom
#define ART_SIZE       450   // Artwork square size
#define TEXT_BAR_H      30   // Text bar height at bottom
#define ART_X          ((EINK_WIDTH - ART_SIZE) / 2)   // Centered horizontally (175px margin)
#define ART_Y            0   // Flush to top
#define TEXT_Y         (EINK_HEIGHT - TEXT_BAR_H)       // Text bar at bottom

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
    bool ble_connected;

    // Dirty flags
    bool dirty;             // Any state changed — needs re-render
    bool art_dirty;         // New artwork needs download
    uint64_t last_change;   // Timestamp of last state change (for debounce)
    uint64_t last_render;   // Timestamp of last completed render (for cooldown)
    bool initial_draw_done; // First render after boot
} s_ui;

// ── Artwork cache (persists between renders to survive framebuffer clear) ──

static uint8_t *s_art_cache = NULL;  // Cached e-ink color indices, ART_SIZE*ART_SIZE bytes

static void blit_art_cache(void) {
    if (!s_art_cache) return;
    for (int y = 0; y < ART_SIZE; y++) {
        for (int x = 0; x < ART_SIZE; x++) {
            uint8_t color = s_art_cache[y * ART_SIZE + x];
            uint16_t px = ART_X + x;
            uint16_t py = ART_Y + y;
            if (px < EINK_WIDTH && py < EINK_HEIGHT) {
                eink_display_set_pixel(px, py, color);
            }
        }
    }
}

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

    // Allocate/reuse art cache for e-ink color indices
    if (!s_art_cache) {
        s_art_cache = heap_caps_malloc(ART_SIZE * ART_SIZE, MALLOC_CAP_SPIRAM);
    }

    // Write dithered pixels to framebuffer and cache
    for (int y = 0; y < ART_SIZE; y++) {
        for (int x = 0; x < ART_SIZE; x++) {
            int idx = (y * ART_SIZE + x) * 3;
            uint8_t r = dithered[idx + 0];
            uint8_t g = dithered[idx + 1];
            uint8_t b = dithered[idx + 2];
            uint8_t color = eink_nearest_color(r, g, b);
            if (s_art_cache) {
                s_art_cache[y * ART_SIZE + x] = color;
            }
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

// Truncate string to fit width, adding "..." if needed
static void truncate_to_fit(const char *src, char *dst, size_t dst_len,
                            int max_width, const eink_font_t *font) {
    int len = strlen(src);
    int w = eink_font_string_width(src, font);
    if (w <= max_width) {
        snprintf(dst, dst_len, "%s", src);
        return;
    }
    // Find max chars that fit with "..." suffix
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

// ── Status icon drawing ─────────────────────────────────────────────────────

// Draw a small Bluetooth-ish icon (8x10 pixels) at (x,y)
static void draw_ble_icon(uint16_t x, uint16_t y, uint8_t color) {
    // Simplified Bluetooth rune: vertical line with arrow tips
    for (int i = 0; i < 10; i++)
        eink_display_set_pixel(x + 3, y + i, color);  // vertical bar
    // Upper-right arrow: (4,2),(5,3),(6,4),(5,5),(4,6)
    eink_display_set_pixel(x + 4, y + 2, color);
    eink_display_set_pixel(x + 5, y + 3, color);
    eink_display_set_pixel(x + 6, y + 4, color);
    eink_display_set_pixel(x + 5, y + 5, color);
    eink_display_set_pixel(x + 4, y + 6, color);
    // Lower-left notches: (2,3),(1,4),(2,5)
    eink_display_set_pixel(x + 2, y + 3, color);
    eink_display_set_pixel(x + 1, y + 4, color);
    eink_display_set_pixel(x + 2, y + 5, color);
    // Top/bottom caps
    eink_display_set_pixel(x + 4, y + 0, color);
    eink_display_set_pixel(x + 5, y + 1, color);
    eink_display_set_pixel(x + 4, y + 8, color);
    eink_display_set_pixel(x + 5, y + 9, color);
}

// Draw a small bridge/connection icon (8x10 pixels) — a simple "link" shape
static void draw_bridge_icon(uint16_t x, uint16_t y, uint8_t color) {
    // Two interlocking chain links
    for (int i = 2; i <= 7; i++)
        eink_display_set_pixel(x + i, y + 3, color);  // top bar
    for (int i = 1; i <= 6; i++)
        eink_display_set_pixel(x + i, y + 6, color);  // bottom bar
    eink_display_set_pixel(x + 2, y + 2, color);
    eink_display_set_pixel(x + 2, y + 4, color);
    eink_display_set_pixel(x + 7, y + 2, color);
    eink_display_set_pixel(x + 7, y + 4, color);
    eink_display_set_pixel(x + 1, y + 5, color);
    eink_display_set_pixel(x + 1, y + 7, color);
    eink_display_set_pixel(x + 6, y + 5, color);
    eink_display_set_pixel(x + 6, y + 7, color);
}

// ── Full screen render ──────────────────────────────────────────────────────

static void render_full_screen(void) {
    ESP_LOGI(TAG, "Rendering full screen...");

    // Clear framebuffer to white
    eink_display_clear(EINK_WHITE);

    // ── Artwork (centered, flush to top) ─────────────────────────────────
    if (s_ui.art_dirty && s_ui.image_key[0]) {
        render_artwork();
        s_ui.art_dirty = false;
    } else if (s_ui.image_key[0] && s_art_cache) {
        // Re-blit cached artwork (framebuffer was cleared above)
        blit_art_cache();
    } else if (!s_ui.image_key[0]) {
        // No artwork — draw thin border placeholder
        draw_hline(ART_X, ART_Y, ART_SIZE, EINK_BLACK);
        draw_hline(ART_X, ART_Y + ART_SIZE - 1, ART_SIZE, EINK_BLACK);
        for (int i = 0; i < ART_SIZE; i++) {
            eink_display_set_pixel(ART_X, ART_Y + i, EINK_BLACK);
            eink_display_set_pixel(ART_X + ART_SIZE - 1, ART_Y + i, EINK_BLACK);
        }
    }

    // ── Text bar at bottom ───────────────────────────────────────────────
    draw_hline(0, TEXT_Y, EINK_WIDTH, EINK_BLACK);

    // "Track — Artist" left-aligned
    {
        char text[256];
        if (s_ui.track[0] && s_ui.artist[0]) {
            snprintf(text, sizeof(text), "%.120s  -  %.120s", s_ui.track, s_ui.artist);
        } else if (s_ui.track[0]) {
            snprintf(text, sizeof(text), "%s", s_ui.track);
        } else if (s_ui.network_status[0]) {
            snprintf(text, sizeof(text), "%s", s_ui.network_status);
        } else {
            snprintf(text, sizeof(text), "No track");
        }
        // Truncate to fit (leave 40px right margin for status icons)
        int max_text_w = EINK_WIDTH - 50;
        char trunc[200];
        truncate_to_fit(text, trunc, sizeof(trunc), max_text_w, &eink_font_16);
        eink_font_draw_string(5, TEXT_Y + 7, trunc, &eink_font_16, EINK_BLACK, 0xFF);
    }

    // Status icons (bottom-right) — piggyback on now-playing refreshes only
    {
        int icon_x = EINK_WIDTH - 12;
        int icon_y = TEXT_Y + 10;

        // Bridge connectivity — always visible, red when offline
        draw_bridge_icon(icon_x, icon_y, s_ui.online ? EINK_BLACK : EINK_RED);

        // BLE remote connection — always visible, red when disconnected
        icon_x -= 14;
        draw_ble_icon(icon_x, icon_y, s_ui.ble_connected ? EINK_BLACK : EINK_RED);
    }

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

// BLE status — updated piggyback on next now-playing refresh, never triggers its own
void eink_ui_set_ble_status(bool connected) {
    s_ui.ble_connected = connected;
    // Don't set dirty — piggyback on next now-playing refresh
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
