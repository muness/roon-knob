#include "touch_ui.h"

#include "bridge_client.h"
#include "controller_action_router.h"
#include "controller_config.h"
#include "controller_input.h"
#include "m5_platform.h"
#include "platform/platform_http.h"
#include "platform/platform_identity.h"
#include "platform/platform_task.h"
#include "wifi_manager.h"

#include <M5Unified.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_app_desc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr const char *TAG = "atom_ui";
constexpr int W = 128;
constexpr int H = 128;
constexpr int THUMB = 72;
constexpr int FULL_ART = 128;
constexpr int ART_TILE_ROWS = 4;
constexpr size_t ART_TILE_BYTES = static_cast<size_t>(FULL_ART) * ART_TILE_ROWS *
                                  sizeof(uint16_t);
constexpr int TOP_STRIP_H = 18;
constexpr int METADATA_Y = 90;
constexpr int METADATA_H = H - METADATA_Y;
constexpr int MAX_TEXT = 96;
constexpr int DEADZONE = 28;
constexpr int64_t VOLUME_REPEAT_INITIAL_US = 220000;
constexpr int64_t VOLUME_REPEAT_INTERVAL_US = 100000;
constexpr int64_t ACTION_ACK_DURATION_US = 1400000;

int axis_direction(uint16_t value) {
    if (value < 128 - DEADZONE) return -1;
    if (value > 128 + DEADZONE) return 1;
    return 0;
}

struct State {
    char zone[MAX_TEXT] = "No Zone";
    char track[MAX_TEXT] = "No track";
    char artist[MAX_TEXT] = "";
    char album[MAX_TEXT] = "";
    char network[MAX_TEXT] = "";
    char artwork_key[MAX_TEXT] = "";
    char zone_names[24][MAX_TEXT] = {};
    char zone_ids[24][MAX_TEXT] = {};
    int zone_count = 0;
    int zone_selected = -1;
    int zone_current = -1;
    int zone_offset = 0;
    float volume = 0;
    float volume_min = -80;
    float volume_max = 0;
    float volume_step = 1;
    int seek_position = 0;
    int seek_length = 0;
    int64_t seek_updated_us = 0;
    bool playing = false;
    bool online = false;
    bool picker = false;
    bool settings = false;
    bool art_mode = false;
    bool art_surface_needs_clear = false;
    bool dirty = true;
    bool charging = false;
    uint8_t last_buttons = 0;
    int64_t right_press_started_us = 0;
    bool right_hold_fired = false;
    int64_t top_right_started_us = 0;
    bool top_right_hold_fired = false;
    bool suppress_top_right_release_open = false;
    int64_t last_input_us = 0;
    int volume_repeat_direction = 0;
    int64_t volume_next_repeat_us = 0;
    int64_t power_started_us = 0;
    uint32_t dim_timeout = 0;
    uint32_t sleep_timeout = 0;
    bool sleeping = false;
    bool wifi_scan_started = false;
    bool wifi_scan_frame_drawn = false;
    rk_wifi_scan_state_t wifi_scan_state = RK_WIFI_SCAN_IDLE;
    int64_t last_draw_us = 0;
    int64_t last_artwork_retry_us = 0;
    bool artwork_available = false;
    bool artwork_visible = false;
    char action_ack[MAX_TEXT] = {};
    int64_t action_ack_until_us = 0;
    bool action_ack_volume = false;
};

State s;
std::atomic_bool s_artwork_loading{false};
std::atomic_uint32_t s_artwork_generation{0};
lgfx::LovyanGFX *s_draw_target = &M5.Display;
SemaphoreHandle_t s_display_mutex = nullptr;
lgfx::LGFX_Sprite s_top_strip(&M5.Display);
lgfx::LGFX_Sprite s_metadata_band(&M5.Display);
bool s_region_buffers_ready = false;
bool s_region_buffers_failed = false;

bool ensure_region_buffers(void) {
    if (s_region_buffers_ready) return true;
    if (s_region_buffers_failed) return false;

    s_top_strip.setColorDepth(16);
    s_metadata_band.setColorDepth(16);
    if (!s_top_strip.createSprite(W, TOP_STRIP_H) ||
        !s_metadata_band.createSprite(W, METADATA_H)) {
        s_top_strip.deleteSprite();
        s_metadata_band.deleteSprite();
        s_region_buffers_failed = true;
        ESP_LOGW(TAG, "region sprite allocation failed");
        return false;
    }
    s_top_strip.setTextFont(1);
    s_top_strip.setTextWrap(false);
    s_metadata_band.setTextFont(1);
    s_metadata_band.setTextWrap(false);
    s_region_buffers_ready = true;
    return true;
}

void copy_text(char *out, size_t len, const char *in) {
    if (out && len) snprintf(out, len, "%s", in ? in : "");
}

bool command(controller_command_t command) {
    controller_action_t action = controller_action_command(command);
    return controller_input_dispatch_action(&action);
}

bool button_action(controller_action_kind_t kind) {
    controller_action_t action = controller_action_simple(kind);
    return controller_input_dispatch_action(&action);
}

void acknowledge(const char *text) {
    if (!text || s.art_mode) return;
    copy_text(s.action_ack, sizeof(s.action_ack), text);
    s.action_ack_volume = false;
    s.action_ack_until_us = esp_timer_get_time() + ACTION_ACK_DURATION_US;
    s.dirty = true;
}

void fetch_artwork(void);

struct ArtworkResult {
    char key[MAX_TEXT];
    int width;
    int height;
    uint32_t generation;
    bool available;
};

struct ArtworkRequest {
    char key[MAX_TEXT];
    uint32_t generation;
    bool art_mode;
};

void artwork_applied(void *arg) {
    ArtworkResult *r = static_cast<ArtworkResult *>(arg);
    if (!r) return;
    const bool current = r->generation == s_artwork_generation.load() &&
                         strcmp(r->key, s.artwork_key) == 0;
    const int desired_size = s.art_mode ? FULL_ART : THUMB;
    const bool desired = current && r->width == desired_size &&
                         r->height == desired_size;
    if (desired && r->available) {
        s.artwork_available = true;
        /* The worker already painted the tiles into the display. A separate
         * visibility flag lets full-screen settings screens invalidate those
         * pixels without retaining a second copy of the artwork. */
        s.artwork_visible = true;
        s.dirty = true;
    }
    const uint32_t owner = s_artwork_generation.load();
    if (owner == r->generation) {
        s_artwork_loading.store(false);
    } else {
        /* The old worker is the only owner that can still be running: a new
         * worker cannot start while the loading latch is set. Release that
         * old ownership before starting the current generation. */
        s_artwork_loading.store(false);
    }
    const bool needs_current_fetch = (!current || !desired) &&
                                     s.artwork_key[0] &&
                                     !s.artwork_visible;
    const bool stale_owner = owner != r->generation;
    free(r);
    if ((needs_current_fetch || stale_owner) && s.artwork_key[0] &&
        !s.artwork_visible) {
        /* A track change can arrive while the previous image is streaming.
         * Do not wait for the periodic retry window to fetch the current key. */
        fetch_artwork();
    }
}

struct ArtworkStream {
    uint8_t tile[ART_TILE_BYTES] = {};
    int width = 0;
    int height = 0;
    int dest_x = 0;
    int dest_y = 0;
    size_t tile_capacity = 0;
    size_t tile_bytes = 0;
    size_t total_bytes = 0;
    int tile_y = 0;
    uint32_t generation = 0;
};

int artwork_stream_chunk(const void *data, size_t len, void *ctx) {
    ArtworkStream *stream = static_cast<ArtworkStream *>(ctx);
    if (!stream || !data || len == 0) return -1;
    if (s_artwork_generation.load() != stream->generation) return -1;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    if (stream->total_bytes == 0 && len >= 2 && bytes[0] == 0x1f &&
        bytes[1] == 0x8b) {
        return -1;
    }
    while (len > 0) {
        const size_t copy_len = std::min(len, stream->tile_capacity - stream->tile_bytes);
        memcpy(stream->tile + stream->tile_bytes, bytes, copy_len);
        stream->tile_bytes += copy_len;
        stream->total_bytes += copy_len;
        bytes += copy_len;
        len -= copy_len;
        if (stream->tile_bytes == stream->tile_capacity) {
            if (stream->tile_y >= stream->height) return -1;
            M5.Display.pushImage(stream->dest_x, stream->dest_y + stream->tile_y,
                                 stream->width, ART_TILE_ROWS,
                                 reinterpret_cast<const uint16_t *>(stream->tile));
            stream->tile_y += ART_TILE_ROWS;
            stream->tile_bytes = 0;
        }
    }
    return 0;
}

void artwork_task(void *arg) {
    ArtworkRequest request = {};
    memcpy(&request, arg, sizeof(request));
    free(arg);
    const char *key = request.key;
    const uint32_t generation = request.generation;
    const bool requested_art_mode = request.art_mode;
    const int width = requested_art_mode ? FULL_ART : THUMB;
    const int height = width;
    const int dest_x = requested_art_mode ? 0 : 4;
    const int dest_y = requested_art_mode ? 0 : 18;
    const size_t expected = static_cast<size_t>(width) * height * sizeof(uint16_t);
    bool fetched = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        ArtworkStream stream;
        stream.width = width;
        stream.height = height;
        stream.dest_x = dest_x;
        stream.dest_y = dest_y;
        stream.generation = generation;
        stream.tile_capacity = static_cast<size_t>(width) * ART_TILE_ROWS *
                                sizeof(uint16_t);
        size_t raw_len = 0;
        bool display_locked = false;
        if (s_display_mutex &&
            xSemaphoreTake(s_display_mutex, portMAX_DELAY) == pdTRUE) {
            display_locked = true;
            M5.Display.startWrite();
        }
        const int http_result = bridge_client_stream_artwork(
            key, width, height, "rgb565", expected, artwork_stream_chunk, &stream,
            &raw_len);
        const bool success = http_result == 0 && raw_len == expected &&
                             stream.tile_y == height && stream.tile_bytes == 0;
        fetched = success;
        if (success && generation == s_artwork_generation.load() &&
            requested_art_mode == s.art_mode &&
            strcmp(key, s.artwork_key) == 0) {
            /* Publish visibility before releasing the display mutex. The UI
             * may redraw immediately after the tile transaction; without
             * this ordering it can clear the freshly rendered artwork before
             * the queued completion callback runs. */
            s.artwork_available = true;
            s.artwork_visible = true;
        }
        if (display_locked) {
            M5.Display.endWrite();
            xSemaphoreGive(s_display_mutex);
        }
        ESP_LOGI(TAG, "artwork fetch result=%d bytes=%u", http_result,
                 static_cast<unsigned>(raw_len));
        if (success) break;
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    ArtworkResult *r = static_cast<ArtworkResult *>(calloc(1, sizeof(*r)));
    if (r) {
        copy_text(r->key, sizeof(r->key), key);
        r->width = width;
        r->height = height;
        r->generation = generation;
        r->available = fetched;
        if (platform_task_post_to_ui(artwork_applied, r)) r = nullptr;
        free(r);
    } else if (generation == s_artwork_generation.load()) {
        s_artwork_loading.store(false);
    }
    vTaskDelete(nullptr);
}

void fetch_artwork(void) {
    if (!s.artwork_key[0] || s_artwork_loading.exchange(true)) return;
    struct ArtworkRequest {
        char key[MAX_TEXT];
        uint32_t generation;
        bool art_mode;
    };
    ArtworkRequest *request = static_cast<ArtworkRequest *>(calloc(1, sizeof(*request)));
    if (request) {
        copy_text(request->key, sizeof(request->key), s.artwork_key);
        request->generation = s_artwork_generation.load();
        request->art_mode = s.art_mode;
    }
    if (!request || platform_task_start_internal_stack("atom_art", 8192,
                                                       artwork_task, request) != 0) {
        ESP_LOGW(TAG, "artwork worker start failed");
        free(request);
        s_artwork_loading.store(false);
    }
}

void draw_text(const char *text, int x, int y, int size, uint32_t color) {
    s_draw_target->setTextSize(size);
    // In M5GFX the one-color overload leaves the background transparent. The
    // two-color overload is intentionally not used here because each region
    // is already painted atomically from its backing sprite.
    s_draw_target->setTextColor(color);
    s_draw_target->setTextDatum(lgfx::top_left);
    s_draw_target->drawString(text ? text : "", x, y);
}

void draw_clipped_text(const char *text, int x, int y, int width, int size,
                       uint32_t color) {
    int32_t old_x = 0;
    int32_t old_y = 0;
    int32_t old_w = 0;
    int32_t old_h = 0;
    s_draw_target->getClipRect(&old_x, &old_y, &old_w, &old_h);
    s_draw_target->setTextSize(size);
    s_draw_target->setClipRect(x, y, width, s_draw_target->fontHeight());
    draw_text(text, x, y, size, color);
    s_draw_target->setClipRect(old_x, old_y, old_w, old_h);
}

struct MarqueeState {
    int x = 0;
    int y = 0;
    int width = 0;
    int size = 0;
    char value[MAX_TEXT] = {};
    int64_t started_us = 0;
};

// Three metadata rows plus the transient acknowledgement need independent
// phase state.  Keep these keyed by their geometry so an overlay never steals
// and restarts a scrolling metadata row.
MarqueeState s_marquees[4];

bool metadata_marquee_needed(void) {
    if (s.art_mode || s.picker || s.settings || wifi_mgr_is_ap_mode() ||
        s.sleeping) return false;
    s_draw_target->setTextSize(1);
    return s_draw_target->textWidth(s.track) > W - 6 ||
           s_draw_target->textWidth(s.artist) > W - 6 ||
           s_draw_target->textWidth(s.album) > W - 6;
}

int current_seek_position(void) {
    if (s.seek_length <= 0 || s.seek_position < 0) return 0;
    int position = s.seek_position;
    if (s.playing && s.seek_updated_us > 0) {
        position += static_cast<int>((esp_timer_get_time() - s.seek_updated_us) /
                                     1000000);
    }
    return std::max(0, std::min(s.seek_length, position));
}

void draw_seek_indicator(int y) {
    const int width = 120;
    s_draw_target->fillRect(4, y, width, 2, 0x1e293b);
    if (s.seek_length <= 0) return;
    const int position = current_seek_position();
    const int filled = std::max(0, std::min(width,
        static_cast<int>((static_cast<int64_t>(position) * width) /
                         s.seek_length)));
    if (filled > 0) s_draw_target->fillRect(4, y, filled, 2, 0x38bdf8);
}

void draw_ellipsized_text(const char *text, int x, int y, int width, int size,
                          uint32_t color) {
    const char *value = text ? text : "";
    s_draw_target->setTextSize(size);
    if (s_draw_target->textWidth(value) <= width) {
        draw_text(value, x, y, size, color);
        return;
    }
    char bounded[MAX_TEXT] = {};
    copy_text(bounded, sizeof(bounded), value);
    while (bounded[0] && s_draw_target->textWidth(bounded) > width) {
        const size_t len = strlen(bounded);
        if (len <= 3) break;
        bounded[len - 1] = '\0';
    }
    if (strlen(bounded) > 3) {
        bounded[strlen(bounded) - 3] = '.';
        bounded[strlen(bounded) - 2] = '.';
        bounded[strlen(bounded) - 1] = '.';
    }
    draw_clipped_text(bounded, x, y, width, size, color);
}

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
            if (candidate.started_us == 0) { marquee = &candidate; break; }
        }
    }
    if (marquee && (marquee->started_us == 0 || strcmp(marquee->value, value) != 0)) {
        copy_text(marquee->value, sizeof(marquee->value), value);
        marquee->x = x;
        marquee->y = y;
        marquee->width = width;
        marquee->size = size;
        marquee->started_us = esp_timer_get_time();
    }
    const int text_width = s_draw_target->textWidth(value);
    if (text_width <= width) { draw_text(value, x, y, size, color); return; }
    const int gap = 18;
    const int cycle = text_width + gap;
    const int travel_ms = std::max(1, cycle * 1000 / 34);
    const int pause_ms = 700;
    const int64_t started_us = marquee ? marquee->started_us : esp_timer_get_time();
    const int64_t phase_ms = ((esp_timer_get_time() - started_us) / 1000) %
                             (2 * pause_ms + travel_ms);
    int offset = 0;
    if (phase_ms >= pause_ms && phase_ms < pause_ms + travel_ms) {
        offset = std::min(cycle, static_cast<int>((phase_ms - pause_ms) * 34 / 1000));
    } else if (phase_ms >= pause_ms + travel_ms) {
        offset = cycle;
    }
    int32_t old_x = 0;
    int32_t old_y = 0;
    int32_t old_w = 0;
    int32_t old_h = 0;
    s_draw_target->getClipRect(&old_x, &old_y, &old_w, &old_h);
    s_draw_target->setClipRect(x, y, width, s_draw_target->fontHeight());
    draw_text(value, x - offset, y, size, color);
    draw_text(value, x - offset + cycle, y, size, color);
    s_draw_target->setClipRect(old_x, old_y, old_w, old_h);
}

void draw_art_mode(void) {
    /* Artwork mode is deliberately a pure image surface. If the stream is
     * unavailable, redraw() has already cleared the display; do not replace
     * that honest blank state with metadata or a placeholder label. */
}

void draw_action_ack(int y_origin) {
    if (s.art_mode || s.action_ack_until_us == 0) return;
    const int64_t now = esp_timer_get_time();
    if (now >= s.action_ack_until_us) return;

    /* Feedback is composed into the metadata band, never painted over the
     * streamed thumbnail or as a second pass over the display. */
    if (s.action_ack_volume) {
        draw_text(s.action_ack, 4, y_origin + 4, 1, 0xf8fafc);
        const float range = s.volume_max - s.volume_min;
        const float fraction = range > 0.0f
                                   ? (s.volume - s.volume_min) / range
                                   : 0.0f;
        const int bar = std::max(0, std::min(120,
            static_cast<int>(fraction * 120.0f)));
        s_draw_target->fillRect(4, y_origin + 21, 120, 5, 0x1e293b);
        s_draw_target->fillRect(4, y_origin + 21, bar, 5, 0x38bdf8);
        char min_text[24] = {};
        snprintf(min_text, sizeof(min_text), "MIN %.1f", s.volume_min);
        draw_text(min_text, 4, y_origin + 29, 1, 0x94a3b8);
        char max_text[24] = {};
        snprintf(max_text, sizeof(max_text), "MAX %.1f", s.volume_max);
        draw_text(max_text, 78, y_origin + 29, 1, 0x94a3b8);
    } else {
        draw_clipped_text(s.action_ack, 4, y_origin + 14, W - 8, 1,
                          0xf8fafc);
    }
}

void draw_top_strip(void) {
    if (!ensure_region_buffers()) {
        draw_ellipsized_text(s.zone, 3, 7, W - 6, 1,
                             s.online ? 0x7dd3fc : 0xf87171);
        return;
    }
    lgfx::LovyanGFX *previous = s_draw_target;
    s_draw_target = &s_top_strip;
    s_top_strip.fillScreen(0x08111d);
    draw_ellipsized_text(s.zone, 3, 7, W - 6, 1,
                         s.online ? 0x7dd3fc : 0xf87171);
    s_draw_target = previous;
    s_top_strip.pushSprite(&M5.Display, 0, 0);
}

void draw_metadata_band(void) {
    if (!ensure_region_buffers()) {
        draw_scrolling_text(s.track, 3, 94, W - 6, 1, 0xf8fafc);
        draw_scrolling_text(s.artist, 3, 106, W - 6, 1, 0xaaaaaa);
        draw_scrolling_text(s.album, 3, 112, W - 6, 1, 0x888888);
        draw_seek_indicator(125);
        return;
    }
    lgfx::LovyanGFX *previous = s_draw_target;
    s_draw_target = &s_metadata_band;
    const bool ack_active = s.action_ack_until_us != 0 &&
                            esp_timer_get_time() < s.action_ack_until_us;
    s_metadata_band.fillScreen(ack_active ? 0x102a43 : 0x08111d);
    if (ack_active) {
        draw_action_ack(0);
    } else {
        draw_scrolling_text(s.track, 3, 4, W - 6, 1, 0xf8fafc);
        draw_scrolling_text(s.artist, 3, 16, W - 6, 1, 0xaaaaaa);
        draw_scrolling_text(s.album, 3, 22, W - 6, 1, 0x888888);
        draw_seek_indicator(36);
    }
    s_draw_target = previous;
    s_metadata_band.pushSprite(&M5.Display, 0, METADATA_Y);
}

void draw_wifi_setup(void) {
    s_draw_target->fillScreen(0x08111d);
    draw_text("WI-FI SETUP", 4, 6, 1, 0x7dd3fc);
    draw_text("JOIN", 4, 28, 1, 0x94a3b8);
    draw_text(platform_provisioning_ssid(), 4, 42, 1, 0xf8fafc);
    draw_text("192.168.4.1", 4, 62, 1, 0x38bdf8);
    draw_text("Choose network on web", 4, 82, 1, 0x94a3b8);
    rk_wifi_network_t scan[2] = {};
    const rk_wifi_scan_state_t scan_state = wifi_mgr_scan_state();
    if (!s.wifi_scan_started &&
        (scan_state == RK_WIFI_SCAN_IDLE || scan_state == RK_WIFI_SCAN_FAILED)) {
        (void)wifi_mgr_scan_start();
        s.wifi_scan_started = true;
    }
    const size_t count = scan_state == RK_WIFI_SCAN_READY
                             ? wifi_mgr_scan_results_copy(scan, 2) : 0;
    if (count == 0) {
        draw_text("Scanning...", 4, 103, 1, 0xfbbf24);
    } else {
        for (size_t i = 0; i < count; ++i) {
            draw_text(scan[i].ssid, 4, 103 + static_cast<int>(i) * 12, 1,
                      0xe2e8f0);
        }
    }
}

void redraw(void) {
    if (!s.dirty) return;
    const bool wifi_scanning = wifi_mgr_is_ap_mode() &&
                               wifi_mgr_scan_state() == RK_WIFI_SCAN_RUNNING;
    if (wifi_scanning && s.wifi_scan_frame_drawn) {
        // Hold the static setup frame while the radio is scanning; repeated
        // full-frame SPI transfers compete with the scan and visibly flicker.
        s.dirty = false;
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    if (s.last_draw_us != 0 && now_us - s.last_draw_us < 100000) return;
    if (!s_display_mutex || xSemaphoreTake(s_display_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_draw_target->startWrite();
    const bool normal_control = !wifi_mgr_is_ap_mode() && !s.art_mode &&
                                !s.picker && !s.settings && !s.sleeping;
    const bool clear_art_surface = s.art_surface_needs_clear;
    if (clear_art_surface) {
        /* Full-screen artwork is streamed directly to the panel, so leaving
         * that mode requires one explicit surface invalidation. Keep this
         * transition clear out of marquee and overlay redraws. */
        s_draw_target->fillScreen(0x08111d);
        s.art_surface_needs_clear = false;
    }
    // Normal updates are region updates.  Clearing the whole panel here makes
    // every marquee tick visibly flash, especially while artwork is absent.
    if (normal_control && !s.artwork_visible && !clear_art_surface) {
        s_draw_target->fillRect(0, 0, THUMB, THUMB, 0x08111d);
    } else if (!clear_art_surface && !normal_control && !s.sleeping &&
               !(s.art_mode && s.artwork_visible)) {
        s_draw_target->fillScreen(0x08111d);
    }
    if (s.sleeping) {
        s.artwork_visible = false;
        s_draw_target->endWrite();
        xSemaphoreGive(s_display_mutex);
        s.dirty = false;
        s.last_draw_us = now_us;
        return;
    }
    if (wifi_mgr_is_ap_mode()) {
        s.artwork_visible = false;
        draw_wifi_setup();
    } else if (s.art_mode) {
        draw_art_mode();
    } else if (s.picker) {
        s.artwork_visible = false;
        draw_text("SELECT ZONE", 4, 3, 1, 0x7dd3fc);
        for (int row = 0; row < 4; ++row) {
            int i = s.zone_offset + row;
            if (i >= s.zone_count) break;
            uint32_t c = i == s.zone_selected ? 0x38bdf8 : 0x9ca3af;
            draw_text(s.zone_names[i], 6, 23 + row * 24, 1, c);
        }
        draw_text("A SELECT  B BACK", 3, 113, 1, 0x64748b);
    } else if (s.settings) {
        s.artwork_visible = false;
        draw_text("SETTINGS", 5, 5, 2, 0x7dd3fc);
        controller_config_snapshot_t snapshot = {};
        char ssid[33] = "(none)";
        char ip[32] = "(none)";
        if (controller_config_snapshot(&snapshot) && snapshot.value.ssid[0]) {
            copy_text(ssid, sizeof(ssid), snapshot.value.ssid);
        }
        wifi_mgr_get_ip(ip, sizeof(ip));
        const esp_app_desc_t *desc = esp_app_get_description();
        char version[32] = {};
        snprintf(version, sizeof(version), "%s", desc->version);
        draw_text("SSID", 5, 31, 1, 0x94a3b8);
        draw_text(ssid, 5, 43, 1, 0xf8fafc);
        draw_text("IP", 5, 59, 1, 0x94a3b8);
        draw_text(ip, 5, 71, 1, 0xf8fafc);
        draw_text("VER", 5, 87, 1, 0x94a3b8);
        draw_text(version, 5, 99, 1, 0xf8fafc);
        draw_text("Hold top-right", 5, 115, 1, 0x64748b);
    } else {
        /* The display itself is the artwork cache. The changing top and
         * metadata regions are composed below; artwork remains direct to the
         * display so no framebuffer is needed. */
        draw_top_strip();
        draw_metadata_band();
    }
    s_draw_target->endWrite();
    xSemaphoreGive(s_display_mutex);
    s.dirty = false;
    s.last_draw_us = now_us;
    if (wifi_scanning) s.wifi_scan_frame_drawn = true;
}

void wake(void) {
    if (s.sleeping) {
        s.sleeping = false;
        m5_platform_display_wake();
    }
    s.power_started_us = esp_timer_get_time();
    s.dirty = true;
}

void leave_art_mode(void) {
    if (!s.art_mode) return;
    s.art_mode = false;
    s.artwork_available = false;
    s.artwork_visible = false;
    s_artwork_generation.fetch_add(1);
    s.art_surface_needs_clear = true;
    s.dirty = true;
}

void toggle_art_mode(void) {
    if (s.art_mode) {
        leave_art_mode();
    } else {
        s.art_mode = true;
        s.artwork_available = false;
        s.artwork_visible = false;
        s_artwork_generation.fetch_add(1);
    }
    s.action_ack_until_us = 0;
    s.action_ack[0] = '\0';
    s.action_ack_volume = false;
    /* The two layouts intentionally request different native dimensions.
     * Invalidate the display cache and let the existing worker stream the
     * newly requested size without allocating a full frame. */
    s.last_artwork_retry_us = 0;
    s.last_draw_us = 0;
    s.dirty = true;
    if (!s.art_mode) acknowledge("CONTROL");
    fetch_artwork();
}

void process_input(void) {
    m5_platform_surface_button_event_t surface = {};
    if (m5_platform_surface_button_event(&surface)) {
        if (surface.clicked || surface.held) wake();
        if (surface.held) {
            s.settings = !s.settings;
            s.picker = false;
            leave_art_mode();
            s.dirty = true;
        } else if (surface.clicked) {
            if (!s.picker && !s.settings) {
                if (command(controller_command_make(CONTROLLER_COMMAND_TOGGLE_PLAYBACK))) {
                    acknowledge(s.playing ? "PAUSE" : "PLAY");
                }
            }
        }
    }
    m5_platform_joystick_state_t js = {};
    if (!m5_platform_joystick_state(&js)) return;
    const int64_t now = esp_timer_get_time();
    const int left_x = axis_direction(js.left_x);
    const int left_y = axis_direction(js.left_y);
    const int right_x = axis_direction(js.right_x);
    const int right_y = axis_direction(js.right_y);
    if (left_y || right_x || right_y) {
        wake();
        // A directional gesture is never also a B-button hold. Cancel the
        // candidate so stick-up/down cannot accidentally open settings.
        if (js.right_stick_pressed) {
            s.right_press_started_us = 0;
            s.right_hold_fired = false;
        }
    }
    const uint8_t buttons = (js.top_left_pressed ? 1 : 0) |
                            (js.top_right_pressed ? 2 : 0) |
                            (js.left_stick_pressed ? 4 : 0) |
                            (js.right_stick_pressed ? 8 : 0);
    const bool picker_was_open = s.picker;
    if (buttons != s.last_buttons) {
        wake();
        const uint8_t pressed = buttons & static_cast<uint8_t>(~s.last_buttons);
        const uint8_t released = s.last_buttons & static_cast<uint8_t>(~buttons);
        if (pressed & 8) {
            s.right_press_started_us = now;
            s.right_hold_fired = false;
        }
        if (pressed & 2) {
            s.top_right_started_us = now;
            s.top_right_hold_fired = false;
        }
        const bool picker_context =
            s.picker || controller_input_get_context() ==
                            CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER;
        if (picker_context) {
            if (pressed & 4) button_action(CONTROLLER_ACTION_SELECT_ZONE_PICKER);
            if (pressed & 2) {
                /* Consume the whole physical R press. The router updates the
                 * shared context synchronously, but the release is sampled
                 * later and must not be reinterpreted as the media-page
                 * short-press that opens the picker. */
                s.suppress_top_right_release_open = true;
                s.top_right_hold_fired = true;
                if (button_action(CONTROLLER_ACTION_CLOSE_ZONE_PICKER)) {
                    acknowledge("ZONE");
                }
            }
        } else if (s.settings) {
            // Settings is toggled by holding the physical top-right button.
            // The other top button is intentionally reserved for a future
            // affordance and must not become an accidental transport action.
        } else {
            if (pressed & 1) {
                toggle_art_mode();
            }
            if (pressed & 4 &&
                command(controller_command_make(CONTROLLER_COMMAND_TOGGLE_PLAYBACK))) {
                acknowledge(s.playing ? "PAUSE" : "PLAY");
            }
        }
        if (released & 2) {
            const bool suppress_open = s.suppress_top_right_release_open;
            s.suppress_top_right_release_open = false;
            if (!suppress_open && !s.top_right_hold_fired &&
                !picker_was_open && !s.settings) {
                button_action(CONTROLLER_ACTION_OPEN_ZONE_PICKER);
                leave_art_mode();
                s.dirty = true;
            }
        }
        s.last_buttons = buttons;
    }
    if (js.top_right_pressed && s.top_right_started_us &&
        !s.top_right_hold_fired && now - s.top_right_started_us >= 1200000) {
        s.top_right_hold_fired = true;
        wake();
        s.settings = !s.settings;
        s.picker = false;
        leave_art_mode();
        s.dirty = true;
    }
    if (js.right_stick_pressed && s.right_press_started_us &&
        !s.right_hold_fired && now - s.right_press_started_us >= 1500000) {
        s.right_hold_fired = true;
        /* Zone selection is an intentional mode change. A long press on the
         * right stick opens the existing selector; ordinary stick movement
         * never persists a zone implicitly. */
        button_action(CONTROLLER_ACTION_OPEN_ZONE_PICKER);
        s.settings = false;
        leave_art_mode();
        s.dirty = true;
    }
    if (s.picker && now - s.last_input_us > 180000) {
        s.volume_repeat_direction = 0;
        s.volume_next_repeat_us = 0;
        /* Prefer the right stick for zone navigation; accept the left stick
         * as a hardware-tolerant fallback so a marginal coprocessor axis
         * cannot make the selector unreachable. */
        /* Prefer the right stick when it moves. Some Atom JoyStick firmware
         * revisions expose its physical vertical movement on the other
         * sampled axis, so accept whichever right-stick axis is active before
         * falling back to either axis of the left stick. */
        const int picker_axis = right_y != 0 ? right_y :
                                right_x != 0 ? right_x :
                                left_y != 0 ? left_y : left_x;
        if (picker_axis != 0) {
            controller_action_t action = controller_action_picker_scroll(picker_axis);
            controller_input_dispatch_action(&action);
            s.last_input_us = now;
        }
    } else if (!s.picker && !s.settings) {
        /* The JoyStick Y axis increases when pushed physically down. Map the
         * user-facing direction (up = more, down = less), not the raw ADC
         * sign. A held direction is a sequence of virtual presses: respond
         * immediately, then repeat once after a short hold and at a useful
         * steady cadence. The shared helper keeps each virtual press aligned
         * with Dial's signed volume-step policy. */
        const int volume_direction = left_y < 0 ? 1 : (left_y > 0 ? -1 : 0);
        bool volume_direction_changed = false;
        if (volume_direction == 0) {
            s.volume_repeat_direction = 0;
            s.volume_next_repeat_us = 0;
        } else if (volume_direction != s.volume_repeat_direction) {
            s.volume_repeat_direction = volume_direction;
            s.volume_next_repeat_us = now;
            volume_direction_changed = true;
        }
        if (volume_direction != 0 && now >= s.volume_next_repeat_us) {
            const int32_t volume_steps =
                controller_input_accelerated_steps(volume_direction);
            if (volume_steps) {
                if (command(controller_command_adjust_volume(volume_steps))) {
                    /* The authoritative value arrives through
                     * touch_ui_show_volume_change; this acknowledgement is
                     * deliberately not painted here to avoid showing a
                     * predicted value as confirmed. */
                }
                s.last_input_us = now;
                s.volume_next_repeat_us = now +
                    (volume_direction_changed
                         ? VOLUME_REPEAT_INITIAL_US
                         : VOLUME_REPEAT_INTERVAL_US);
            }
        }
        if (right_x < 0 && command(controller_command_make(CONTROLLER_COMMAND_PREVIOUS_TRACK))) {
            acknowledge("PREVIOUS"); s.last_input_us = now;
        }
        if (right_x > 0 && command(controller_command_make(CONTROLLER_COMMAND_NEXT_TRACK))) {
            acknowledge("NEXT"); s.last_input_us = now;
        }
    } else {
        /* Picker/settings own the sticks; never carry a media-page repeat
         * into those contexts. */
        s.volume_repeat_direction = 0;
        s.volume_next_repeat_us = 0;
    }
}

void update_power(void) {
    if (s.sleeping || !s.dim_timeout) return;
    const int64_t elapsed = esp_timer_get_time() - s.power_started_us;
    if (elapsed >= static_cast<int64_t>(s.dim_timeout) * 1000000 &&
        !s.artwork_available) {
        m5_platform_set_brightness(28);
    }
    if (s.sleep_timeout && elapsed >= static_cast<int64_t>(s.sleep_timeout) * 1000000) {
        s.sleeping = true;
        m5_platform_display_sleep();
        s.dirty = true;
    }
}
}

extern "C" void touch_ui_init(void) {
    s = State{};
    s_artwork_generation.store(0);
    s_artwork_loading.store(false);
    s.power_started_us = esp_timer_get_time();
    s_display_mutex = xSemaphoreCreateMutex();
    s_draw_target = &M5.Display;
    s_region_buffers_ready = false;
    s_region_buffers_failed = false;
    M5.Display.setTextDatum(lgfx::middle_left);
    M5.Display.setTextFont(1);
    M5.Display.setTextWrap(false);
    s.dirty = true;
}
extern "C" void touch_ui_process(void) {
    m5_platform_update();
    platform_task_run_pending();
    process_input();
    /* The shared media view intentionally deduplicates image keys. If the
     * first fetch races bridge readiness, retry the same key here rather
     * than waiting for another metadata change. */
    const int64_t now_us = esp_timer_get_time();
    if (s.action_ack_until_us != 0 && now_us >= s.action_ack_until_us) {
        s.action_ack_until_us = 0;
        s.action_ack[0] = '\0';
        s.action_ack_volume = false;
        s.dirty = true;
    }
    if (s.artwork_key[0] && !s.artwork_visible && !s_artwork_loading.load() &&
        !s.settings && !s.picker && !wifi_mgr_is_ap_mode() && !s.sleeping &&
        now_us - s.last_artwork_retry_us >= 5000000) {
        s.last_artwork_retry_us = now_us;
        fetch_artwork();
    }
    const rk_wifi_scan_state_t scan_state = wifi_mgr_scan_state();
    if (scan_state != s.wifi_scan_state) {
        s.wifi_scan_state = scan_state;
        s.wifi_scan_frame_drawn = false;
        s.dirty = true;
    }
    update_power();
    if (metadata_marquee_needed()) {
        /* Marquee animation must continue even when no artwork is available. */
        s.dirty = true;
    }
    if (s.playing && s.seek_length > 0 && !s.art_mode && !s.picker &&
        !s.settings && !wifi_mgr_is_ap_mode() && !s.sleeping) {
        s.dirty = true;
    }
    redraw();
}
extern "C" void touch_ui_set_status(bool v) { if (s.online != v) { s.online = v; s.dirty = true; } }
extern "C" void touch_ui_set_message(const char *v) { if (strcmp(s.network, v ? v : "") != 0) { copy_text(s.network, sizeof(s.network), v); s.dirty = true; } }
extern "C" void touch_ui_set_zone_name(const char *v) { if (strcmp(s.zone, v ? v : "") != 0) { copy_text(s.zone, sizeof(s.zone), v); s.dirty = true; } }
extern "C" void touch_ui_set_network_status(const char *v) { if (strcmp(s.network, v ? v : "") != 0) { copy_text(s.network, sizeof(s.network), v); s.dirty = true; } }
extern "C" void touch_ui_post_zone_name(const char *v) { char *c = strdup(v ? v : ""); platform_task_post_to_ui([](void *p){ touch_ui_set_zone_name(static_cast<char *>(p)); free(p); }, c); }
extern "C" void touch_ui_post_network_status(const char *v) { char *c = strdup(v ? v : ""); platform_task_post_to_ui([](void *p){ touch_ui_set_network_status(static_cast<char *>(p)); free(p); }, c); }
extern "C" void touch_ui_set_artwork(const char *v) {
    const char *key = v ? v : "";
    if (strcmp(s.artwork_key, key) == 0) {
        /* A transient HTTP failure must not strand the current track in NO
         * ART forever; retry when the same track is shown again. */
        if (key[0] && !s.artwork_available) fetch_artwork();
        return;
    }
    s_artwork_generation.fetch_add(1);
    copy_text(s.artwork_key, sizeof(s.artwork_key), key);
    /* Never leave the previous track's pixels presented while the new
     * request is in flight. A failed fetch should produce an honest NO ART
     * state, not stale artwork that looks like a playback update failure. */
    s.artwork_available = false;
    s.artwork_visible = false;
    s.dirty = true;
    if (!s.artwork_key[0]) {
        return;
    }
    fetch_artwork();
}
extern "C" void touch_ui_post_artwork(const char *v) { char *c = strdup(v ? v : ""); platform_task_post_to_ui([](void *p){ touch_ui_set_artwork(static_cast<char *>(p)); free(p); }, c); }
extern "C" void touch_ui_show_volume_change(float v, float step) {
    s.volume = v;
    s.volume_step = step;
    if (s.art_mode) return;
    char volume_text[32] = {};
    snprintf(volume_text, sizeof(volume_text), "VOL %.1f / %.1f dB",
             s.volume, s.volume_max);
    copy_text(s.action_ack, sizeof(s.action_ack), volume_text);
    s.action_ack_volume = true;
    s.action_ack_until_us = esp_timer_get_time() + ACTION_ACK_DURATION_US;
    s.dirty = true;
}
extern "C" void touch_ui_update(const char *a, const char *b, const char *c, bool p, float v, float volume_min, float volume_max, float step, int pos, int length) {
    const bool changed = strcmp(s.track, a ? a : "") != 0 ||
        strcmp(s.artist, b ? b : "") != 0 || strcmp(s.album, c ? c : "") != 0 ||
        s.playing != p || s.volume != v || s.volume_min != volume_min ||
        s.volume_max != volume_max || s.volume_step != step ||
        s.seek_position != pos || s.seek_length != length;
    if (!changed) return;
    copy_text(s.track, sizeof(s.track), a); copy_text(s.artist, sizeof(s.artist), b);
    copy_text(s.album, sizeof(s.album), c); s.playing = p; s.volume = v;
    s.volume_min = volume_min; s.volume_max = volume_max;
    s.volume_step = step;
    s.seek_position = std::max(0, pos);
    s.seek_length = std::max(0, length);
    s.seek_updated_us = esp_timer_get_time();
    s.dirty = true;
}
extern "C" void touch_ui_show_zone_picker(const char **n, const char **i, int count, int selected) { s.zone_count = std::min(count, 24); s.zone_selected = selected; s.zone_current = selected; s.zone_offset = std::max(0, selected - 2); for (int x=0;x<s.zone_count;x++){copy_text(s.zone_names[x],MAX_TEXT,n[x]);copy_text(s.zone_ids[x],MAX_TEXT,i[x]);} s.picker=true;s.dirty=true; }
extern "C" void touch_ui_hide_zone_picker(void) { s.picker=false;s.dirty=true; }
extern "C" bool touch_ui_is_zone_picker_visible(void) { return s.picker; }
extern "C" void touch_ui_zone_picker_scroll(int d) { s.zone_offset=std::max(0,std::min(std::max(0,s.zone_count-4),s.zone_offset+d)); s.zone_selected=std::max(0,std::min(s.zone_count-1,s.zone_selected+d)); s.dirty=true; }
extern "C" void touch_ui_zone_picker_get_selected_id(char *o,size_t l){if(o&&l&&s.zone_selected>=0&&s.zone_selected<s.zone_count)copy_text(o,l,s.zone_ids[s.zone_selected]);}
extern "C" bool touch_ui_zone_picker_is_current_selection(void){return s.zone_selected==s.zone_current;}
extern "C" void touch_ui_update_battery(void) { }
extern "C" void touch_ui_apply_display_config(const rk_cfg_t *cfg,bool charging){if(!cfg)return;s.charging=charging;s.dim_timeout=rk_cfg_get_dim_timeout(cfg,charging);s.sleep_timeout=rk_cfg_get_sleep_timeout(cfg,charging);s.power_started_us=esp_timer_get_time();}
extern "C" bool touch_ui_is_display_sleeping(void){return s.sleeping;}
extern "C" void touch_ui_show_settings(void){
    s.settings = true;
    s.picker = false;
    leave_art_mode();
    wake();
}
