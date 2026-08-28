// E-ink now-playing UI renderer for hiphi frame
// Replaces LVGL-based ui.c — renders directly to e-ink framebuffer

#include "eink_ui.h"
#include "eink_display.h"
#include "eink_font.h"
#include "bridge_client.h"
#include "controller_utf8.h"
#include "platform/platform_http.h"
#include "platform/platform_time.h"
#include "platform/platform_log.h"
#include "platform/platform_task.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "eink_ui";

// ── UI state ────────────────────────────────────────────────────────────────

// Art-forward layout: 800 wide x 480 tall
// Full-width artwork, slim text bar at bottom
#define TEXT_BAR_H      30   // Text bar height at bottom
#define ART_W          EINK_WIDTH                       // Full panel width
#define ART_H          (EINK_HEIGHT - TEXT_BAR_H)       // Fill above text bar
#define ART_X            0   // Flush left
#define ART_Y            0   // Flush to top
#define TEXT_Y         (EINK_HEIGHT - TEXT_BAR_H)       // Text bar at bottom
/* UHC composes directly for this viewport: up to 10% crop, then a complete
 * proportional fit on a quiet gallery mat. */
#define ART_CROP_LIMIT_PERCENT 10

// Debounce: wait 3s after last state change before rendering
#define RENDER_DEBOUNCE_MS 3000
// Minimum 180s between refreshes (Waveshare recommended minimum for panel longevity)
#define RENDER_COOLDOWN_MS 180000
#define ART_RETRY_INITIAL_MS 3000
#define ART_RETRY_MAX_MS 60000

static struct {
    char zone_name[64];
    char track[128];
    char artist[128];
    char album[128];
    char message[128];
    char network_status[128];
    char device_ip[16];
    char image_key[128];
    float volume;
    float volume_step;
    bool playing;
    bool online;
    bool ble_connected;
    bool power_state_known;
    bool show_ip;

    // Dirty flags
    bool dirty;             // Any state changed — needs re-render
    bool art_dirty;         // New artwork needs download
    bool display_pref_dirty; // User-facing display preference changed
    uint64_t last_change;   // Timestamp of last state change (for debounce)
    uint64_t last_render;   // Timestamp of last completed render (for cooldown)
    uint64_t art_retry_after; // Earliest retry after a failed artwork request
    uint32_t art_retry_delay; // Exponential retry delay for the current key
    bool initial_draw_done; // First render after boot
} s_ui;

// ── Artwork cache (persists between renders to survive framebuffer clear) ──

#define ART_CACHE_SIZE ((ART_W * ART_H) / 2)
static uint8_t *s_art_cache = NULL;  // Native 4bpp panel bytes

static void blit_art_cache(void) {
    if (!s_art_cache) return;
    uint8_t *framebuffer = eink_display_get_fb();
    if (!framebuffer) return;
    memcpy(framebuffer + (ART_Y * EINK_WIDTH + ART_X) / 2,
           s_art_cache, ART_CACHE_SIZE);
}

// ── Packed artwork download ─────────────────────────────────────────────────

static bool cache_packed_artwork(const uint8_t *img_data, size_t img_len) {
    if (img_len != ART_CACHE_SIZE) {
        ESP_LOGW(TAG, "Unexpected packed artwork size: %u (expected %u)",
                 (unsigned)img_len, (unsigned)ART_CACHE_SIZE);
        return false;
    }
    if (!s_art_cache) {
        s_art_cache = heap_caps_malloc(ART_CACHE_SIZE, MALLOC_CAP_SPIRAM);
    }
    if (!s_art_cache) {
        ESP_LOGE(TAG, "Failed to allocate packed artwork cache");
        return false;
    }
    memcpy(s_art_cache, img_data, ART_CACHE_SIZE);
    return true;
}

static bool render_artwork(void) {
    if (!s_ui.image_key[0]) return false;

    char url[512];
    char *img_data = NULL;
    size_t img_len = 0;
    const char *art_url = bridge_client_get_artwork_url_for_format_and_scale(
        url, sizeof(url), ART_W, ART_H, 0, "eink_acep6", "smart",
        ART_CROP_LIMIT_PERCENT);
    if (!art_url || !art_url[0]) {
        ESP_LOGW(TAG, "No artwork URL available");
        return false;
    }

    ESP_LOGI(TAG, "Downloading packed Frame artwork (10%% crop budget): %s", art_url);
    if (platform_http_get_image(art_url, &img_data, &img_len) != 0 || !img_data) {
        ESP_LOGE(TAG, "Packed artwork download failed");
        platform_http_free(img_data);
        return false;
    }
    const bool cached = cache_packed_artwork((const uint8_t *)img_data, img_len);
    platform_http_free(img_data);
    if (cached) ESP_LOGI(TAG, "Packed artwork ready for panel");
    return cached;
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
    int w = eink_font_string_width(src, font);
    const size_t source_bytes = strlen(src);
    if (w <= max_width && source_bytes < dst_len) {
        memcpy(dst, src, source_bytes + 1);
        return;
    }
    // Find max chars that fit with "..." suffix
    int ellipsis_w = eink_font_string_width("...", font);
    int fit_w = max_width - ellipsis_w;
    size_t bytes = 0;
    int used_width = 0;
    const char *cursor = src;
    while (*cursor && bytes < dst_len - 4) {
        const char *codepoint_start = cursor;
        const uint32_t codepoint = controller_utf8_decode_next(&cursor);
        const size_t codepoint_bytes = (size_t)(cursor - codepoint_start);
        const int codepoint_width = eink_font_codepoint_width(codepoint, font);
        if (used_width + codepoint_width > fit_w ||
            bytes + codepoint_bytes >= dst_len - 3) {
            break;
        }
        bytes += codepoint_bytes;
        used_width += codepoint_width;
    }
    memcpy(dst, src, bytes);
    dst[bytes] = '\0';
    strncat(dst, "...", dst_len - bytes - 1);
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

static bool render_full_screen(void) {
    ESP_LOGI(TAG, "Rendering full screen...");

    // ── Artwork (centered, flush to top) ─────────────────────────────────
    if (s_ui.art_dirty && s_ui.image_key[0]) {
        if (!render_artwork()) {
            ESP_LOGW(TAG, "New artwork unavailable; preserving the current panel");
            return false;
        }
        s_ui.art_dirty = false;
    }

    // If we have an image key but no cached artwork, skip the render entirely.
    // It's e-ink — whatever's on screen stays. Better than blanking it out.
    if (s_ui.image_key[0] && !s_art_cache) {
        ESP_LOGW(TAG, "No artwork cache available, skipping render to preserve display");
        return false;
    }

    // Clear framebuffer to white, then re-draw everything
    eink_display_clear(EINK_WHITE);

    if (s_art_cache) {
        // Always prefer cached artwork — even if image_key was cleared.
        // On e-ink, showing last-known art beats a blank screen.
        blit_art_cache();
    } else {
        // No artwork ever loaded — draw thin border placeholder
        draw_hline(ART_X, ART_Y, ART_W, EINK_BLACK);
        draw_hline(ART_X, ART_Y + ART_H - 1, ART_W, EINK_BLACK);
        for (int i = 0; i < ART_H; i++) {
            eink_display_set_pixel(ART_X, ART_Y + i, EINK_BLACK);
            eink_display_set_pixel(ART_X + ART_W - 1, ART_Y + i, EINK_BLACK);
        }
    }

    // ── Text bar at bottom ───────────────────────────────────────────────
    draw_hline(0, TEXT_Y, EINK_WIDTH, EINK_BLACK);

    // "Track  -  Artist  -  Album" left-aligned
    {
        char text[384];
        if (s_ui.track[0] && s_ui.artist[0] && s_ui.album[0]) {
            snprintf(text, sizeof(text), "%.100s  -  %.100s  -  %.100s",
                     s_ui.track, s_ui.artist, s_ui.album);
        } else if (s_ui.track[0] && s_ui.artist[0]) {
            snprintf(text, sizeof(text), "%.120s  -  %.120s", s_ui.track, s_ui.artist);
        } else if (s_ui.track[0]) {
            snprintf(text, sizeof(text), "%s", s_ui.track);
        } else if (s_ui.network_status[0]) {
            snprintf(text, sizeof(text), "%s", s_ui.network_status);
        } else {
            snprintf(text, sizeof(text), "No track");
        }
        const bool draw_ip = s_ui.show_ip && s_ui.device_ip[0];
        const int ip_width = draw_ip ? (int)strlen(s_ui.device_ip) * eink_font_16.width : 0;
        // Leave room for the optional address and the status icons.
        int max_text_w = EINK_WIDTH - 50 - (draw_ip ? ip_width + 12 : 0);
        char trunc[300];
        truncate_to_fit(text, trunc, sizeof(trunc), max_text_w, &eink_font_16);
        eink_font_draw_string(5, TEXT_Y + 7, trunc, &eink_font_16, EINK_BLACK, 0xFF);
        if (draw_ip) {
            eink_font_draw_string(EINK_WIDTH - 34 - ip_width, TEXT_Y + 7,
                                  s_ui.device_ip,
                                  &eink_font_16, EINK_BLACK, 0xFF);
        }
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
    return true;
}

// ── Public API (wrapped by controller_presentation_frame.c) ─────────────────

void eink_ui_init(void) {
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.volume = -999.0f;  // Sentinel: no volume yet

    // Don't render at boot — wait for artwork to arrive.
    // It's e-ink: whatever's on the panel from last time stays visible.
    s_ui.dirty = false;
    s_ui.initial_draw_done = false;
    s_ui.last_render = 0;  // Allow first render immediately after debounce
    s_ui.last_change = 0;

    ESP_LOGI(TAG, "E-ink UI initialized");
}

void eink_ui_set_status(bool online) {
    if (s_ui.online != online) {
        s_ui.online = online;
        // Don't set dirty — not worth a 20s e-ink refresh for a status icon change.
        // Piggyback on next artwork-triggered render.
    }
}

void eink_ui_set_message(const char *msg) {
    if (!msg) msg = "";
    if (strcmp(s_ui.message, msg) != 0) {
        snprintf(s_ui.message, sizeof(s_ui.message), "%s", msg);
        // Text-only change — piggyback on next artwork render
    }
}

void eink_ui_set_zone_name(const char *name) {
    if (!name) name = "";
    if (strcmp(s_ui.zone_name, name) != 0) {
        snprintf(s_ui.zone_name, sizeof(s_ui.zone_name), "%s", name);
        // Text-only change — piggyback on next artwork render
    }
}

void eink_ui_set_network_status(const char *status) {
    if (!status) status = "";
    if (strcmp(s_ui.network_status, status) != 0) {
        snprintf(s_ui.network_status, sizeof(s_ui.network_status), "%s", status);
        // Text-only — piggyback on artwork render. No point burning a 20s refresh
        // just to show "Connected" on an otherwise blank screen.
    }
}

static void set_zone_name_on_ui(void *arg) {
    char *name = arg;
    eink_ui_set_zone_name(name);
    free(name);
}

void eink_ui_post_zone_name(const char *name) {
    char *copy = strdup(name ? name : "");
    if (!copy) return;
    if (!platform_task_post_to_ui(set_zone_name_on_ui, copy)) {
        free(copy);
    }
}

static void set_network_status_on_ui(void *arg) {
    char *status = arg;
    eink_ui_set_network_status(status);
    free(status);
}

void eink_ui_post_network_status(const char *status) {
    char *copy = strdup(status ? status : "");
    if (!copy) return;
    if (!platform_task_post_to_ui(set_network_status_on_ui, copy)) {
        free(copy);
    }
}

static void set_device_ip_on_ui(void *arg) {
    char *ip = arg;
    snprintf(s_ui.device_ip, sizeof(s_ui.device_ip), "%s", ip ? ip : "");
    free(ip);
}

void eink_ui_post_device_ip(const char *ip) {
    char *copy = strdup(ip ? ip : "");
    if (!copy) return;
    if (!platform_task_post_to_ui(set_device_ip_on_ui, copy)) {
        free(copy);
    }
}

static void set_show_ip_on_ui(void *arg) {
    bool *show = arg;
    if (show) {
        if (s_ui.show_ip != *show) {
            s_ui.show_ip = *show;
            s_ui.display_pref_dirty = true;
            s_ui.dirty = true;
            s_ui.last_change = platform_millis();
        }
    }
    free(show);
}

void eink_ui_post_show_ip(bool show) {
    bool *copy = malloc(sizeof(*copy));
    if (!copy) return;
    *copy = show;
    if (!platform_task_post_to_ui(set_show_ip_on_ui, copy)) {
        free(copy);
    }
}

void eink_ui_set_artwork(const char *image_key) {
    if (!image_key) image_key = "";
    if (strcmp(s_ui.image_key, image_key) != 0) {
        snprintf(s_ui.image_key, sizeof(s_ui.image_key), "%s", image_key);
        if (image_key[0]) {
            // New artwork — trigger render
            s_ui.art_dirty = true;
            s_ui.dirty = true;
            s_ui.last_change = platform_millis();
            s_ui.art_retry_after = 0;
            s_ui.art_retry_delay = ART_RETRY_INITIAL_MS;
        } else {
            // Artwork cleared (nothing playing) — cancel any pending render.
            // It's e-ink: keep whatever's on the display rather than blanking it.
            s_ui.dirty = false;
            s_ui.art_dirty = false;
        }
    }
}

void eink_ui_show_volume_change(float vol, float vol_step) {
    // Track volume state but don't trigger e-ink refresh — volume isn't displayed
    // and a full ACeP refresh (~19s) for a volume knob turn is disruptive
    s_ui.volume = vol;
    s_ui.volume_step = vol_step;
}

void eink_ui_update(const char *line1, const char *line2, const char *line3,
                    bool playing, float volume, float volume_min,
                    float volume_max, float volume_step, int seek_position,
                    int length) {
    (void)volume_min; (void)volume_max; (void)volume_step; (void)seek_position; (void)length;

    bool changed = false;
    s_ui.power_state_known = true;

    if (line1 && strcmp(s_ui.track, line1) != 0) {
        snprintf(s_ui.track, sizeof(s_ui.track), "%s", line1);
        changed = true;
    }
    if (line2 && strcmp(s_ui.artist, line2) != 0) {
        snprintf(s_ui.artist, sizeof(s_ui.artist), "%s", line2);
        changed = true;
    }
    if (line3 && strcmp(s_ui.album, line3) != 0) {
        snprintf(s_ui.album, sizeof(s_ui.album), "%s", line3);
        changed = true;
    }
    if (s_ui.playing != playing) {
        s_ui.playing = playing;
        changed = true;
    }
    // Track volume but don't trigger refresh — volume isn't displayed on e-ink
    s_ui.volume = volume;

    // Text-only changes (track/artist/playing state) piggyback on artwork renders.
    // When artwork changes, eink_ui_set_artwork() sets dirty.
    (void)changed;
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
    const bool artwork_changed = s_ui.art_dirty && s_ui.image_key[0];
    const bool urgent_refresh = artwork_changed || s_ui.display_pref_dirty;

    if (artwork_changed && now < s_ui.art_retry_after) return;

    // A media snapshot applies metadata before its artwork key, so a new key
    // is already coherent and can begin downloading on the next UI tick.
    // Retain debounce only for non-art presentation changes.
    if (!urgent_refresh && now - s_ui.last_change < RENDER_DEBOUNCE_MS) return;

    /* A new image key is the user's strongest signal that the visible content
     * is stale. Let it bypass the general three-minute panel cooldown. The IP
     * visibility preference also bypasses it so a user action takes effect. */
    if (artwork_changed && s_ui.initial_draw_done &&
        now - s_ui.last_render < RENDER_COOLDOWN_MS) {
        ESP_LOGI(TAG, "New artwork bypassing the general render cooldown");
    }

    // Cooldown: don't refresh non-art changes too often (full refresh is slow).
    // Skip cooldown for the first render and for a genuinely changed image key.
    if (!urgent_refresh && s_ui.initial_draw_done &&
        now - s_ui.last_render < RENDER_COOLDOWN_MS) {
        // Log once when cooldown is first hit (not every 50ms loop)
        static uint64_t s_last_cooldown_log = 0;
        if (now - s_last_cooldown_log > 10000) {
            uint64_t remaining = RENDER_COOLDOWN_MS - (now - s_ui.last_render);
            ESP_LOGI(TAG, "Render pending, cooldown %ds remaining",
                     (int)(remaining / 1000));
            s_last_cooldown_log = now;
        }
        return;
    }

    if (render_full_screen()) {
        s_ui.dirty = false;
        s_ui.display_pref_dirty = false;
        s_ui.art_retry_after = 0;
        s_ui.art_retry_delay = ART_RETRY_INITIAL_MS;
        s_ui.initial_draw_done = true;
        s_ui.last_render = platform_millis();
    } else {
        // Keep the new key pending, but avoid hammering UHC on every UI tick.
        if (s_ui.art_retry_delay == 0) {
            s_ui.art_retry_delay = ART_RETRY_INITIAL_MS;
        }
        s_ui.art_retry_after = platform_millis() + s_ui.art_retry_delay;
        ESP_LOGW(TAG, "Artwork retry scheduled in %u ms",
                 (unsigned)s_ui.art_retry_delay);
        if (s_ui.art_retry_delay < ART_RETRY_MAX_MS) {
            uint32_t next_delay = s_ui.art_retry_delay * 2;
            s_ui.art_retry_delay = next_delay > ART_RETRY_MAX_MS
                                       ? ART_RETRY_MAX_MS : next_delay;
        }
    }
}

bool eink_ui_power_state_known(void) { return s_ui.power_state_known; }
bool eink_ui_is_playing(void) { return s_ui.playing; }
bool eink_ui_has_pending_refresh(void) { return s_ui.dirty; }

// BLE status — updated piggyback on next now-playing refresh, never triggers its own
void eink_ui_set_ble_status(bool connected) {
    s_ui.ble_connected = connected;
    // Don't set dirty — piggyback on next now-playing refresh
}

// Battery display refresh — noop for e-ink (battery not shown on display)
void eink_ui_update_battery(void) {
    // Don't trigger a 20s e-ink refresh for battery — it's not displayed
}

// Settings panel — noop for e-ink (no LVGL settings screen)
void eink_ui_show_settings(void) {}
