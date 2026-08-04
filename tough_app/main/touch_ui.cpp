#include "touch_ui.h"

#include "controller_action.h"
#include "controller_config.h"
#include "controller_input.h"
#include "bridge_client.h"
#include "m5_platform.h"
#include "platform/platform_http.h"
#include "platform/platform_task.h"
#include "platform/platform_identity.h"
#include "wifi_manager.h"

#include <M5Unified.h>

#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>

static const char *TAG = "m5_ui";

namespace {

constexpr int kMaxZones = 32;
constexpr int kMaxText = 128;
constexpr int kMaxZoneId = 64;
constexpr int kArtworkWidth = 240;
constexpr int kArtworkHeight = 240;
constexpr uint8_t kNormalBrightness = 180;
constexpr uint8_t kDimBrightness = 28;

enum class PowerState : uint8_t { Normal, Art, Dim, Sleep };

struct UiState {
    char zone[kMaxText] = "No Zone";
    char track[kMaxText] = "";
    char artist[kMaxText] = "";
    char album[kMaxText] = "";
    char message[kMaxText] = "";
    char network[kMaxText] = "";
    char zone_names[kMaxZones][kMaxText] = {};
    char zone_ids[kMaxZones][kMaxZoneId] = {};
    int zone_count = 0;
    int zone_selected = -1;
    int zone_current = -1;
    int zone_offset = 0;
    int zone_drag_remainder = 0;
    int zone_touch_last_y = 0;
    bool zone_touch_active = false;
    bool zone_touch_dragged = false;
    float volume = 0.0f;
    float volume_step = 1.0f;
    bool online = false;
    bool playing = false;
    bool picker = false;
    bool settings = false;
    /* A mode switch must consume the gesture that caused it. M5Unified can
     * report press/click/release across adjacent update cycles; without a
     * short quarantine, the newly shown mode sees that same gesture and
     * immediately toggles back. */
    int64_t consume_touch_until_us = 0;
    int64_t art_reentry_block_until_us = 0;
    bool dirty = true;
    char artwork_key[kMaxText] = {};
    uint16_t *artwork_pixels = nullptr;
    int artwork_width = 0;
    int artwork_height = 0;
    PowerState power_state = PowerState::Normal;
    bool charging = false;
    uint32_t art_timeout_sec = 0;
    uint32_t dim_timeout_sec = 0;
    uint32_t sleep_timeout_sec = 0;
    int64_t power_state_started_us = 0;
    int64_t last_activity_us = 0;
    int64_t message_until_us = 0;
};

UiState s_state;
std::atomic_bool s_artwork_loading{false};
std::atomic_bool s_display_sleeping{false};
M5Canvas s_canvas(&M5.Display);
lgfx::LovyanGFX *s_draw_target = &M5.Display;
bool s_canvas_ready = false;

void copy_text(char *out, size_t out_len, const char *in) {
    if (!out || out_len == 0) return;
    snprintf(out, out_len, "%s", in ? in : "");
}

void dispatch_command(controller_command_kind_t kind) {
    controller_action_t action =
        controller_action_command(controller_command_make(kind));
    (void)controller_input_dispatch_action(&action);
}

void dispatch_simple(controller_action_kind_t kind) {
    controller_action_t action = controller_action_simple(kind);
    (void)controller_input_dispatch_action(&action);
}

void dispatch_volume(int direction) {
    controller_action_t action = controller_action_command(
        controller_command_adjust_volume(direction));
    (void)controller_input_dispatch_action(&action);
}

void draw_text(const char *text, int x, int y, int size, uint32_t color) {
    s_draw_target->setTextSize(size);
    s_draw_target->setTextColor(color);
    s_draw_target->setTextDatum(lgfx::top_left);
    s_draw_target->drawString(text ? text : "", x, y);
}

struct MarqueeState {
    int x = 0;
    int y = 0;
    int width = 0;
    int size = 0;
    char value[256] = {};
    int64_t started_us = 0;
};

static MarqueeState s_marquees[32];

void draw_scrolling_text(const char *text, int x, int y, int width, int size,
                         uint32_t color) {
    const char *value = text ? text : "";
    s_draw_target->setTextSize(size);
    MarqueeState *marquee = nullptr;
    for (MarqueeState &candidate : s_marquees) {
        if (candidate.started_us != 0 && candidate.x == x && candidate.y == y &&
            candidate.width == width && candidate.size == size) {
            marquee = &candidate;
            break;
        }
    }
    if (!marquee) {
        for (MarqueeState &candidate : s_marquees) {
            if (candidate.started_us == 0) {
                marquee = &candidate;
                break;
            }
        }
    }
    if (marquee) {
        if (marquee->started_us == 0 || strcmp(marquee->value, value) != 0) {
            strncpy(marquee->value, value, sizeof(marquee->value) - 1);
            marquee->value[sizeof(marquee->value) - 1] = '\0';
            marquee->x = x;
            marquee->y = y;
            marquee->width = width;
            marquee->size = size;
            marquee->started_us = esp_timer_get_time();
        }
    }
    const int text_width = s_draw_target->textWidth(value);
    if (text_width <= width) {
        draw_text(value, x, y, size, color);
        return;
    }
    const int gap = 28;
    const int cycle = text_width + gap;
    const int travel_ms = std::max(1, cycle * 1000 / 34);
    const int pause_ms = 700;
    const int64_t started_us = marquee ? marquee->started_us : esp_timer_get_time();
    const int64_t phase_ms = ((esp_timer_get_time() - started_us) / 1000) %
                             (2 * pause_ms + travel_ms);
    int offset = 0;
    if (phase_ms >= pause_ms && phase_ms < pause_ms + travel_ms) {
        offset = std::min(cycle, static_cast<int>(
            (phase_ms - pause_ms) * 34 / 1000));
    } else if (phase_ms >= pause_ms + travel_ms) {
        /* Hold at the seam. The second copy is aligned at x, so resetting to
         * offset 0 shows the same pixels rather than jumping backward. */
        offset = cycle;
    }
    s_draw_target->setClipRect(x, y, width, size * 9 + 4);
    draw_text(value, x - offset, y, size, color);
    draw_text(value, x - offset + cycle, y, size, color);
    s_draw_target->clearClipRect();
}

void draw_center(const char *text, int x, int y, int size, uint32_t color) {
    s_draw_target->setTextSize(size);
    s_draw_target->setTextColor(color);
    s_draw_target->setTextDatum(lgfx::middle_center);
    s_draw_target->drawString(text ? text : "", x, y);
}

void draw_button(int x, int y, int w, int h, const char *label,
                uint32_t color) {
    s_draw_target->fillRoundRect(x, y, w, h, 8, color);
    draw_center(label, x + w / 2, y + h / 2, 2, 0xffffff);
}

struct ArtworkJob {
    char key[kMaxText];
};

struct ArtworkResult {
    char key[kMaxText];
    uint16_t *pixels;
    int width;
    int height;
};

void start_artwork_fetch(void);

void apply_artwork_result(void *arg) {
    ArtworkResult *result = static_cast<ArtworkResult *>(arg);
    if (!result) {
        s_artwork_loading.store(false);
        return;
    }

    const bool current = strcmp(result->key, s_state.artwork_key) == 0;
    if (current && result->pixels) {
        free(s_state.artwork_pixels);
        s_state.artwork_pixels = result->pixels;
        s_state.artwork_width = result->width;
        s_state.artwork_height = result->height;
        s_state.dirty = true;
        ESP_LOGI(TAG, "Artwork ready for '%s' (%dx%d)", result->key,
                 result->width, result->height);
        result->pixels = nullptr;
    }
    free(result->pixels);
    free(result);
    s_artwork_loading.store(false);
    if (!current && s_state.artwork_key[0]) {
        start_artwork_fetch();
    }
}

void artwork_fetch_task(void *arg) {
    ArtworkJob *job = static_cast<ArtworkJob *>(arg);
    if (!job) {
        s_artwork_loading.store(false);
        vTaskDelete(nullptr);
        return;
    }

    char url[384];
    const char *art_url = bridge_client_get_artwork_url_for_format(
        url, sizeof(url), kArtworkWidth, kArtworkHeight, 0, "rgb565");
    if (art_url) {
        /* The bridge endpoint is keyed by zone and dimensions, while the
         * response changes with the now-playing image key. Ensure an HTTP
         * cache cannot hand us the previous track's pixels. The bridge may
         * ignore this query parameter; proxies still see a distinct URL. */
        uint32_t cache_key = 2166136261u;
        for (const unsigned char *p =
                 reinterpret_cast<const unsigned char *>(job->key); *p; ++p) {
            cache_key ^= *p;
            cache_key *= 16777619u;
        }
        const size_t url_len = strlen(url);
        if (url_len < sizeof(url)) {
            snprintf(url + url_len, sizeof(url) - url_len,
                     "&cache_bust=%08" PRIx32, cache_key);
        }
        ESP_LOGI(TAG, "Fetching artwork key '%s'", job->key);
    }
    char *raw = nullptr;
    size_t raw_len = 0;
    const size_t expected = static_cast<size_t>(kArtworkWidth) *
                            static_cast<size_t>(kArtworkHeight) * sizeof(uint16_t);
    bool posted = false;
    if (art_url && platform_http_get_image(art_url, &raw, &raw_len) == 0 &&
        raw && raw_len == expected) {
        ArtworkResult *result = static_cast<ArtworkResult *>(
            calloc(1, sizeof(*result)));
        uint16_t *pixels = static_cast<uint16_t *>(malloc(expected));
        if (result && pixels) {
            memcpy(pixels, raw, expected);
            copy_text(result->key, sizeof(result->key), job->key);
            result->pixels = pixels;
            result->width = kArtworkWidth;
            result->height = kArtworkHeight;
            if (platform_task_post_to_ui(apply_artwork_result, result)) {
                posted = true;
                result = nullptr;
                pixels = nullptr;
            }
        }
        free(pixels);
        free(result);
    } else {
        ESP_LOGW(TAG, "Artwork fetch failed or returned %zu bytes (expected %zu)",
                 raw_len, expected);
    }
    platform_http_free(raw);
    free(job);
    if (!posted) {
        s_artwork_loading.store(false);
    }
    vTaskDelete(nullptr);
}

void start_artwork_fetch(void) {
    if (!s_state.artwork_key[0] ||
        s_artwork_loading.exchange(true)) {
        return;
    }
    ArtworkJob *job = static_cast<ArtworkJob *>(calloc(1, sizeof(*job)));
    if (!job) {
        s_artwork_loading.store(false);
        return;
    }
    copy_text(job->key, sizeof(job->key), s_state.artwork_key);
    if (platform_task_start_internal_stack("m5_artwork", 16384,
                                           artwork_fetch_task, job) != 0) {
        free(job);
        s_artwork_loading.store(false);
        ESP_LOGW(TAG, "Could not start artwork worker");
    }
}

uint16_t dim_rgb565(uint16_t pixel) {
    const uint16_t r = (pixel >> 11) & 0x1f;
    const uint16_t g = (pixel >> 5) & 0x3f;
    const uint16_t b = pixel & 0x1f;
    return static_cast<uint16_t>(((r * 42 / 100) << 11) |
                                 ((g * 42 / 100) << 5) |
                                 (b * 42 / 100));
}

void draw_artwork_scaled(int x, int y, int width, int height, bool muted) {
    if (!s_state.artwork_pixels || s_state.artwork_width <= 0 ||
        s_state.artwork_height <= 0 || width <= 0 || height <= 0) {
        return;
    }
    uint16_t row[320];
    /* Scale-to-cover, then center-crop. Independent X/Y scaling visibly
     * stretches album art, especially in the 320x240 Art mode. */
    int crop_width = s_state.artwork_width;
    int crop_height = s_state.artwork_height;
    if (width * s_state.artwork_height > height * s_state.artwork_width) {
        crop_height = std::max(1, s_state.artwork_width * height / width);
    } else {
        crop_width = std::max(1, s_state.artwork_height * width / height);
    }
    const int src_x0 = (s_state.artwork_width - crop_width) / 2;
    const int src_y0 = (s_state.artwork_height - crop_height) / 2;
    for (int dy = 0; dy < height; ++dy) {
        const int sy = src_y0 + (dy * crop_height) / height;
        const uint16_t *src = s_state.artwork_pixels + sy * s_state.artwork_width;
        for (int dx = 0; dx < width; ++dx) {
            const int sx = src_x0 + (dx * crop_width) / width;
            uint16_t pixel = src[sx];
            row[dx] = muted ? dim_rgb565(pixel) : pixel;
        }
        s_draw_target->pushImage(x, y + dy, width, 1, row);
    }
}

void set_power_state(PowerState state) {
    if (s_state.power_state == state) {
        return;
    }
    const PowerState previous = s_state.power_state;
    s_state.power_state = state;
    s_display_sleeping.store(state == PowerState::Sleep);
    ESP_LOGI(TAG, "Power state -> %s",
             state == PowerState::Normal ? "normal" :
             state == PowerState::Art ? "art" :
             state == PowerState::Dim ? "dim" : "sleep");
    s_state.power_state_started_us = esp_timer_get_time();
    switch (state) {
    case PowerState::Normal:
        m5_platform_display_wake();
        m5_platform_set_brightness(kNormalBrightness);
        break;
    case PowerState::Art:
        m5_platform_display_wake();
        m5_platform_set_brightness(kNormalBrightness);
        break;
    case PowerState::Dim:
        m5_platform_display_wake();
        m5_platform_set_brightness(kDimBrightness);
        break;
    case PowerState::Sleep:
        m5_platform_display_sleep();
        break;
    }
    if (state == PowerState::Normal && previous != PowerState::Normal) {
        bridge_client_request_poll();
    }
    s_state.dirty = true;
}

void reset_activity(void) {
    const int64_t now = esp_timer_get_time();
    s_state.last_activity_us = now;
    if (s_state.power_state != PowerState::Normal) {
        set_power_state(PowerState::Normal);
    } else {
        s_state.power_state_started_us = now;
    }
}

void update_power_state(void) {
    const int64_t now = esp_timer_get_time();
    if (s_state.power_state == PowerState::Sleep) {
        return;
    }
    const uint32_t timeout = s_state.power_state == PowerState::Normal
        ? (s_state.artwork_key[0] && bridge_client_is_ready_for_art_mode()
               ? s_state.art_timeout_sec
               : s_state.dim_timeout_sec)
        : (s_state.power_state == PowerState::Art
               ? s_state.dim_timeout_sec
               : s_state.sleep_timeout_sec);
    if (timeout == 0 || now - s_state.power_state_started_us <
            static_cast<int64_t>(timeout) * 1000000) {
        return;
    }
    if (s_state.power_state == PowerState::Normal) {
        if (s_state.artwork_key[0] && s_state.art_timeout_sec > 0 &&
            bridge_client_is_ready_for_art_mode()) {
            set_power_state(PowerState::Art);
        } else if (s_state.dim_timeout_sec > 0) {
            set_power_state(PowerState::Dim);
        } else if (s_state.sleep_timeout_sec > 0) {
            set_power_state(PowerState::Sleep);
        }
    } else if (s_state.power_state == PowerState::Art) {
        if (s_state.dim_timeout_sec > 0) {
            set_power_state(PowerState::Dim);
        } else if (s_state.sleep_timeout_sec > 0) {
            set_power_state(PowerState::Sleep);
        }
    } else if (s_state.power_state == PowerState::Dim) {
        if (s_state.sleep_timeout_sec > 0) {
            set_power_state(PowerState::Sleep);
        }
    }
}

void draw_main(void) {
    const int w = static_cast<int>(m5_platform_display_width());
    const int h = static_cast<int>(m5_platform_display_height());
    s_draw_target->fillScreen(0x101018);
    if (s_state.artwork_pixels) {
        draw_artwork_scaled(8, 34, 112, 100, false);
        s_draw_target->drawRoundRect(8, 34, 112, 100, 6, 0x5a5a68);
    }

    draw_scrolling_text(s_state.zone, 10, 7, w - 34, 2, 0xfafafa);
    s_draw_target->fillCircle(w - 15, 15, 5, s_state.online ? 0x00c853 : 0x666666);
    if (!s_state.artwork_pixels) {
        s_draw_target->drawRoundRect(8, 34, 112, 100, 6, 0x3a3a48);
        draw_center("No artwork", 64, 82, 1, 0x888888);
    }
    draw_scrolling_text(s_state.track, 132, 42, w - 142, 2, 0xfafafa);
    draw_scrolling_text(s_state.artist, 132, 69, w - 142, 1, 0xaaaaaa);
    draw_scrolling_text(s_state.album, 132, 88, w - 142, 1, 0x888888);

    char volume[20];
    if (s_state.volume_step > 0.0f && s_state.volume_step < 1.0f) {
        snprintf(volume, sizeof(volume), "%.1f", static_cast<double>(s_state.volume));
    } else {
        snprintf(volume, sizeof(volume), "%.0f", static_cast<double>(s_state.volume));
    }
    draw_button(18, 140, 52, 32, "-", 0x28344a);
    draw_center(volume, w / 2, 156, 2, 0xfafafa);
    draw_button(w - 70, 140, 52, 32, "+", 0x28344a);

    draw_button(18, h - 62, 72, 44, "<<", 0x28344a);
    draw_button(w / 2 - 38, h - 66, 76, 50, s_state.playing ? "||" : ">", 0x00695c);
    draw_button(w - 90, h - 62, 72, 44, ">>", 0x28344a);

    if (s_state.network[0]) {
        draw_center(s_state.network, w / 2, h - 8, 1, 0xffb74d);
    }
    if (s_state.message[0] && esp_timer_get_time() < s_state.message_until_us) {
        draw_center(s_state.message, w / 2, 108, 1, 0xffd54f);
    }
}

void draw_art_mode(void) {
    const int w = static_cast<int>(m5_platform_display_width());
    const int h = static_cast<int>(m5_platform_display_height());
    s_draw_target->fillScreen(0x080808);
    if (s_state.artwork_pixels) draw_artwork_scaled(0, 0, w, h, false);
    s_draw_target->fillRectAlpha(0, h - 34, w, 34, 190, 0x101018);
    draw_scrolling_text(s_state.track, 10, h - 29, w - 20, 1, 0xfafafa);
    draw_scrolling_text(s_state.artist, 10, h - 14, w - 20, 1, 0xaaaaaa);
}

void draw_provisioning(void) {
    const int w = static_cast<int>(m5_platform_display_width());
    const int h = static_cast<int>(m5_platform_display_height());
    s_draw_target->fillScreen(0x101018);
    draw_center("WI-FI SETUP", w / 2, 16, 2, 0xfafafa);
    draw_center("Connect to:", w / 2, 45, 1, 0xaaaaaa);
    draw_center(platform_provisioning_ssid(), w / 2, 64, 2, 0x4fc3f7);
    draw_center("Open 192.168.4.1", w / 2, 91, 1, 0xfafafa);
    draw_center("Choose a network + enter password", w / 2, 111, 1, 0xaaaaaa);
    rk_wifi_network_t scan[6] = {};
    const rk_wifi_scan_state_t scan_state = wifi_mgr_scan_state();
    if (scan_state == RK_WIFI_SCAN_IDLE || scan_state == RK_WIFI_SCAN_FAILED) {
        (void)wifi_mgr_scan_start();
    }
    const size_t count = scan_state == RK_WIFI_SCAN_READY
                             ? wifi_mgr_scan_results_copy(scan, 6)
                             : 0;
    int y = 135;
    if (count == 0) {
        draw_center("Scanning...", w / 2, y, 1, 0xffd54f);
    } else {
        for (size_t i = 0; i < count && y < h - 10; ++i, y += 16) {
            char line[224];
            snprintf(line, sizeof(line), "%s  %d dBm", scan[i].ssid,
                     (int)scan[i].rssi);
            draw_text(line, 12, y, 1, 0xcccccc);
        }
    }
}

void draw_picker(void) {
    const int w = static_cast<int>(m5_platform_display_width());
    const int h = static_cast<int>(m5_platform_display_height());
    s_draw_target->fillScreen(0x080808);
    draw_center("SELECT ZONE", w / 2, 13, 2, 0xfafafa);
    const int row_h = 30;
    const int first_y = 34;
    const int visible = std::max(1, (h - first_y - 5) / row_h);
    for (int row = 0; row < visible; ++row) {
        const int index = s_state.zone_offset + row;
        if (index >= s_state.zone_count) break;
        const int y = first_y + row * row_h;
        const uint32_t bg = index == s_state.zone_selected ? 0x2a4a6a : 0x181818;
        s_draw_target->fillRoundRect(6, y, w - 18, row_h - 3, 5, bg);
        draw_scrolling_text(s_state.zone_names[index], 14, y + 7, w - 32, 1,
                            0xfafafa);
    }
    if (s_state.zone_count > visible) {
        const int track_y = first_y;
        const int track_h = h - first_y - 12;
        const int thumb_h = std::max(18, track_h * visible / s_state.zone_count);
        const int max_offset = std::max(1, s_state.zone_count - visible);
        const int thumb_y = track_y +
            (track_h - thumb_h) * s_state.zone_offset / max_offset;
        s_draw_target->fillRoundRect(w - 9, track_y, 4, track_h, 2, 0x383848);
        s_draw_target->fillRoundRect(w - 9, thumb_y, 4, thumb_h, 2, 0x9ecbff);
        draw_center("swipe", w / 2, h - 7, 1, 0x888888);
    }
}

void draw_settings(void) {
    const int w = static_cast<int>(m5_platform_display_width());
    const int h = static_cast<int>(m5_platform_display_height());
    s_draw_target->fillScreen(0x080808);
    draw_center("SETTINGS", w / 2, 18, 2, 0xfafafa);
    controller_config_snapshot_t snapshot = {};
    char ip[32] = "(none)";
    char text[256];
    char ssid[33] = "(none)";
    char bridge[128] = "(mDNS auto-discovery)";
    if (controller_config_snapshot(&snapshot)) {
        copy_text(ssid, sizeof(ssid), snapshot.value.ssid[0] ? snapshot.value.ssid : "(none)");
        copy_text(bridge, sizeof(bridge), snapshot.value.bridge_base[0] ? snapshot.value.bridge_base : "(mDNS auto-discovery)");
    }
    wifi_mgr_get_ip(ip, sizeof(ip));
    const esp_app_desc_t *desc = esp_app_get_description();
    snprintf(text, sizeof(text), "WiFi: %s\nIP: %s\nBridge: %s\nVersion: %s",
             ssid, ip, bridge, desc->version);
    int y = 58;
    for (char *line = strtok(text, "\n"); line; line = strtok(nullptr, "\n")) {
        draw_scrolling_text(line, 12, y, w - 24, 1, 0xcccccc);
        y += 20;
    }
    draw_button(10, h - 48, 140, 34, "FORGET WIFI", 0x7f1d1d);
    draw_button(w - 150, h - 48, 140, 34, "CLOSE", 0x28344a);
}

void redraw(void) {
    if (!s_state.dirty) return;
    s_draw_target->startWrite();
    if (s_state.settings) draw_settings();
    else if (s_state.picker) draw_picker();
    else if (wifi_mgr_is_ap_mode()) draw_provisioning();
    else if (s_state.power_state == PowerState::Art) draw_art_mode();
    else draw_main();
    s_draw_target->endWrite();
    if (s_canvas_ready) {
        s_canvas.pushSprite(&M5.Display, 0, 0);
    }
    s_state.dirty = false;
}

void handle_touch(const m5_platform_touch_event_t &event) {
    const int w = static_cast<int>(m5_platform_display_width());
    const int h = static_cast<int>(m5_platform_display_height());
    if (event.state == M5_PLATFORM_TOUCH_NONE) {
        return;
    }
    if (s_state.consume_touch_until_us != 0) {
        const int64_t now = esp_timer_get_time();
        if (now < s_state.consume_touch_until_us) return;
        s_state.consume_touch_until_us = 0;
        if (event.state != M5_PLATFORM_TOUCH_PRESSED) return;
    }
    if (s_state.power_state == PowerState::Sleep) {
        reset_activity();
        return;
    }
    if (s_state.power_state == PowerState::Art) {
        if (event.state == M5_PLATFORM_TOUCH_CLICKED ||
            event.state == M5_PLATFORM_TOUCH_HELD) {
            set_power_state(PowerState::Normal);
            s_state.consume_touch_until_us = esp_timer_get_time() + 500000;
            s_state.art_reentry_block_until_us = esp_timer_get_time() + 1000000;
        }
        return;
    }
    reset_activity();
    if (s_state.picker) {
        if (event.state == M5_PLATFORM_TOUCH_PRESSED) {
            s_state.zone_touch_active = true;
            s_state.zone_touch_dragged = false;
            s_state.zone_touch_last_y = event.y;
            s_state.zone_drag_remainder = 0;
        } else if ((event.state == M5_PLATFORM_TOUCH_DRAGGING ||
                    event.delta_y != 0) && s_state.zone_touch_active) {
            constexpr int row_h = 30;
            const int delta_y = event.y - s_state.zone_touch_last_y;
            s_state.zone_touch_last_y = event.y;
            s_state.zone_drag_remainder -= delta_y;
            if (std::abs(delta_y) >= 2) s_state.zone_touch_dragged = true;
            while (s_state.zone_drag_remainder >= row_h) {
                ++s_state.zone_offset;
                s_state.zone_drag_remainder -= row_h;
            }
            while (s_state.zone_drag_remainder <= -row_h) {
                --s_state.zone_offset;
                s_state.zone_drag_remainder += row_h;
            }
            const int visible = std::max(
                1, (h - 34 - 5) / row_h);
            const int max_offset = std::max(0, s_state.zone_count - visible);
            s_state.zone_offset = std::clamp(s_state.zone_offset, 0, max_offset);
            s_state.dirty = true;
        } else if (event.state == M5_PLATFORM_TOUCH_RELEASED) {
            /* A drag release must never select the row under the finger. */
            s_state.zone_drag_remainder = 0;
            s_state.zone_touch_active = false;
        } else if (event.state == M5_PLATFORM_TOUCH_CLICKED) {
            const int row = (event.y - 34) / 30;
            const int index = s_state.zone_offset + row;
            if (!s_state.zone_touch_dragged && event.y >= 34 && index >= 0 &&
                index < s_state.zone_count) {
                s_state.zone_selected = index;
                dispatch_simple(CONTROLLER_ACTION_SELECT_ZONE_PICKER);
            }
            s_state.zone_touch_active = false;
            s_state.zone_touch_dragged = false;
        }
        return;
    }
    if (s_state.settings) {
        if (event.state == M5_PLATFORM_TOUCH_CLICKED && event.y > h - 65) {
            if (event.x < w / 2) wifi_mgr_forget_wifi();
            else s_state.settings = false;
            s_state.dirty = true;
        }
        return;
    }
    if (event.state == M5_PLATFORM_TOUCH_HELD && event.y < 110) {
        s_state.settings = true;
        s_state.dirty = true;
        return;
    }
    if (event.state != M5_PLATFORM_TOUCH_CLICKED) return;
    if (event.y < 38) {
        /* Open through the shared action router so the renderer receives the
         * bridge-owned zone list (including its shared label fallback). */
        dispatch_simple(CONTROLLER_ACTION_OPEN_ZONE_PICKER);
    } else if (event.y >= 34 && event.y < 134 && event.x < 126 &&
               s_state.artwork_pixels &&
               esp_timer_get_time() >= s_state.art_reentry_block_until_us) {
        set_power_state(PowerState::Art);
        s_state.consume_touch_until_us = esp_timer_get_time() + 500000;
    } else if (event.y >= 135 && event.y < 178 && event.x < 95) {
        dispatch_volume(-1);
    } else if (event.y >= 135 && event.y < 178 && event.x > w - 95) {
        dispatch_volume(1);
    } else if (event.y > h - 78 && event.x < 105) {
        dispatch_command(CONTROLLER_COMMAND_PREVIOUS_TRACK);
    } else if (event.y > h - 78 && event.x < w - 105) {
        dispatch_command(CONTROLLER_COMMAND_TOGGLE_PLAYBACK);
    } else if (event.y > h - 78) {
        dispatch_command(CONTROLLER_COMMAND_NEXT_TRACK);
    }
}

void set_zone_on_ui(void *arg) {
    char *value = static_cast<char *>(arg);
    copy_text(s_state.zone, sizeof(s_state.zone), value);
    s_state.dirty = true;
    free(value);
}

void set_network_on_ui(void *arg) {
    char *value = static_cast<char *>(arg);
    copy_text(s_state.network, sizeof(s_state.network), value);
    s_state.dirty = true;
    free(value);
}

void set_artwork_impl(const char *image_key) {
    const char *key = image_key ? image_key : "";
    if (strcmp(s_state.artwork_key, key) == 0) {
        return;
    }
    copy_text(s_state.artwork_key, sizeof(s_state.artwork_key), key);
    if (!s_state.artwork_key[0]) {
        free(s_state.artwork_pixels);
        s_state.artwork_pixels = nullptr;
        s_state.artwork_width = 0;
        s_state.artwork_height = 0;
        s_state.dirty = true;
        return;
    }
    /* Keep the old image visible while the new one downloads. */
    s_state.dirty = true;
    start_artwork_fetch();
}

void set_artwork_on_ui(void *arg) {
    char *value = static_cast<char *>(arg);
    set_artwork_impl(value);
    free(value);
}

void post_text(const char *value, platform_task_fn_t fn) {
    char *copy = strdup(value ? value : "");
    if (!copy || !platform_task_post_to_ui(fn, copy)) free(copy);
}

}  // namespace

extern "C" void touch_ui_init(void) {
    s_state = UiState{};
    s_display_sleeping.store(false);
    s_canvas.setColorDepth(16);
    s_canvas.setSwapBytes(true);
    s_canvas_ready = s_canvas.createSprite(m5_platform_display_width(),
                                           m5_platform_display_height()) != nullptr;
    if (s_canvas_ready) {
        s_draw_target = &s_canvas;
    }
    s_state.dirty = true;
    s_state.power_state_started_us = esp_timer_get_time();
    s_state.last_activity_us = s_state.power_state_started_us;
    m5_platform_set_brightness(kNormalBrightness);
    ESP_LOGI(TAG, "M5GFX touch UI initialized for %s", m5_platform_board_name());
}

extern "C" void touch_ui_process(void) {
    m5_platform_update();
    platform_task_run_pending();
    m5_platform_touch_event_t event = {};
    if (m5_platform_touch_event(&event)) handle_touch(event);
    update_power_state();
    /* Marquee text is time-based; repaint only while there is content that
     * can actually move, keeping ordinary control frames inexpensive. */
    if (s_state.track[0] || s_state.artist[0] || s_state.album[0]) {
        s_state.dirty = true;
    }
    redraw();
}

extern "C" void touch_ui_set_status(bool online) {
    s_state.online = online;
    s_state.dirty = true;
}

extern "C" void touch_ui_set_message(const char *msg) {
    copy_text(s_state.message, sizeof(s_state.message), msg);
    s_state.message_until_us = esp_timer_get_time() + 4000000;
    s_state.dirty = true;
}

extern "C" void touch_ui_set_zone_name(const char *name) {
    copy_text(s_state.zone, sizeof(s_state.zone), name && name[0] ? name : "No Zone");
    s_state.dirty = true;
}

extern "C" void touch_ui_set_network_status(const char *status) {
    copy_text(s_state.network, sizeof(s_state.network), status);
    s_state.dirty = true;
}

extern "C" void touch_ui_post_zone_name(const char *name) {
    post_text(name, set_zone_on_ui);
}

extern "C" void touch_ui_post_network_status(const char *status) {
    post_text(status, set_network_on_ui);
}

extern "C" void touch_ui_set_artwork(const char *image_key) {
    set_artwork_impl(image_key);
}

extern "C" void touch_ui_post_artwork(const char *image_key) {
    post_text(image_key, set_artwork_on_ui);
}

extern "C" void touch_ui_show_volume_change(float volume, float step) {
    s_state.volume = volume;
    s_state.volume_step = step;
    s_state.dirty = true;
}

extern "C" void touch_ui_update(const char *line1, const char *line2,
                                  const char *line3, bool playing,
                                  float volume, float volume_min, float volume_max,
                                  float volume_step, int seek_position, int length) {
    (void)volume_min;
    (void)volume_max;
    (void)seek_position;
    (void)length;
    copy_text(s_state.track, sizeof(s_state.track), line1);
    copy_text(s_state.artist, sizeof(s_state.artist), line2);
    copy_text(s_state.album, sizeof(s_state.album), line3);
    s_state.playing = playing;
    touch_ui_show_volume_change(volume, volume_step);
}

extern "C" void touch_ui_show_zone_picker(const char **names,
                                            const char **ids, int count,
                                            int selected_idx) {
    s_state.zone_count = std::clamp(count, 0, kMaxZones);
    s_state.zone_selected = std::clamp(selected_idx, -1, s_state.zone_count - 1);
    s_state.zone_current = s_state.zone_selected;
    s_state.zone_offset = 0;
    s_state.zone_drag_remainder = 0;
    for (int i = 0; i < s_state.zone_count; ++i) {
        copy_text(s_state.zone_ids[i], sizeof(s_state.zone_ids[i]),
                  ids && ids[i] ? ids[i] : "");
        copy_text(s_state.zone_names[i], sizeof(s_state.zone_names[i]),
                  names && names[i] ? names[i] : "(unnamed zone)");
    }
    ESP_LOGI(TAG, "Zone picker populated with %d entries (selected=%d)",
             s_state.zone_count, s_state.zone_selected);
    s_state.picker = true;
    s_state.dirty = true;
}

extern "C" void touch_ui_hide_zone_picker(void) {
    s_state.picker = false;
    s_state.dirty = true;
}

extern "C" bool touch_ui_is_zone_picker_visible(void) { return s_state.picker; }

extern "C" void touch_ui_zone_picker_scroll(int delta) {
    const int visible = std::max(1, (static_cast<int>(m5_platform_display_height()) - 34 - 5) / 30);
    s_state.zone_offset = std::clamp(s_state.zone_offset + delta, 0,
                                     std::max(0, s_state.zone_count - visible));
    s_state.dirty = true;
}

extern "C" void touch_ui_zone_picker_get_selected_id(char *out, size_t len) {
    if (!out || len == 0) return;
    out[0] = '\0';
    if (s_state.picker && s_state.zone_selected >= 0 &&
        s_state.zone_selected < s_state.zone_count) {
        copy_text(out, len, s_state.zone_ids[s_state.zone_selected]);
    }
}

extern "C" bool touch_ui_zone_picker_is_current_selection(void) {
    return s_state.zone_selected == s_state.zone_current;
}

extern "C" void touch_ui_update_battery(void) {}

extern "C" void touch_ui_apply_display_config(const rk_cfg_t *cfg,
                                                 bool is_charging) {
    if (!cfg) {
        return;
    }
    s_state.charging = is_charging;
    s_state.art_timeout_sec = rk_cfg_get_art_mode_timeout(cfg, is_charging);
    s_state.dim_timeout_sec = rk_cfg_get_dim_timeout(cfg, is_charging);
    s_state.sleep_timeout_sec = rk_cfg_get_sleep_timeout(cfg, is_charging);
    reset_activity();
    ESP_LOGI(TAG, "Power policy: art=%us dim=%us sleep=%us charging=%s",
             (unsigned)s_state.art_timeout_sec,
             (unsigned)s_state.dim_timeout_sec,
             (unsigned)s_state.sleep_timeout_sec,
             is_charging ? "yes" : "no");
}

extern "C" bool touch_ui_is_display_sleeping(void) {
    return s_display_sleeping.load();
}

extern "C" void touch_ui_show_settings(void) {
    s_state.settings = true;
    s_state.dirty = true;
}
