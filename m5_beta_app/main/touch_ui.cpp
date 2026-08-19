#include "touch_ui.h"

#include "controller_action.h"
#include "controller_command.h"
#include "controller_input.h"
#include "m5_platform.h"
#include "m5_interaction_policy.h"
#include "platform/platform_task.h"

#include <M5Unified.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_timer.h>

namespace {
constexpr int TARGET_DIAL = 1;
constexpr int TARGET_STICKS3 = 2;
constexpr int TARGET_STOPWATCH = 3;
constexpr int TARGET_STACKCHAN = 4;
constexpr uint32_t BG = 0x080b12;
constexpr uint32_t INK = 0xf4f1e8;
constexpr uint32_t MUTED = 0x778397;
constexpr uint32_t ACCENT = 0x35e0a1;
constexpr uint32_t HOT = 0xff4f87;

struct State {
    char title[96] = "HiPhi";
    char artist[96] = "waiting for music";
    char album[96] = "";
    char zone[64] = "NO ZONE";
    char network[96] = "Starting...";
    bool playing = false;
    bool online = false;
    bool dirty = true;
    bool sleeping = false;
    bool picker = false;
    bool settings = false;
    bool twist_armed = false;
    bool action_flash = false;
    float volume = -40;
    float volume_min = -80;
    float volume_max = 0;
    int battery = -1;
    int zone_count = 0;
    int zone_selected = 0;
    int zone_current = 0;
    char zone_names[18][64] = {};
    char zone_ids[18][64] = {};
    int64_t action_until = 0;
    int64_t input_next = 0;
    int64_t haptic_off = 0;
    int64_t awake_until = 0;
    float last_accel_mag = 1.0f;
} s;

void copy_text(char *out, size_t len, const char *value) {
    if (out && len) std::snprintf(out, len, "%s", value ? value : "");
}

bool dispatch(controller_command_t command) {
    controller_action_t action = controller_action_command(command);
    return controller_input_dispatch_action(&action);
}

bool simple(controller_action_kind_t kind) {
    controller_action_t action = controller_action_simple(kind);
    return controller_input_dispatch_action(&action);
}

void flash_action() {
    s.action_flash = true;
    s.action_until = esp_timer_get_time() + 550000;
    s.dirty = true;
}

[[maybe_unused]] void volume_steps(int steps) {
    if (steps && dispatch(controller_command_adjust_volume(steps))) flash_action();
}

void toggle_playback() {
    if (dispatch(controller_command_make(CONTROLLER_COMMAND_TOGGLE_PLAYBACK))) {
        flash_action();
    }
}

[[maybe_unused]] void wake_display() {
    s.awake_until = esp_timer_get_time() + 8000000;
    if (s.sleeping) {
        m5_platform_display_wake();
        s.sleeping = false;
    }
    m5_platform_set_brightness(210);
    s.dirty = true;
}

void draw_centered(const char *text, int y, int size, uint32_t color) {
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(size);
    M5.Display.setTextColor(color, BG);
    M5.Display.drawString(text ? text : "", M5.Display.width() / 2, y);
}

void draw_battery() {
    if (s.battery < 0) return;
    char text[12];
    std::snprintf(text, sizeof(text), "%d%%", s.battery);
    M5.Display.setTextDatum(top_right);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(MUTED, BG);
    M5.Display.drawString(text, M5.Display.width() - 7, 7);
}

void render_picker() {
    const int w = M5.Display.width();
    const int h = M5.Display.height();
    M5.Display.fillScreen(BG);
    draw_battery();
    draw_centered("CHOOSE A ROOM", 22, 1, ACCENT);
    const int visible = std::min(5, s.zone_count);
    int first = std::max(0, std::min(s.zone_selected - visible / 2,
                                     s.zone_count - visible));
    for (int row = 0; row < visible; ++row) {
        const int index = first + row;
        const int y = 48 + row * std::max(28, (h - 58) / std::max(1, visible));
        if (index == s.zone_selected) {
            M5.Display.fillRoundRect(10, y - 13, w - 20, 27, 8, ACCENT);
        }
        M5.Display.setTextColor(index == s.zone_selected ? BG : INK,
                                index == s.zone_selected ? ACCENT : BG);
        M5.Display.setTextDatum(middle_center);
        M5.Display.setTextSize(1);
        M5.Display.drawString(s.zone_names[index], w / 2, y);
    }
}

[[maybe_unused]] void render_dial() {
    const int cx = M5.Display.width() / 2;
    const int cy = M5.Display.height() / 2;
    M5.Display.fillScreen(BG);
    draw_battery();
    const float range = std::max(1.0f, s.volume_max - s.volume_min);
    const float ratio = std::max(0.0f, std::min(1.0f, (s.volume - s.volume_min) / range));
    M5.Display.drawArc(cx, cy, 116, 106, 35, 325, 0x252b38);
    M5.Display.drawArc(cx, cy, 116, 106, 35, 35 + static_cast<int>(290 * ratio),
                       s.action_flash ? HOT : ACCENT);
    draw_centered(s.playing ? "PLAYING" : "PAUSED", 60, 1,
                  s.playing ? ACCENT : MUTED);
    draw_centered(s.title, 102, 2, INK);
    draw_centered(s.artist, 133, 1, MUTED);
    char volume[24];
    std::snprintf(volume, sizeof(volume), "%.1f dB", s.volume);
    draw_centered(volume, 172, 2, s.action_flash ? HOT : INK);
    draw_centered("TURN = VOLUME  PRESS = PLAY", 212, 1, MUTED);
}

[[maybe_unused]] void render_stick() {
    const int w = M5.Display.width();
    M5.Display.fillScreen(s.twist_armed ? 0x10251f : BG);
    M5.Display.fillRect(0, 0, w, 8, s.twist_armed ? HOT : ACCENT);
    draw_battery();
    draw_centered(s.twist_armed ? "TWIST LIVE" : "HOLD TO TWIST", 28, 1,
                  s.twist_armed ? HOT : MUTED);
    draw_centered(s.playing ? ">" : "||", 65, 3, s.playing ? ACCENT : MUTED);
    draw_centered(s.title, 107, 1, INK);
    draw_centered(s.artist, 128, 1, MUTED);
    char volume[24];
    std::snprintf(volume, sizeof(volume), "VOL %.1f", s.volume);
    draw_centered(volume, 169, 2, s.action_flash ? HOT : INK);
    draw_centered(s.twist_armed ? "ROTATE YOUR HAND" : "B = PLAY / PAUSE",
                  216, 1, s.twist_armed ? ACCENT : MUTED);
}

[[maybe_unused]] void render_stopwatch() {
    const int w = M5.Display.width();
    const int h = M5.Display.height();
    M5.Display.fillScreen(BG);
    draw_battery();
    M5.Display.fillCircle(w / 2, 90, 58, s.playing ? ACCENT : 0x252b38);
    draw_centered(s.playing ? ">" : "||", 90, 4, s.playing ? BG : MUTED);
    draw_centered(s.title, 178, 3, INK);
    draw_centered(s.artist, 220, 2, MUTED);
    char volume[32];
    std::snprintf(volume, sizeof(volume), "%.1f dB", s.volume);
    draw_centered(volume, 292, 4, s.action_flash ? HOT : ACCENT);
    M5.Display.fillRoundRect(28, h - 96, w - 56, 58, 20, 0x171c27);
    draw_centered("-   SIDE BUTTONS   +", h - 67, 2, INK);
    draw_centered("RAISE TO WAKE  TAP TO PLAY", h - 18, 1, MUTED);
}

[[maybe_unused]] void render_stackchan() {
    const int w = M5.Display.width();
    const int h = M5.Display.height();
    const int bob = s.action_flash ? -7 : 0;
    M5.Display.fillScreen(s.playing ? 0x10201b : BG);
    draw_battery();
    const uint32_t eye = s.online ? INK : MUTED;
    if (s.playing) {
        M5.Display.fillEllipse(w / 2 - 72, 93 + bob, 25, 40, eye);
        M5.Display.fillEllipse(w / 2 + 72, 93 + bob, 25, 40, eye);
        M5.Display.fillCircle(w / 2 - 65, 84 + bob, 8, ACCENT);
        M5.Display.fillCircle(w / 2 + 79, 84 + bob, 8, ACCENT);
        M5.Display.drawArc(w / 2, 147 + bob, 40, 34, 30, 150, HOT);
    } else {
        M5.Display.fillRoundRect(w / 2 - 104, 92 + bob, 64, 8, 4, eye);
        M5.Display.fillRoundRect(w / 2 + 40, 92 + bob, 64, 8, 4, eye);
        M5.Display.fillRoundRect(w / 2 - 25, 151 + bob, 50, 7, 3, MUTED);
    }
    draw_centered(s.playing ? "I'M INTO THIS" : "SERVOS PARKED - FACE LIVE", 18, 1,
                  s.playing ? ACCENT : MUTED);
    draw_centered(s.title, h - 55, 2, INK);
    draw_centered(s.artist, h - 34, 1, MUTED);
    draw_centered("TAP MY FACE", h - 13, 1, s.action_flash ? HOT : MUTED);
}

void render() {
    if (s.picker) return render_picker();
    if (s.settings) {
        M5.Display.fillScreen(BG);
        draw_centered("SETUP", M5.Display.height()/2 - 30, 2, ACCENT);
        draw_centered(s.network, M5.Display.height()/2 + 10, 1, INK);
        return;
    }
#if HIPHI_M5_TARGET_ID == 1
    render_dial();
#elif HIPHI_M5_TARGET_ID == 2
    render_stick();
#elif HIPHI_M5_TARGET_ID == 3
    render_stopwatch();
#else
    render_stackchan();
#endif
}

[[maybe_unused]] void picker_input(int delta, bool select) {
    if (delta) {
        controller_action_t a = controller_action_picker_scroll(delta);
        controller_input_dispatch_action(&a);
    }
    if (select) simple(CONTROLLER_ACTION_SELECT_ZONE_PICKER);
}

void process_input() {
    m5_platform_update();
    [[maybe_unused]] const int64_t now = esp_timer_get_time();
    m5_platform_surface_button_event_t buttons = {};
    m5_platform_surface_button_event(&buttons);
    m5_platform_touch_event_t touch = {};
    [[maybe_unused]] const bool touched = m5_platform_touch_event(&touch);

#if HIPHI_M5_TARGET_ID == 1
    int32_t delta = 0;
    m5_platform_encoder_delta(&delta);
    if (s.picker) {
        picker_input(delta, buttons.clicked);
    } else {
        if (delta) volume_steps(controller_input_accelerated_steps(delta));
        if (buttons.clicked) toggle_playback();
        if (touched && touch.state == M5_PLATFORM_TOUCH_CLICKED)
            simple(CONTROLLER_ACTION_OPEN_ZONE_PICKER);
    }
#elif HIPHI_M5_TARGET_ID == 2
    if (s.picker) {
        if (buttons.clicked) picker_input(1, false);
        if (buttons.secondary_clicked) picker_input(0, true);
        return;
    }
    if (buttons.secondary_held) {
        simple(CONTROLLER_ACTION_OPEN_ZONE_PICKER);
        return;
    }
    const bool armed = buttons.pressed || buttons.held;
    if (armed != s.twist_armed) { s.twist_armed = armed; s.dirty = true; }
    if (buttons.secondary_clicked) toggle_playback();
    if (armed && now >= s.input_next) {
        float gx, gy, gz;
        if (m5_platform_gyro(&gx, &gy, &gz)) {
            const int step = m5_interaction_twist_step(
                true, gz, now, &s.input_next);
            if (step) volume_steps(step);
        }
    }
#elif HIPHI_M5_TARGET_ID == 3
    float ax, ay, az;
    if (m5_platform_accel(&ax, &ay, &az)) {
        const float mag = std::sqrt(ax*ax + ay*ay + az*az);
        if (m5_interaction_raise_wake(s.last_accel_mag, mag)) wake_display();
        s.last_accel_mag = mag;
    }
    if (s.picker) {
        if (buttons.clicked) picker_input(-1, false);
        if (buttons.secondary_clicked) picker_input(1, false);
        if (touched && touch.state == M5_PLATFORM_TOUCH_CLICKED)
            picker_input(0, true);
    } else if (buttons.clicked || buttons.secondary_clicked) {
        wake_display();
        volume_steps(buttons.secondary_clicked ? 1 : -1);
        m5_platform_haptic(90);
        s.haptic_off = now + 70000;
    }
    if (!s.picker && touched && touch.state == M5_PLATFORM_TOUCH_HELD) {
        wake_display(); simple(CONTROLLER_ACTION_OPEN_ZONE_PICKER);
    } else if (!s.picker && touched && touch.state == M5_PLATFORM_TOUCH_CLICKED) {
        wake_display(); toggle_playback(); m5_platform_haptic(70); s.haptic_off = now + 50000;
    }
    if (s.haptic_off && now >= s.haptic_off) { m5_platform_haptic(0); s.haptic_off = 0; }
    if (s.awake_until && now >= s.awake_until && !s.sleeping) {
        m5_platform_set_brightness(20); s.sleeping = true;
    }
#else
    if (s.picker) {
        if (touched && touch.state == M5_PLATFORM_TOUCH_DRAGGING &&
            std::abs(touch.delta_y) > 8)
            picker_input(touch.delta_y > 0 ? -1 : 1, false);
        if (touched && touch.state == M5_PLATFORM_TOUCH_CLICKED)
            picker_input(0, true);
    } else if (touched && touch.state == M5_PLATFORM_TOUCH_HELD) {
        simple(CONTROLLER_ACTION_OPEN_ZONE_PICKER);
    } else if (touched && touch.state == M5_PLATFORM_TOUCH_CLICKED) {
        toggle_playback();
    }
#endif
}
}

extern "C" void touch_ui_init(void) { s.awake_until = esp_timer_get_time() + 8000000; render(); }
extern "C" void touch_ui_process(void) {
    process_input();
    if (s.action_flash && esp_timer_get_time() >= s.action_until) { s.action_flash=false; s.dirty=true; }
    if (s.dirty && !s.sleeping) { s.dirty=false; render(); }
}
extern "C" void touch_ui_set_status(bool v){if(s.online!=v){s.online=v;s.dirty=true;}}
extern "C" void touch_ui_set_message(const char *v){touch_ui_set_network_status(v);}
extern "C" void touch_ui_set_zone_name(const char *v){if(std::strcmp(s.zone,v?v:"")!=0){copy_text(s.zone,sizeof(s.zone),v);s.dirty=true;}}
extern "C" void touch_ui_set_network_status(const char *v){if(std::strcmp(s.network,v?v:"")!=0){copy_text(s.network,sizeof(s.network),v);s.dirty=true;}}
extern "C" void touch_ui_post_zone_name(const char *v){char *c=strdup(v?v:"");platform_task_post_to_ui([](void*p){touch_ui_set_zone_name(static_cast<char*>(p));free(p);},c);}
extern "C" void touch_ui_post_network_status(const char *v){char *c=strdup(v?v:"");platform_task_post_to_ui([](void*p){touch_ui_set_network_status(static_cast<char*>(p));free(p);},c);}
extern "C" void touch_ui_set_artwork(const char *v){(void)v;}
extern "C" void touch_ui_post_artwork(const char *v){(void)v;}
extern "C" void touch_ui_show_volume_change(float v,float step){(void)step;s.volume=v;s.dirty=true;flash_action();}
extern "C" void touch_ui_update(const char *a,const char *b,const char *c,bool p,float v,float min,float max,float step,int pos,int length){(void)step;(void)pos;(void)length;copy_text(s.title,sizeof(s.title),a);copy_text(s.artist,sizeof(s.artist),b);copy_text(s.album,sizeof(s.album),c);s.playing=p;s.volume=v;s.volume_min=min;s.volume_max=max;s.dirty=true;}
extern "C" void touch_ui_show_zone_picker(const char **n,const char **i,int count,int selected){s.zone_count=std::min(count,18);s.zone_selected=std::max(0,std::min(selected,s.zone_count-1));s.zone_current=s.zone_selected;for(int x=0;x<s.zone_count;x++){copy_text(s.zone_names[x],64,n[x]);copy_text(s.zone_ids[x],64,i[x]);}s.picker=true;s.settings=false;s.dirty=true;}
extern "C" void touch_ui_hide_zone_picker(void){s.picker=false;s.dirty=true;}
extern "C" bool touch_ui_is_zone_picker_visible(void){return s.picker;}
extern "C" void touch_ui_zone_picker_scroll(int d){s.zone_selected=std::max(0,std::min(s.zone_count-1,s.zone_selected+(d>0?1:-1)));s.dirty=true;}
extern "C" void touch_ui_zone_picker_get_selected_id(char *o,size_t l){if(o&&l&&s.zone_selected>=0&&s.zone_selected<s.zone_count)copy_text(o,l,s.zone_ids[s.zone_selected]);}
extern "C" bool touch_ui_zone_picker_is_current_selection(void){return s.zone_selected==s.zone_current;}
extern "C" void touch_ui_update_battery(void){int level=m5_platform_battery_level();if(level!=s.battery){s.battery=level;s.dirty=true;}}
extern "C" void touch_ui_apply_display_config(const rk_cfg_t *cfg,bool charging){(void)cfg;(void)charging;}
extern "C" bool touch_ui_is_display_sleeping(void){return s.sleeping;}
extern "C" void touch_ui_show_settings(void){s.settings=true;s.picker=false;s.dirty=true;}
