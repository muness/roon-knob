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
constexpr int ART = 128;
constexpr int MAX_TEXT = 96;
constexpr int DEADZONE = 28;

int axis_direction(uint8_t value) {
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
    float volume_step = 1;
    bool playing = false;
    bool online = false;
    bool picker = false;
    bool settings = false;
    bool art_mode = false;
    bool dirty = true;
    bool charging = false;
    uint8_t last_buttons = 0;
    int64_t right_press_started_us = 0;
    bool right_hold_fired = false;
    int64_t top_right_started_us = 0;
    bool top_right_hold_fired = false;
    int64_t last_input_us = 0;
    int64_t power_started_us = 0;
    uint32_t dim_timeout = 0;
    uint32_t sleep_timeout = 0;
    bool sleeping = false;
    bool wifi_scan_started = false;
    bool wifi_scan_frame_drawn = false;
    rk_wifi_scan_state_t wifi_scan_state = RK_WIFI_SCAN_IDLE;
    int64_t last_draw_us = 0;
    int64_t last_artwork_retry_us = 0;
    uint16_t *artwork = nullptr;
    char artwork_result_key[MAX_TEXT] = {};
};

State s;
std::atomic_bool s_artwork_loading{false};
M5Canvas s_canvas(&M5.Display);
lgfx::LovyanGFX *s_draw_target = &M5.Display;
bool s_canvas_ready = false;

void copy_text(char *out, size_t len, const char *in) {
    if (out && len) snprintf(out, len, "%s", in ? in : "");
}

void command(controller_command_t command) {
    controller_action_t action = controller_action_command(command);
    controller_input_dispatch_action(&action);
}

void button_action(controller_action_kind_t kind) {
    controller_action_t action = controller_action_simple(kind);
    controller_input_dispatch_action(&action);
}

struct ArtworkResult { char key[MAX_TEXT]; uint16_t *pixels; };

void artwork_applied(void *arg) {
    ArtworkResult *r = static_cast<ArtworkResult *>(arg);
    if (!r) return;
    if (strcmp(r->key, s.artwork_key) == 0 && r->pixels) {
        free(s.artwork);
        s.artwork = r->pixels;
        r->pixels = nullptr;
        s.dirty = true;
    }
    free(r->pixels);
    free(r);
    s_artwork_loading.store(false);
}

void artwork_task(void *arg) {
    char key[MAX_TEXT] = {};
    copy_text(key, sizeof(key), static_cast<const char *>(arg));
    free(arg);
    const size_t expected = ART * ART * sizeof(uint16_t);
    for (int attempt = 0; attempt < 3; ++attempt) {
        char *raw = nullptr;
        size_t raw_len = 0;
        const int http_result = bridge_client_fetch_artwork(
            key, ART, ART, "rgb565", &raw, &raw_len);
        ESP_LOGI(TAG, "artwork fetch result=%d bytes=%u", http_result,
                 static_cast<unsigned>(raw_len));
        if (http_result == 0 && raw_len == expected) {
            ArtworkResult *r = static_cast<ArtworkResult *>(calloc(1, sizeof(*r)));
            if (r) {
                copy_text(r->key, sizeof(r->key), key);
                /* Transfer the HTTP buffer directly. */
                r->pixels = reinterpret_cast<uint16_t *>(raw);
                raw = nullptr;
                if (r->pixels) {
                    const bool posted = platform_task_post_to_ui(artwork_applied, r);
                    if (posted) r = nullptr;
                }
                if (r) { free(r->pixels); free(r); }
            }
            platform_http_free(raw);
            break;
        }
        platform_http_free(raw);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    s_artwork_loading.store(false);
    vTaskDelete(nullptr);
}

void fetch_artwork(void) {
    if (!s.artwork_key[0] || s_artwork_loading.exchange(true)) return;
    char *key = strdup(s.artwork_key);
    if (!key || platform_task_start_internal_stack("atom_art", 4096,
                                                   artwork_task, key) != 0) {
        ESP_LOGW(TAG, "artwork worker start failed");
        free(key);
        s_artwork_loading.store(false);
    }
}

void draw_text(const char *text, int x, int y, int size, uint32_t color) {
    s_draw_target->setTextSize(size);
    // Use M5GFX's transparent text overload. Supplying a background color
    // paints opaque glyph cells and makes changing labels look like flicker.
    s_draw_target->setTextColor(color);
    s_draw_target->drawString(text ? text : "", x, y);
}

void draw_crop(void) {
    if (!s.artwork) return;
    uint16_t row[72];
    for (int y = 0; y < 72; ++y) {
        for (int x = 0; x < 72; ++x) {
            row[x] = s.artwork[(y * ART / 72) * ART + (x * ART / 72)];
        }
        s_draw_target->pushImage(4, 18 + y, 72, 1, row);
    }
}

void draw_art_mode(void) {
    s_draw_target->fillScreen(0x05070b);
    if (s.artwork) {
        s_draw_target->pushImage(0, 0, ART, ART, s.artwork);
    } else draw_text("NO ART", 42, 58, 1, 0x94a3b8);
    s_draw_target->fillRectAlpha(0, 100, 128, 28, 190, 0x08111d);
    draw_text(s.track, 4, 105, 1, 0xf8fafc);
    draw_text(s.artist, 4, 118, 1, 0x94a3b8);
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
    // A canvas is an off-screen framebuffer.  Drawing it inside a display
    // transaction is unnecessary and can make the panel visibly flicker on
    // the Atom's small SPI display.  Keep the transaction only for the
    // direct-display fallback.
    const int64_t now_us = esp_timer_get_time();
    if (s.last_draw_us != 0 && now_us - s.last_draw_us < 100000) return;
    const bool direct_display = !s_canvas_ready;
    if (direct_display) s_draw_target->startWrite();
    s_draw_target->fillScreen(0x08111d);
    if (s.sleeping) {
        if (direct_display) s_draw_target->endWrite();
        if (s_canvas_ready) s_canvas.pushSprite(&M5.Display, 0, 0);
        s.dirty = false;
        s.last_draw_us = now_us;
        return;
    }
    if (wifi_mgr_is_ap_mode()) {
        draw_wifi_setup();
    } else if (s.art_mode) {
        draw_art_mode();
    } else if (s.picker) {
        draw_text("SELECT ZONE", 4, 3, 1, 0x7dd3fc);
        for (int row = 0; row < 4; ++row) {
            int i = s.zone_offset + row;
            if (i >= s.zone_count) break;
            uint32_t c = i == s.zone_selected ? 0x38bdf8 : 0x9ca3af;
            draw_text(s.zone_names[i], 6, 23 + row * 24, 1, c);
        }
        draw_text("A SELECT  B BACK", 3, 113, 1, 0x64748b);
    } else if (s.settings) {
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
        draw_text(s.zone, 3, 3, 1, s.online ? 0x7dd3fc : 0xf87171);
        draw_crop();
        /* A playing zone offers pause, while a stopped zone offers play. */
        if (!s.artwork) draw_text(s.playing ? "||" : ">", 58, 45, 2, 0x38bdf8);
        draw_text(s.track, 3, 94, 1, 0xf8fafc);
        draw_text(s.artist, 3, 106, 1, 0x94a3b8);
        int bar = std::max(0, std::min(116, static_cast<int>(s.volume + 60)));
        s_draw_target->fillRect(4, 122, 120, 3, 0x1e293b);
        s_draw_target->fillRect(4, 122, bar, 3, 0x38bdf8);
    }
    if (direct_display) s_draw_target->endWrite();
    if (s_canvas_ready) s_canvas.pushSprite(&M5.Display, 0, 0);
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

void toggle_art_mode(void) {
    s.art_mode = !s.art_mode;
    /* Do not wait for a bridge poll to repaint the other paradigm. The
     * current zone, metadata, and artwork are already cached locally. */
    s.last_draw_us = 0;
    s.dirty = true;
}

void process_input(void) {
    m5_platform_surface_button_event_t surface = {};
    if (m5_platform_surface_button_event(&surface)) {
        if (surface.clicked || surface.held) wake();
        if (surface.held) {
            s.settings = !s.settings;
            s.picker = false;
            s.art_mode = false;
            s.dirty = true;
        } else if (surface.clicked) {
            /* Artwork mode is a distinct UI paradigm. The dedicated
             * top-left button is its only mode toggle; a surface tap must
             * never bounce back to control mode. */
        }
    }
    m5_platform_joystick_state_t js = {};
    if (!m5_platform_joystick_state(&js)) return;
    const int64_t now = esp_timer_get_time();
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
        if (s.picker) {
            if (pressed & 4) button_action(CONTROLLER_ACTION_SELECT_ZONE_PICKER);
            if (released & 8 && s.right_press_started_us && !s.right_hold_fired) button_action(CONTROLLER_ACTION_CLOSE_ZONE_PICKER);
        } else if (s.settings) {
            // Settings is toggled by holding the physical top-right button.
            // The other top button is intentionally reserved for a future
            // affordance and must not become an accidental transport action.
        } else {
            if (pressed & 1) {
                toggle_art_mode();
            }
            if (pressed & 4) command(controller_command_make(CONTROLLER_COMMAND_TOGGLE_PLAYBACK));
        }
        s.last_buttons = buttons;
    }
    if (js.top_right_pressed && s.top_right_started_us &&
        !s.top_right_hold_fired && now - s.top_right_started_us >= 1200000) {
        s.top_right_hold_fired = true;
        wake();
        s.settings = !s.settings;
        s.picker = false;
        s.art_mode = false;
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
        s.art_mode = false;
        s.dirty = true;
    }
    if (s.picker && now - s.last_input_us > 180000) {
        /* Prefer the right stick for zone navigation; accept the left stick
         * as a hardware-tolerant fallback so a marginal coprocessor axis
         * cannot make the selector unreachable. */
        const int picker_axis = right_y != 0 ? right_y : left_y;
        if (picker_axis != 0) {
            controller_action_t action = controller_action_picker_scroll(picker_axis);
            controller_input_dispatch_action(&action);
            s.last_input_us = now;
        }
    } else if (!s.picker && !s.settings && now - s.last_input_us > 180000) {
        /* The JoyStick Y axis increases when pushed physically down.  Map
         * the user-facing direction (up = more, down = less), not the raw
         * ADC sign. */
        if (left_y < 0) { command(controller_command_adjust_volume(1)); s.last_input_us = now; }
        if (left_y > 0) { command(controller_command_adjust_volume(-1)); s.last_input_us = now; }
        if (right_x < 0) { command(controller_command_make(CONTROLLER_COMMAND_PREVIOUS_TRACK)); s.last_input_us = now; }
        if (right_x > 0) { command(controller_command_make(CONTROLLER_COMMAND_NEXT_TRACK)); s.last_input_us = now; }
    }
}

void update_power(void) {
    if (s.sleeping || !s.dim_timeout) return;
    const int64_t elapsed = esp_timer_get_time() - s.power_started_us;
    if (elapsed >= static_cast<int64_t>(s.dim_timeout) * 1000000 && !s.artwork) {
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
    s.power_started_us = esp_timer_get_time();
    s_canvas.setColorDepth(16);
    // M5Canvas(parent) defaults to PSRAM, but the original AtomS3 has no
    // PSRAM. Use internal DMA-capable RAM so the full-frame sprite succeeds.
    s_canvas.setPsram(false);
    s_canvas.setSwapBytes(true);
    s_canvas_ready = s_canvas.createSprite(W, H) != nullptr;
    if (s_canvas_ready) s_draw_target = &s_canvas;
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
    if (s.artwork_key[0] && !s.artwork && !s_artwork_loading.load() &&
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
        if (key[0] && !s.artwork) fetch_artwork();
        return;
    }
    copy_text(s.artwork_key, sizeof(s.artwork_key), key);
    /* Never leave the previous track's pixels presented while the new
     * request is in flight. A failed fetch should produce an honest NO ART
     * state, not stale artwork that looks like a playback update failure. */
    free(s.artwork);
    s.artwork = nullptr;
    s.dirty = true;
    if (!s.artwork_key[0]) {
        return;
    }
    fetch_artwork();
}
extern "C" void touch_ui_post_artwork(const char *v) { char *c = strdup(v ? v : ""); platform_task_post_to_ui([](void *p){ touch_ui_set_artwork(static_cast<char *>(p)); free(p); }, c); }
extern "C" void touch_ui_show_volume_change(float v, float step) { s.volume = v; s.volume_step = step; s.dirty = true; }
extern "C" void touch_ui_update(const char *a, const char *b, const char *c, bool p, float v, float, float, float step, int, int) {
    const bool changed = strcmp(s.track, a ? a : "") != 0 ||
        strcmp(s.artist, b ? b : "") != 0 || strcmp(s.album, c ? c : "") != 0 ||
        s.playing != p || s.volume != v || s.volume_step != step;
    if (!changed) return;
    copy_text(s.track, sizeof(s.track), a); copy_text(s.artist, sizeof(s.artist), b);
    copy_text(s.album, sizeof(s.album), c); s.playing = p; s.volume = v;
    s.volume_step = step; s.dirty = true;
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
    s.art_mode = false;
    wake();
}
