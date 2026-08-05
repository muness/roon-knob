#include "touch_ui.h"

#include "bridge_client.h"
#include "controller_action_router.h"
#include "controller_input.h"
#include "m5_platform.h"
#include "platform/platform_http.h"
#include "platform/platform_task.h"

#include <M5Unified.h>
#include <esp_timer.h>
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
constexpr int ART = 72;
constexpr int MAX_TEXT = 96;
constexpr int DEADZONE = 28;

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
    bool dirty = true;
    bool charging = false;
    uint8_t last_buttons = 0;
    int64_t last_input_us = 0;
    int64_t power_started_us = 0;
    uint32_t dim_timeout = 0;
    uint32_t sleep_timeout = 0;
    bool sleeping = false;
    uint16_t *artwork = nullptr;
    char artwork_result_key[MAX_TEXT] = {};
};

State s;
std::atomic_bool s_artwork_loading{false};

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
    char url[384] = {};
    char *raw = nullptr;
    size_t raw_len = 0;
    const size_t expected = ART * ART * sizeof(uint16_t);
    const char *art_url = bridge_client_get_artwork_url_for_format(
        url, sizeof(url), ART, ART, 0, "rgb565");
    if (art_url && platform_http_get_image(art_url, &raw, &raw_len) == 0 &&
        raw_len == expected) {
        ArtworkResult *r = static_cast<ArtworkResult *>(calloc(1, sizeof(*r)));
        if (r) {
            copy_text(r->key, sizeof(r->key), key);
            r->pixels = static_cast<uint16_t *>(malloc(expected));
            if (r->pixels) {
                memcpy(r->pixels, raw, expected);
                if (platform_task_post_to_ui(artwork_applied, r)) r = nullptr;
            }
            if (r) { free(r->pixels); free(r); }
        }
    }
    platform_http_free(raw);
    s_artwork_loading.store(false);
    vTaskDelete(nullptr);
}

void fetch_artwork(void) {
    if (!s.artwork_key[0] || s_artwork_loading.exchange(true)) return;
    char *key = strdup(s.artwork_key);
    if (!key || platform_task_start_internal_stack("atom_art", 12288,
                                                   artwork_task, key) != 0) {
        free(key);
        s_artwork_loading.store(false);
    }
}

void draw_text(const char *text, int x, int y, int size, uint32_t color) {
    M5.Display.setTextSize(size);
    M5.Display.setTextColor(color, 0x08111d);
    M5.Display.drawString(text ? text : "", x, y);
}

void draw_crop(void) {
    if (!s.artwork) return;
    M5.Display.pushImage(28, 18, ART, ART, s.artwork);
}

void redraw(void) {
    if (!s.dirty) return;
    M5.Display.startWrite();
    M5.Display.fillScreen(0x08111d);
    if (s.sleeping) {
        M5.Display.endWrite();
        s.dirty = false;
        return;
    }
    if (s.picker) {
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
        draw_text("Hold both buttons", 5, 35, 1, 0xe2e8f0);
        draw_text("to return", 5, 51, 1, 0xe2e8f0);
        draw_text(s.network, 5, 88, 1, 0x94a3b8);
    } else {
        draw_text(s.zone, 3, 3, 1, s.online ? 0x7dd3fc : 0xf87171);
        draw_crop();
        if (!s.artwork) draw_text(s.playing ? ">" : "||", 58, 45, 2, 0x38bdf8);
        draw_text(s.track, 3, 94, 1, 0xf8fafc);
        draw_text(s.artist, 3, 106, 1, 0x94a3b8);
        int bar = std::max(0, std::min(116, static_cast<int>(s.volume + 60)));
        M5.Display.fillRect(4, 122, 120, 3, 0x1e293b);
        M5.Display.fillRect(4, 122, bar, 3, 0x38bdf8);
    }
    M5.Display.endWrite();
    s.dirty = false;
}

void wake(void) {
    if (s.sleeping) {
        s.sleeping = false;
        m5_platform_display_wake();
    }
    s.power_started_us = esp_timer_get_time();
    s.dirty = true;
}

void process_input(void) {
    m5_platform_joystick_state_t js = {};
    if (!m5_platform_joystick_state(&js)) return;
    const int64_t now = esp_timer_get_time();
    if (js.buttons != s.last_buttons) {
        wake();
        const uint8_t pressed = js.buttons & static_cast<uint8_t>(~s.last_buttons);
        if (s.picker) {
            if (pressed & 4) button_action(CONTROLLER_ACTION_SELECT_ZONE_PICKER);
            if (pressed & 8) button_action(CONTROLLER_ACTION_CLOSE_ZONE_PICKER);
        } else if (s.settings) {
            if ((js.buttons & 3) == 3) { s.settings = false; s.dirty = true; }
        } else {
            if (pressed & 1) command(controller_command_make(CONTROLLER_COMMAND_PREVIOUS_TRACK));
            if (pressed & 2) command(controller_command_make(CONTROLLER_COMMAND_NEXT_TRACK));
            if (pressed & 4) command(controller_command_make(CONTROLLER_COMMAND_TOGGLE_PLAYBACK));
            if (pressed & 8) button_action(CONTROLLER_ACTION_OPEN_ZONE_PICKER);
        }
        s.last_buttons = js.buttons;
    }
    if (s.picker && now - s.last_input_us > 180000) {
        if (js.right_y < 96) { button_action(CONTROLLER_ACTION_SCROLL_ZONE_PICKER); touch_ui_zone_picker_scroll(-1); s.last_input_us = now; }
        if (js.right_y > 160) { touch_ui_zone_picker_scroll(1); s.last_input_us = now; }
    } else if (!s.picker && !s.settings && now - s.last_input_us > 180000) {
        if (js.left_x < 96) { command(controller_command_adjust_volume(-1)); s.last_input_us = now; }
        if (js.left_x > 160) { command(controller_command_adjust_volume(1)); s.last_input_us = now; }
        if (js.right_x < 96) { command(controller_command_make(CONTROLLER_COMMAND_PREVIOUS_TRACK)); s.last_input_us = now; }
        if (js.right_x > 160) { command(controller_command_make(CONTROLLER_COMMAND_NEXT_TRACK)); s.last_input_us = now; }
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
    M5.Display.setTextDatum(lgfx::middle_left);
    M5.Display.setTextFont(1);
    M5.Display.setTextWrap(false);
    s.dirty = true;
}
extern "C" void touch_ui_process(void) {
    m5_platform_update();
    platform_task_run_pending();
    process_input();
    update_power();
    redraw();
}
extern "C" void touch_ui_set_status(bool v) { s.online = v; s.dirty = true; }
extern "C" void touch_ui_set_message(const char *v) { copy_text(s.network, sizeof(s.network), v); s.dirty = true; }
extern "C" void touch_ui_set_zone_name(const char *v) { copy_text(s.zone, sizeof(s.zone), v); s.dirty = true; }
extern "C" void touch_ui_set_network_status(const char *v) { copy_text(s.network, sizeof(s.network), v); s.dirty = true; }
extern "C" void touch_ui_post_zone_name(const char *v) { char *c = strdup(v ? v : ""); platform_task_post_to_ui([](void *p){ touch_ui_set_zone_name(static_cast<char *>(p)); free(p); }, c); }
extern "C" void touch_ui_post_network_status(const char *v) { char *c = strdup(v ? v : ""); platform_task_post_to_ui([](void *p){ touch_ui_set_network_status(static_cast<char *>(p)); free(p); }, c); }
extern "C" void touch_ui_set_artwork(const char *v) {
    const char *key = v ? v : "";
    if (strcmp(s.artwork_key, key) == 0) return;
    copy_text(s.artwork_key, sizeof(s.artwork_key), key);
    if (!s.artwork_key[0]) {
        free(s.artwork);
        s.artwork = nullptr;
        s.dirty = true;
        return;
    }
    fetch_artwork();
}
extern "C" void touch_ui_post_artwork(const char *v) { char *c = strdup(v ? v : ""); platform_task_post_to_ui([](void *p){ touch_ui_set_artwork(static_cast<char *>(p)); free(p); }, c); }
extern "C" void touch_ui_show_volume_change(float v, float step) { s.volume = v; s.volume_step = step; s.dirty = true; }
extern "C" void touch_ui_update(const char *a, const char *b, const char *c, bool p, float v, float, float, float step, int, int) { copy_text(s.track, sizeof(s.track), a); copy_text(s.artist, sizeof(s.artist), b); copy_text(s.album, sizeof(s.album), c); s.playing = p; s.volume = v; s.volume_step = step; s.dirty = true; }
extern "C" void touch_ui_show_zone_picker(const char **n, const char **i, int count, int selected) { s.zone_count = std::min(count, 24); s.zone_selected = selected; s.zone_current = selected; s.zone_offset = std::max(0, selected - 2); for (int x=0;x<s.zone_count;x++){copy_text(s.zone_names[x],MAX_TEXT,n[x]);copy_text(s.zone_ids[x],MAX_TEXT,i[x]);} s.picker=true;s.dirty=true; }
extern "C" void touch_ui_hide_zone_picker(void) { s.picker=false;s.dirty=true; }
extern "C" bool touch_ui_is_zone_picker_visible(void) { return s.picker; }
extern "C" void touch_ui_zone_picker_scroll(int d) { s.zone_offset=std::max(0,std::min(std::max(0,s.zone_count-4),s.zone_offset+d)); s.zone_selected=std::max(0,std::min(s.zone_count-1,s.zone_selected+d)); s.dirty=true; }
extern "C" void touch_ui_zone_picker_get_selected_id(char *o,size_t l){if(o&&l&&s.zone_selected>=0&&s.zone_selected<s.zone_count)copy_text(o,l,s.zone_ids[s.zone_selected]);}
extern "C" bool touch_ui_zone_picker_is_current_selection(void){return s.zone_selected==s.zone_current;}
extern "C" void touch_ui_update_battery(void) { }
extern "C" void touch_ui_apply_display_config(const rk_cfg_t *cfg,bool charging){if(!cfg)return;s.charging=charging;s.dim_timeout=rk_cfg_get_dim_timeout(cfg,charging);s.sleep_timeout=rk_cfg_get_sleep_timeout(cfg,charging);s.power_started_us=esp_timer_get_time();}
extern "C" bool touch_ui_is_display_sleeping(void){return s.sleeping;}
extern "C" void touch_ui_show_settings(void){s.settings=true;s.picker=false;s.dirty=true;}
