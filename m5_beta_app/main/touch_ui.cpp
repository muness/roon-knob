#include "touch_ui.h"

#include "controller_action.h"
#include "controller_command.h"
#include "controller_input.h"
#include "bridge_client.h"
#include "m5_platform.h"
#include "m5_terminal_power.h"
#include "m5_interaction_policy.h"
#include "m5_stackchan_faces.h"
#include "platform/platform_http.h"
#include "platform/platform_identity.h"
#include "platform/platform_task.h"
#include "wifi_manager.h"

#include <M5Unified.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_timer.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <nvs.h>

namespace {
constexpr char TAG[] = "touch_ui";
constexpr int TARGET_DIAL = 1;
constexpr int TARGET_STICKS3 = 2;
constexpr int TARGET_STOPWATCH = 3;
constexpr int TARGET_STACKCHAN = 4;
constexpr uint32_t BG = 0x080b12;
constexpr uint32_t INK = 0xf4f1e8;
constexpr uint32_t MUTED = 0x778397;
constexpr uint32_t ACCENT = 0x35e0a1;
constexpr uint32_t HOT = 0xff4f87;
constexpr uint32_t STACK_BG = 0x07090d;
constexpr uint32_t STACK_INK = 0xfff8e7;
constexpr uint32_t STACK_SECONDARY = 0xd4dbe5;
constexpr uint32_t STACK_TERTIARY = 0xaeb9c8;
constexpr uint32_t STACK_ACCENT = 0x7aa2ff;
constexpr uint32_t STACK_HOT = 0xff6b8f;
constexpr uint32_t STACK_CONTROL = 0x252c38;
#if HIPHI_M5_TARGET_ID == 1 || HIPHI_M5_TARGET_ID == 2
constexpr int STACKCHAN_ARTWORK_SIZE = 120;
#elif HIPHI_M5_TARGET_ID == 3
constexpr int STACKCHAN_ARTWORK_SIZE = 360;
#else
/* Kizz's Art mode is full-bleed on a 320x240 panel. Fetch at panel
 * width so the hero image is never enlarged from a postage-stamp source. */
constexpr int STACKCHAN_ARTWORK_SIZE = 320;
#endif
constexpr int STACKCHAN_FACE_BOTTOM = 108;
constexpr int STACKCHAN_VOLUME_TOP = 168;
constexpr int STACKCHAN_TRANSPORT_TOP = 199;

struct State {
    char title[96] = "HiPhi";
    char artist[96] = "waiting for music";
    char album[96] = "";
    char track_identity_title[96] = {};
    char track_identity_artist[96] = {};
    char zone[64] = "NO ZONE";
    char network[96] = "Starting...";
    bool playing = false;
    bool online = false;
    bool ever_online = false;
    bool dirty = true;
    bool dimmed = false;
    bool voice_listening = false;
    bool voice_diagnostics = true;
    char voice_state[16] = "STARTING";
    uint8_t voice_score_percent = 0;
    uint8_t voice_cutoff_percent = 0;
    bool sleeping = false;
    bool picker = false;
    bool settings = false;
    bool twist_armed = false;
    bool action_flash = false;
    bool body_enabled = false;
    bool sound_enabled = true;
    m5_platform_stackchan_volume_t voice_volume =
        M5_PLATFORM_STACKCHAN_VOLUME_LOW;
    bool body_hold_consumed = false;
    bool setup_mode = false;
    bool track_seen = false;
    bool playback_seen = false;
    bool zone_seen = false;
    bool controls_mode = false;
    bool art_mode = false;
    bool gesture_consumed = false;
    float volume = -40;
    float volume_min = -80;
    float volume_max = 0;
    int battery = -1;
    int zone_count = 0;
    int zone_selected = 0;
    int zone_current = 0;
    int seek_position = 0;
    int seek_length = 0;
    char zone_names[18][64] = {};
    char zone_ids[18][64] = {};
    int64_t action_until = 0;
    int64_t controls_until = 0;
    int64_t track_reveal_started = 0;
    int64_t track_reveal_until = 0;
    int64_t last_activity_us = 0;
    int64_t sleep_started_us = 0;
    int64_t input_next = 0;
    int64_t voice_diagnostics_next = 0;
    int64_t touch_quarantine_until = 0;
    int64_t haptic_off = 0;
    float last_accel_mag = 1.0f;
    char body_notice[32] = {};
    int64_t body_notice_until = 0;
    char action_notice[32] = {};
    char artwork_key[128] = {};
    uint16_t *artwork_pixels = nullptr;
    int artwork_width = 0;
    int artwork_height = 0;
    uint16_t art_timeout_sec = 0;
    uint16_t dim_timeout_sec = 0;
    uint16_t sleep_timeout_sec = 0;
    uint16_t power_off_timeout_sec = 0;
    uint16_t configured_art_timeout_sec = 0;
    uint16_t configured_dim_timeout_sec = 0;
    uint16_t configured_sleep_timeout_sec = 0;
    uint16_t configured_power_off_timeout_sec = 0;
    uint8_t action_face_variant = 0;
    uint8_t ambient_face_variant = 0;
} s;

std::atomic_bool s_artwork_loading{false};
M5Canvas s_stackchan_canvas(&M5.Display);
bool s_stackchan_canvas_ready = false;

struct MarqueeState {
    int x = 0;
    int y = 0;
    int width = 0;
    int size = 0;
    char value[128] = {};
    int64_t started_us = 0;
};

MarqueeState s_stackchan_marquees[3];

constexpr char kStackChanNvsNamespace[] = "stackchan";
constexpr char kStackChanBodyKey[] = "body_on";
/* v3 deliberately retires the debug-era off preference. Those values were
 * created while the servo driver could not arm and should not suppress the
 * first real body-language build. Choices made from this build persist. */
constexpr char kStackChanBodyPreferenceKey[] = "body_pref_v3";
constexpr char kStackChanSoundKey[] = "sound_on";
constexpr char kStackChanSoundPreferenceKey[] = "sound_pref_v1";
constexpr char kStackChanVoiceVolumeKey[] = "voice_vol";
constexpr char kStackChanVoiceVolumePreferenceKey[] = "voice_vol_v1";

void copy_text(char *out, size_t len, const char *value) {
    if (out && len) std::snprintf(out, len, "%s", value ? value : "");
}

void stackchan_apply_font(lgfx::LovyanGFX *target, int size) {
#if HIPHI_M5_TARGET_ID == 4
    target->setFont(size >= 2 ? &fonts::Font4 : &fonts::Font2);
    target->setTextSize(1);
#else
    target->setFont(&fonts::Font0);
    target->setTextSize(size);
#endif
}

void stackchan_draw_text(lgfx::LovyanGFX *target, const char *text, int x, int y,
                         int size, uint32_t color) {
    target->setTextDatum(lgfx::top_left);
    stackchan_apply_font(target, size);
    target->setTextColor(color);
    target->drawString(text ? text : "", x, y);
}

void stackchan_draw_center(lgfx::LovyanGFX *target, const char *text, int x, int y,
                           int size, uint32_t color) {
    target->setTextDatum(lgfx::middle_center);
    stackchan_apply_font(target, size);
    target->setTextColor(color);
    target->drawString(text ? text : "", x, y);
}

void stackchan_draw_voice_diagnostics(lgfx::LovyanGFX *target, int width,
                                      int height) {
#if HIPHI_M5_TARGET_ID == 4
    if (!s.voice_diagnostics) return;
    char label[40];
    std::snprintf(label, sizeof(label), "%s %u/%u", s.voice_state,
                  static_cast<unsigned>(s.voice_score_percent),
                  static_cast<unsigned>(s.voice_cutoff_percent));
    const bool fault = std::strcmp(s.voice_state, "FAULT") == 0;
    const bool active = std::strcmp(s.voice_state, "ARMED") != 0;
    target->fillRoundRect(72, height - 23, width - 144, 21, 8, STACK_BG);
    target->drawRoundRect(72, height - 23, width - 144, 21, 8,
                          fault ? STACK_HOT :
                          (active ? STACK_ACCENT : STACK_CONTROL));
    stackchan_draw_center(target, label, width / 2, height - 13, 1,
                          fault ? STACK_HOT :
                          (active ? STACK_INK : STACK_SECONDARY));
#else
    (void)target;
    (void)width;
    (void)height;
#endif
}

void stackchan_draw_thick_line(lgfx::LovyanGFX *target, int x0, int y0, int x1,
                               int y1, uint32_t color, int thickness = 3) {
    const int half = thickness / 2;
    for (int offset = -half; offset <= half; ++offset) {
        target->drawLine(x0, y0 + offset, x1, y1 + offset, color);
    }
}

/* Face and body are one performance. These are intentionally bold graphic
 * cues rather than tiny emoji changes: Kizz is normally read from across
 * a room, and the face must remain legible while the head is moving. */
void stackchan_draw_performance_face(
    lgfx::LovyanGFX *target, m5_platform_stackchan_face_cue_t cue, int w,
    uint8_t variant = 0) {
    const int cx = w / 2;
    const int left = cx - 64;
    const int right = cx + 64;
    const int eye_y = 78;

    const auto open_eye = [&](int x, int pupil_dx, int pupil_dy,
                              int radius_x = 28, int radius_y = 40) {
        target->fillEllipse(x, eye_y, radius_x, radius_y, STACK_INK);
        target->fillCircle(x + pupil_dx, eye_y - 8 + pupil_dy, 8,
                           STACK_ACCENT);
        target->fillCircle(x + pupil_dx - 2, eye_y - 10 + pupil_dy, 2,
                           STACK_INK);
    };
    const auto happy_eye = [&](int x, int gaze) {
        stackchan_draw_thick_line(target, x - 23, eye_y + 7, x, eye_y - 7,
                                  STACK_INK, 5);
        stackchan_draw_thick_line(target, x, eye_y - 7, x + 23, eye_y + 7,
                                  STACK_INK, 5);
        target->fillCircle(x + gaze, eye_y + 1, 3, STACK_ACCENT);
    };
    const auto broad_smile = [&](int offset) {
        target->drawArc(cx + offset, 122, 43, 30, 32, 148, STACK_HOT);
        target->drawArc(cx + offset, 123, 43, 30, 32, 148, STACK_HOT);
    };

    switch (cue) {
        case M5_PLATFORM_STACKCHAN_FACE_ANTICIPATE:
            open_eye(left, 4, 4, 25, 37);
            open_eye(right, -4, 4, 25, 37);
            target->fillCircle(cx, 126, 12, STACK_HOT);
            target->fillCircle(cx, 126, 6, STACK_BG);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_BEAM_LEFT:
            happy_eye(left, -5);
            happy_eye(right, -5);
            broad_smile(-5);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_BEAM_RIGHT:
            happy_eye(left, 5);
            happy_eye(right, 5);
            broad_smile(5);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_POP:
            open_eye(left, 0, 3, 30, 43);
            open_eye(right, 0, 3, 30, 43);
            target->fillEllipse(cx, 128, 17, 21, STACK_HOT);
            target->fillEllipse(cx, 128, 9, 12, STACK_BG);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_WINK:
            stackchan_draw_thick_line(target, left - 23, eye_y + 4, left,
                                      eye_y - 5, STACK_INK, 5);
            stackchan_draw_thick_line(target, left, eye_y - 5, left + 23,
                                      eye_y + 4, STACK_INK, 5);
            open_eye(right, 6, 1, 27, 39);
            broad_smile(7);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_SAD:
            open_eye(left, -5, 8, 27, 38);
            open_eye(right, 5, 8, 27, 38);
            stackchan_draw_thick_line(target, left - 22, eye_y - 30,
                                      left + 18, eye_y - 23, STACK_HOT, 3);
            stackchan_draw_thick_line(target, right - 18, eye_y - 23,
                                      right + 22, eye_y - 30, STACK_HOT, 3);
            target->drawArc(cx, 143, 35, 25, 210, 330, STACK_HOT);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_SETTLE:
            happy_eye(left, 0);
            happy_eye(right, 0);
            target->drawArc(cx, 121, 35, 25, 35, 145, STACK_HOT);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_ATTENTIVE:
            open_eye(left, variant == 1 ? -4 : 4, -2, 28, 41);
            open_eye(right, variant == 2 ? 4 : -4, -2, 28, 41);
            target->drawArc(cx, 122, 36, 26, 35, 145, STACK_HOT);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_RESTING:
        case M5_PLATFORM_STACKCHAN_FACE_BORED:
            target->drawArc(left, eye_y + 3 + (variant == 2 ? 4 : 0),
                            29, 16, 20, 160, STACK_INK);
            target->drawArc(right, eye_y + 3 + (variant == 1 ? 4 : 0),
                            29, 16, 20, 160, STACK_INK);
            target->drawArc(cx, 121, 25, 17,
                            cue == M5_PLATFORM_STACKCHAN_FACE_BORED ? 210 : 40,
                            cue == M5_PLATFORM_STACKCHAN_FACE_BORED ? 330 : 140,
                            STACK_SECONDARY);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_CURIOUS:
            open_eye(left, 7, variant == 2 ? -4 : 0, 31, 43);
            open_eye(right, -4, 3, 23, 35);
            stackchan_draw_thick_line(target, left - 22, eye_y - 34,
                                      left + 18, eye_y - 39, STACK_ACCENT, 3);
            target->fillCircle(cx + 8, 127, variant == 1 ? 12 : 9, STACK_HOT);
            target->fillCircle(cx + 8, 127, variant == 1 ? 6 : 4, STACK_BG);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_RELIEVED:
        case M5_PLATFORM_STACKCHAN_FACE_ACCEPTING:
            happy_eye(left, 0);
            happy_eye(right, 0);
            broad_smile(0);
            if (variant) {
                target->fillCircle(cx - 50, 121, 4, STACK_HOT);
                target->fillCircle(cx + 50, 121, 4, STACK_HOT);
            }
            break;
        case M5_PLATFORM_STACKCHAN_FACE_GLANCE_LEFT:
            open_eye(left, -9 - static_cast<int>(variant), variant & 1, 27, 39);
            open_eye(right, -9 - static_cast<int>(variant), variant & 1, 27, 39);
            target->drawArc(cx - 7, 122, 31, 22, 35, 145, STACK_HOT);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_GLANCE_RIGHT:
            open_eye(left, 9 + static_cast<int>(variant), variant & 1, 27, 39);
            open_eye(right, 9 + static_cast<int>(variant), variant & 1, 27, 39);
            target->drawArc(cx + 7, 122, 31, 22, 35, 145, STACK_HOT);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_LOUD:
            if (variant == 2) {
                open_eye(left, -2, -3, 31, 44);
                open_eye(right, 2, -3, 31, 44);
            } else {
                happy_eye(left, variant == 1 ? -5 : -2);
                happy_eye(right, variant == 1 ? 5 : 2);
            }
            target->fillEllipse(cx, 127, variant == 3 ? 22 : 18,
                                variant == 3 ? 16 : 23, STACK_HOT);
            target->fillEllipse(cx, 127, variant == 3 ? 13 : 10,
                                variant == 3 ? 8 : 14, STACK_BG);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_HUSH:
            if (variant == 2) {
                open_eye(left, 8, 8, 25, 36);
                open_eye(right, -8, 8, 25, 36);
            } else {
                target->fillRoundRect(left - 25, eye_y - 2, 50,
                                      variant == 1 ? 5 : 7, 4, STACK_INK);
                target->fillRoundRect(right - 25, eye_y - 2, 50,
                                      variant == 1 ? 5 : 7, 4, STACK_INK);
            }
            target->fillCircle(cx, 127, variant == 3 ? 6 : 9, STACK_HOT);
            target->fillCircle(cx, 127, variant == 3 ? 2 : 4, STACK_BG);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_PROUD:
        case M5_PLATFORM_STACKCHAN_FACE_CONTENT:
            happy_eye(left, -2);
            happy_eye(right, 2);
            broad_smile(0);
            if (cue == M5_PLATFORM_STACKCHAN_FACE_PROUD) {
                stackchan_draw_thick_line(target, left - 20, eye_y - 34,
                                          left + 18, eye_y - 39,
                                          STACK_ACCENT, 3);
                stackchan_draw_thick_line(target, right - 18, eye_y - 39,
                                          right + 20, eye_y - 34,
                                          STACK_ACCENT, 3);
            }
            break;
        case M5_PLATFORM_STACKCHAN_FACE_SHY:
            open_eye(left, 8, 8, 25, 36);
            open_eye(right, -8, 8, 25, 36);
            target->drawArc(cx, 124, 25, 18, 40, 140, STACK_HOT);
            if (variant != 1) {
                target->fillCircle(left - 32, 117, 5, STACK_HOT);
                target->fillCircle(right + 32, 117, 5, STACK_HOT);
            }
            break;
        case M5_PLATFORM_STACKCHAN_FACE_WORRIED:
        case M5_PLATFORM_STACKCHAN_FACE_FEAR:
            open_eye(left, 5, cue == M5_PLATFORM_STACKCHAN_FACE_FEAR ? 2 : 7,
                     cue == M5_PLATFORM_STACKCHAN_FACE_FEAR ? 32 : 28,
                     cue == M5_PLATFORM_STACKCHAN_FACE_FEAR ? 44 : 40);
            open_eye(right, -5, cue == M5_PLATFORM_STACKCHAN_FACE_FEAR ? 2 : 7,
                     cue == M5_PLATFORM_STACKCHAN_FACE_FEAR ? 32 : 28,
                     cue == M5_PLATFORM_STACKCHAN_FACE_FEAR ? 44 : 40);
            stackchan_draw_thick_line(target, left - 22, eye_y - 37,
                                      left + 18, eye_y - 29, STACK_HOT, 3);
            stackchan_draw_thick_line(target, right - 18, eye_y - 29,
                                      right + 22, eye_y - 37, STACK_HOT, 3);
            if (cue == M5_PLATFORM_STACKCHAN_FACE_FEAR) {
                target->fillEllipse(cx, 128, 13, 20, STACK_HOT);
                target->fillEllipse(cx, 128, 6, 12, STACK_BG);
            } else {
                target->drawArc(cx, 143, 30, 22, 210, 330, STACK_HOT);
            }
            break;
        case M5_PLATFORM_STACKCHAN_FACE_STERN:
        case M5_PLATFORM_STACKCHAN_FACE_ANGER:
            target->fillRoundRect(left - 25, eye_y - 1, 50,
                                  cue == M5_PLATFORM_STACKCHAN_FACE_ANGER ? 10 : 7,
                                  3, STACK_INK);
            target->fillRoundRect(right - 25, eye_y - 1, 50,
                                  cue == M5_PLATFORM_STACKCHAN_FACE_ANGER ? 10 : 7,
                                  3, STACK_INK);
            stackchan_draw_thick_line(target, left - 22, eye_y - 34,
                                      left + 18, eye_y - 25, STACK_HOT, 4);
            stackchan_draw_thick_line(target, right - 18, eye_y - 25,
                                      right + 22, eye_y - 34, STACK_HOT, 4);
            target->drawArc(cx, 140, 31, 22, 210, 330, STACK_HOT);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_DISGUST:
            target->fillRoundRect(left - 27, eye_y - 4, 54, 8, 4, STACK_INK);
            open_eye(right, 8, 7, 22, 32);
            stackchan_draw_thick_line(target, right - 18, eye_y - 31,
                                      right + 20, eye_y - 36, STACK_HOT, 3);
            target->drawArc(cx + 12, 135, 29, 18, 200, 320, STACK_HOT);
            break;
        case M5_PLATFORM_STACKCHAN_FACE_NEUTRAL:
        default:
            open_eye(left, 0, 1, 27, 39);
            open_eye(right, 0, 1, 27, 39);
            target->drawArc(cx, 122, 34, 25, 35, 145, STACK_HOT);
            break;
    }
}

m5_platform_stackchan_face_cue_t stackchan_current_face(
    bool connection_lost) {
    const auto performance = m5_platform_stackchan_face_cue();
    if (performance != M5_PLATFORM_STACKCHAN_FACE_NEUTRAL) return performance;
    if (connection_lost) return M5_PLATFORM_STACKCHAN_FACE_SAD;
    if (s.action_flash) {
        if (std::strcmp(s.action_notice, "PREVIOUS") == 0)
            return M5_PLATFORM_STACKCHAN_FACE_GLANCE_LEFT;
        if (std::strcmp(s.action_notice, "NEXT") == 0)
            return M5_PLATFORM_STACKCHAN_FACE_GLANCE_RIGHT;
        if (std::strcmp(s.action_notice, "VOLUME UP") == 0)
            return M5_PLATFORM_STACKCHAN_FACE_LOUD;
        if (std::strcmp(s.action_notice, "VOLUME DOWN") == 0)
            return M5_PLATFORM_STACKCHAN_FACE_HUSH;
        if (std::strcmp(s.action_notice, "PLAY") == 0)
            return M5_PLATFORM_STACKCHAN_FACE_POP;
        if (std::strcmp(s.action_notice, "PAUSE") == 0)
            return M5_PLATFORM_STACKCHAN_FACE_RESTING;
        if (std::strcmp(s.action_notice, "CONNECTED") == 0)
            return M5_PLATFORM_STACKCHAN_FACE_RELIEVED;
        if (std::strcmp(s.action_notice, "NEW ROOM") == 0)
            return M5_PLATFORM_STACKCHAN_FACE_PROUD;
    }
    if (!s.online) return M5_PLATFORM_STACKCHAN_FACE_CURIOUS;
    return s.playing ? M5_PLATFORM_STACKCHAN_FACE_ATTENTIVE
                     : M5_PLATFORM_STACKCHAN_FACE_RESTING;
}

void stackchan_draw_marquee(lgfx::LovyanGFX *target, const char *text, int x,
                            int y, int width, int size, uint32_t color,
                            MarqueeState *marquee) {
    const char *value = text ? text : "";
    stackchan_apply_font(target, size);
    if (marquee->started_us == 0 || std::strcmp(marquee->value, value) != 0) {
        copy_text(marquee->value, sizeof(marquee->value), value);
        marquee->x = x;
        marquee->y = y;
        marquee->width = width;
        marquee->size = size;
        marquee->started_us = esp_timer_get_time();
    }
    const int text_width = target->textWidth(value);
    if (text_width <= width) {
        stackchan_draw_text(target, value, x, y, size, color);
        return;
    }
    constexpr int gap = 22;
    constexpr int speed = 34;
    constexpr int pause_ms = 700;
    const int cycle = text_width + gap;
    const int travel_ms = std::max(1, cycle * 1000 / speed);
    const int64_t phase_ms = ((esp_timer_get_time() - marquee->started_us) / 1000) %
                             (2 * pause_ms + travel_ms);
    int offset = 0;
    if (phase_ms >= pause_ms && phase_ms < pause_ms + travel_ms) {
        offset = std::min(cycle, static_cast<int>(
            (phase_ms - pause_ms) * speed / 1000));
    } else if (phase_ms >= pause_ms + travel_ms) {
        offset = cycle;
    }
    int32_t old_x = 0;
    int32_t old_y = 0;
    int32_t old_w = 0;
    int32_t old_h = 0;
    target->getClipRect(&old_x, &old_y, &old_w, &old_h);
    target->setClipRect(x, y, width, target->fontHeight());
    stackchan_draw_text(target, value, x - offset, y, size, color);
    stackchan_draw_text(target, value, x - offset + cycle, y, size, color);
    target->setClipRect(old_x, old_y, old_w, old_h);
}

bool stackchan_marquee_needed() {
    if (s.art_mode || s.picker || s.settings || wifi_mgr_is_ap_mode())
        return false;
    stackchan_apply_font(&M5.Display, 2);
    const bool title_scrolls = M5.Display.textWidth(s.title) > 302;
    stackchan_apply_font(&M5.Display, 1);
    return title_scrolls || M5.Display.textWidth(s.artist) > 302 ||
           M5.Display.textWidth(s.album) > 302;
}

struct ArtworkJob { char key[128]; };
struct ArtworkResult {
    char key[128];
    uint16_t *pixels = nullptr;
};

void start_stackchan_artwork_fetch();

void apply_stackchan_artwork(void *arg) {
    ArtworkResult *result = static_cast<ArtworkResult *>(arg);
    if (!result) {
        s_artwork_loading.store(false);
        return;
    }
    const bool current = std::strcmp(result->key, s.artwork_key) == 0;
    if (current && result->pixels) {
        free(s.artwork_pixels);
        s.artwork_pixels = result->pixels;
        s.artwork_width = STACKCHAN_ARTWORK_SIZE;
        s.artwork_height = STACKCHAN_ARTWORK_SIZE;
        result->pixels = nullptr;
        s.dirty = true;
        ESP_LOGI(TAG, "Kizz artwork ready for '%s'", result->key);
    }
    free(result->pixels);
    free(result);
    s_artwork_loading.store(false);
    if (!current && s.artwork_key[0]) start_stackchan_artwork_fetch();
}

void stackchan_artwork_task(void *arg) {
    ArtworkJob *job = static_cast<ArtworkJob *>(arg);
    if (!job) {
        s_artwork_loading.store(false);
        vTaskDelete(nullptr);
        return;
    }
    char url[384] = {};
    const char *art_url = bridge_client_get_artwork_url_for_format(
        url, sizeof(url), STACKCHAN_ARTWORK_SIZE, STACKCHAN_ARTWORK_SIZE, 0,
        "rgb565");
    if (art_url) {
        uint32_t hash = 2166136261u;
        for (const unsigned char *p =
                 reinterpret_cast<const unsigned char *>(job->key); *p; ++p) {
            hash ^= *p;
            hash *= 16777619u;
        }
        const size_t used = std::strlen(url);
        if (used < sizeof(url)) {
            std::snprintf(url + used, sizeof(url) - used,
                          "&cache_bust=%08" PRIx32, hash);
        }
    }
    char *raw = nullptr;
    size_t raw_len = 0;
    constexpr size_t expected = STACKCHAN_ARTWORK_SIZE * STACKCHAN_ARTWORK_SIZE *
                                sizeof(uint16_t);
    bool posted = false;
    if (art_url && platform_http_get_image(art_url, &raw, &raw_len) == 0 &&
        raw && raw_len == expected) {
        ArtworkResult *result = static_cast<ArtworkResult *>(
            calloc(1, sizeof(*result)));
        uint16_t *pixels = static_cast<uint16_t *>(malloc(expected));
        if (result && pixels) {
            std::memcpy(pixels, raw, expected);
            /* The worker owns this result until it posts it. Do not use the
             * UI copy helper here: that helper also mutates render state. */
            std::snprintf(result->key, sizeof(result->key), "%s", job->key);
            result->pixels = pixels;
            if (platform_task_post_to_ui(apply_stackchan_artwork, result)) {
                posted = true;
                result = nullptr;
                pixels = nullptr;
            }
        }
        free(pixels);
        free(result);
    } else {
        ESP_LOGW(TAG, "Kizz artwork fetch returned %zu bytes (expected %zu)",
                 raw_len, expected);
    }
    platform_http_free(raw);
    free(job);
    if (!posted) s_artwork_loading.store(false);
    vTaskDelete(nullptr);
}

void start_stackchan_artwork_fetch() {
    if (!s.artwork_key[0] || s_artwork_loading.exchange(true)) return;
    ArtworkJob *job = static_cast<ArtworkJob *>(calloc(1, sizeof(*job)));
    if (!job) {
        s_artwork_loading.store(false);
        return;
    }
    copy_text(job->key, sizeof(job->key), s.artwork_key);
    /* Artwork fetches only use network/heap operations and never write flash.
     * Keep this large, short-lived worker out of scarce internal RAM so the
     * ESP-SR voice pipeline and UI task can coexist with album art. */
    if (platform_task_start_external_stack("stackchan_art", 16384,
                                           stackchan_artwork_task, job) != 0) {
        free(job);
        s_artwork_loading.store(false);
        ESP_LOGW(TAG, "Could not start Kizz artwork worker");
    }
}

void stackchan_draw_artwork(lgfx::LovyanGFX *target, int x, int y, int width,
                            int height, bool muted = false) {
    if (!s.artwork_pixels || s.artwork_width <= 0 || s.artwork_height <= 0 ||
        width <= 0 || height <= 0 || width > 466) return;
    uint16_t row[466];
    int crop_width = s.artwork_width;
    int crop_height = s.artwork_height;
    if (width * s.artwork_height > height * s.artwork_width) {
        crop_height = std::max(1, s.artwork_width * height / width);
    } else {
        crop_width = std::max(1, s.artwork_height * width / height);
    }
    const int src_x0 = (s.artwork_width - crop_width) / 2;
    const int src_y0 = (s.artwork_height - crop_height) / 2;
    for (int dy = 0; dy < height; ++dy) {
        const int sy = src_y0 + dy * crop_height / height;
        const uint16_t *src = s.artwork_pixels + sy * s.artwork_width;
        for (int dx = 0; dx < width; ++dx) {
            uint16_t pixel = src[src_x0 + dx * crop_width / width];
            if (muted) {
                const uint16_t r = (pixel >> 11) & 0x1f;
                const uint16_t g = (pixel >> 5) & 0x3f;
                const uint16_t b = pixel & 0x1f;
                pixel = static_cast<uint16_t>(((r * 25 / 100) << 11) |
                                              ((g * 25 / 100) << 5) |
                                              (b * 25 / 100));
            }
            row[dx] = pixel;
        }
        target->pushImage(x, y + dy, width, 1, row);
    }
}

bool load_body_enabled(bool *configured) {
    if (configured) *configured = false;
    nvs_handle_t handle = 0;
    uint8_t value = 0;
    // Body language is on by default.  A stored value is an explicit user
    // choice, so preserve a prior long-hold "off" setting across updates.
    if (nvs_open(kStackChanNvsNamespace, NVS_READONLY, &handle) != ESP_OK)
        return true;
    uint8_t preference = 0;
    // Older builds wrote body_on=false when hardware qualification failed.
    // Only the new preference marker means the user explicitly chose a value.
    if (nvs_get_u8(handle, kStackChanBodyPreferenceKey, &preference) != ESP_OK) {
        nvs_close(handle);
        return true;
    }
    const esp_err_t err = nvs_get_u8(handle, kStackChanBodyKey, &value);
    if (err != ESP_OK) {
        nvs_close(handle);
        return true;
    }
    if (configured) *configured = true;
    nvs_close(handle);
    return value == 1;
}

void save_body_enabled(bool enabled) {
    nvs_handle_t handle = 0;
    if (nvs_open(kStackChanNvsNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return;
    nvs_set_u8(handle, kStackChanBodyKey, enabled ? 1 : 0);
    nvs_set_u8(handle, kStackChanBodyPreferenceKey, 1);
    nvs_commit(handle);
    nvs_close(handle);
}

bool load_sound_enabled(bool *configured = nullptr) {
    if (configured) *configured = false;
    nvs_handle_t handle = 0;
    uint8_t value = 1;
    if (nvs_open(kStackChanNvsNamespace, NVS_READONLY, &handle) != ESP_OK)
        return true;
    uint8_t preference = 0;
    if (nvs_get_u8(handle, kStackChanSoundPreferenceKey, &preference) != ESP_OK ||
        nvs_get_u8(handle, kStackChanSoundKey, &value) != ESP_OK) {
        nvs_close(handle);
        return true;
    }
    nvs_close(handle);
    if (configured) *configured = true;
    return value == 1;
}

void save_sound_enabled(bool enabled) {
    nvs_handle_t handle = 0;
    if (nvs_open(kStackChanNvsNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return;
    nvs_set_u8(handle, kStackChanSoundKey, enabled ? 1 : 0);
    nvs_set_u8(handle, kStackChanSoundPreferenceKey, 1);
    nvs_commit(handle);
    nvs_close(handle);
}

m5_platform_stackchan_volume_t load_voice_volume(bool *configured = nullptr) {
    if (configured) *configured = false;
    nvs_handle_t handle = 0;
    uint8_t value = M5_PLATFORM_STACKCHAN_VOLUME_LOW;
    if (nvs_open(kStackChanNvsNamespace, NVS_READONLY, &handle) != ESP_OK)
        return M5_PLATFORM_STACKCHAN_VOLUME_LOW;
    uint8_t preference = 0;
    const bool found =
        nvs_get_u8(handle, kStackChanVoiceVolumePreferenceKey, &preference) == ESP_OK &&
        nvs_get_u8(handle, kStackChanVoiceVolumeKey, &value) == ESP_OK;
    nvs_close(handle);
    if (!found || value > M5_PLATFORM_STACKCHAN_VOLUME_HIGH)
        return M5_PLATFORM_STACKCHAN_VOLUME_LOW;
    if (configured) *configured = true;
    return static_cast<m5_platform_stackchan_volume_t>(value);
}

void save_voice_volume(m5_platform_stackchan_volume_t volume) {
    nvs_handle_t handle = 0;
    if (nvs_open(kStackChanNvsNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return;
    nvs_set_u8(handle, kStackChanVoiceVolumeKey, static_cast<uint8_t>(volume));
    nvs_set_u8(handle, kStackChanVoiceVolumePreferenceKey, 1);
    nvs_commit(handle);
    nvs_close(handle);
}

void body_notice(const char *message) {
    copy_text(s.body_notice, sizeof(s.body_notice), message);
    s.body_notice_until = esp_timer_get_time() + 1800000;
    s.dirty = true;
}

void set_body_language(bool wanted, bool persist = true) {
    s.body_enabled = m5_platform_stackchan_expression_enable(wanted) && wanted;
    ESP_LOGI(TAG, "Kizz body toggle: wanted=%d enabled=%d faulted=%d",
             wanted, s.body_enabled,
             m5_platform_stackchan_expression_faulted());
    /* Persist the user's intent, not the result of this one hardware attempt.
     * A transient qualification failure should retry after the next boot. */
    if (persist) save_body_enabled(wanted);
    body_notice(s.body_enabled ? "BODY LANGUAGE ON" :
                (wanted ? "SERVOS NOT READY" : "BODY LANGUAGE OFF"));
}

void toggle_body_language() { set_body_language(!s.body_enabled); }

void set_sounds(bool enabled, bool confirm = true, bool persist = true) {
    s.sound_enabled = m5_platform_stackchan_sound_enable(enabled) && enabled;
    if (persist) save_sound_enabled(enabled);
    ESP_LOGI(TAG, "Kizz sound toggle: wanted=%d enabled=%d", enabled,
             s.sound_enabled);
    if (confirm) body_notice(s.sound_enabled ? "SOUNDS ON" : "SOUNDS OFF");
    /* Enabling should prove itself immediately; disabling has already stopped
     * the currently queued phrase in the platform adapter. */
    if (s.sound_enabled && confirm)
        m5_platform_stackchan_sound_trigger(
            M5_PLATFORM_STACKCHAN_SOUND_CONNECTED);
}

void toggle_sounds() { set_sounds(!s.sound_enabled); }

void set_voice_volume(m5_platform_stackchan_volume_t volume,
                      bool preview = true, bool persist = true) {
    if (volume < M5_PLATFORM_STACKCHAN_VOLUME_LOW ||
        volume > M5_PLATFORM_STACKCHAN_VOLUME_HIGH) return;
    if (!m5_platform_stackchan_sound_volume(volume)) {
        body_notice("VOICE LEVEL FAILED");
        return;
    }
    s.voice_volume = volume;
    if (persist) save_voice_volume(volume);
    static constexpr const char *NAMES[] = {"VOICE LOW", "VOICE MEDIUM",
                                            "VOICE HIGH"};
    body_notice(NAMES[static_cast<size_t>(volume)]);
    if (preview && s.sound_enabled)
        m5_platform_stackchan_sound_trigger(M5_PLATFORM_STACKCHAN_SOUND_MORE);
}

bool dispatch(controller_command_t command) {
    controller_action_t action = controller_action_command(command);
    return controller_input_dispatch_action(&action);
}

bool simple(controller_action_kind_t kind) {
    controller_action_t action = controller_action_simple(kind);
    return controller_input_dispatch_action(&action);
}

void flash_action(const char *notice = nullptr) {
    s.action_flash = true;
    s.action_until = esp_timer_get_time() + 550000;
    if (notice) copy_text(s.action_notice, sizeof(s.action_notice), notice);
    s.action_face_variant = static_cast<uint8_t>(esp_random());
#if HIPHI_M5_TARGET_ID == 4
    if (s.controls_mode) s.controls_until = esp_timer_get_time() + 7000000;
    if (notice) {
        if (std::strcmp(notice, "VOLUME UP") == 0)
            m5_platform_stackchan_sound_trigger(M5_PLATFORM_STACKCHAN_SOUND_MORE);
        else if (std::strcmp(notice, "VOLUME DOWN") == 0)
            m5_platform_stackchan_sound_trigger(M5_PLATFORM_STACKCHAN_SOUND_LESS);
        else if (std::strcmp(notice, "PREVIOUS") == 0)
            m5_platform_stackchan_sound_trigger(M5_PLATFORM_STACKCHAN_SOUND_PREVIOUS);
        else if (std::strcmp(notice, "NEXT") == 0)
            m5_platform_stackchan_sound_trigger(M5_PLATFORM_STACKCHAN_SOUND_NEXT);
        else if (std::strcmp(notice, "PLAY") == 0)
            m5_platform_stackchan_sound_trigger(M5_PLATFORM_STACKCHAN_SOUND_PLAY);
        else if (std::strcmp(notice, "PAUSE") == 0)
            m5_platform_stackchan_sound_trigger(M5_PLATFORM_STACKCHAN_SOUND_PAUSE);
        else if (std::strcmp(notice, "CONNECTED") == 0)
            m5_platform_stackchan_sound_trigger(M5_PLATFORM_STACKCHAN_SOUND_CONNECTED);
        else if (std::strcmp(notice, "NEW ROOM") == 0)
            m5_platform_stackchan_sound_trigger(M5_PLATFORM_STACKCHAN_SOUND_NEW_ROOM);
    }
#endif
    s.dirty = true;
}

[[maybe_unused]] void volume_steps(int steps) {
    if (steps && dispatch(controller_command_adjust_volume(steps))) {
        flash_action(steps > 0 ? "VOLUME UP" : "VOLUME DOWN");
    }
}

void toggle_playback() {
    if (dispatch(controller_command_make(CONTROLLER_COMMAND_TOGGLE_PLAYBACK))) {
        flash_action(s.playing ? "PAUSE" : "PLAY");
    }
}

void stackchan_transport(controller_command_kind_t kind, const char *notice) {
    if (dispatch(controller_command_make(kind))) flash_action(notice);
}

void wake_display() {
    const int64_t now = esp_timer_get_time();
    if (s.sleeping) {
        m5_terminal_power_note_runtime_wake();
        m5_platform_display_wake();
        s.sleeping = false;
    }
    s.dimmed = false;
    s.sleep_started_us = 0;
    s.last_activity_us = now;
    m5_platform_set_brightness(210);
    s.dirty = true;
}

void enter_connected_sleep(int64_t now) {
    m5_platform_set_brightness(0);
    m5_platform_display_sleep();
    s.dimmed = true;
    s.sleeping = true;
    s.sleep_started_us = now;
    m5_terminal_power_note_display_sleep();
    ESP_LOGI(TAG, "%s entered connected display sleep",
             m5_platform_board_name());
}

void apply_power_policy(int64_t now, bool artwork_transition_pending) {
    if (m5_terminal_power_debug_due()) {
        (void)m5_terminal_power_off();
        return;
    }
    const m5_power_action_t action = m5_interaction_power_action(
        now, s.last_activity_us, s.sleep_started_us, s.dim_timeout_sec,
        s.sleep_timeout_sec, s.power_off_timeout_sec, s.dimmed, s.sleeping,
        s.setup_mode, artwork_transition_pending);
    switch (action) {
    case M5_POWER_ACTION_DIM:
        m5_platform_set_brightness(20);
        s.dimmed = true;
        ESP_LOGI(TAG, "%s display dimmed", m5_platform_board_name());
        break;
    case M5_POWER_ACTION_CONNECTED_SLEEP:
        enter_connected_sleep(now);
        break;
    case M5_POWER_ACTION_POWER_OFF:
        ESP_LOGI(TAG, "%s connected-sleep timeout reached",
                 m5_platform_board_name());
        (void)m5_terminal_power_off();
        break;
    case M5_POWER_ACTION_NONE:
    default:
        break;
    }
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
    M5.Display.fillRoundRect(w - 42, 4, 36, 30, 7, 0x293446);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(INK, 0x293446);
    M5.Display.drawString("X", w - 24, 19);
    const int visible = std::min(5, s.zone_count);
    if (visible == 0) {
        draw_centered("NO ROOMS YET", h / 2 - 8, 2, INK);
        draw_centered("CHECK YOUR BRIDGE CONNECTION", h / 2 + 22, 1, MUTED);
    }
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
    lgfx::LovyanGFX *target = s_stackchan_canvas_ready
                                 ? static_cast<lgfx::LovyanGFX *>(&s_stackchan_canvas)
                                 : static_cast<lgfx::LovyanGFX *>(&M5.Display);
    const int bob = s.action_flash ? -3 : 0;
    const bool connection_lost = !s.online && s.ever_online;
    const uint32_t surface = connection_lost ? 0x17111b :
                             (s.playing ? 0x10201b : BG);
    target->fillScreen(surface);

    if (s.art_mode && s.artwork_pixels) {
        stackchan_draw_artwork(target, 0, 0, w, h, true);
        target->fillRect(0, 0, w, 25, BG);
        target->fillRect(0, 25, w, 26, 0x101722);
        target->fillRect(0, STACKCHAN_VOLUME_TOP - 1, w,
                         h - STACKCHAN_VOLUME_TOP + 1, BG);
        stackchan_draw_marquee(target, s.title, 8, 33, w - 16, 1, INK,
                               &s_stackchan_marquees[0]);
    }

    stackchan_draw_text(target, s.zone[0] ? s.zone : "NO ROOM", 7, 7, 1,
                        s.online ? ACCENT : HOT);
    target->fillCircle(w - 49, 11, 3, s.online ? ACCENT : HOT);
    if (s.battery >= 0) {
        char battery[12];
        std::snprintf(battery, sizeof(battery), "%d%%", s.battery);
        target->setTextDatum(lgfx::top_right);
        target->setTextSize(1);
        target->setTextColor(MUTED);
        target->drawString(battery, w - 7, 7);
    }

    if (!s.art_mode) {
        const uint32_t eye = s.online ? INK : MUTED;
        if (connection_lost) {
            target->fillEllipse(w / 2 - 54, 59, 22, 8, eye);
            target->fillEllipse(w / 2 + 54, 59, 22, 8, eye);
            target->drawArc(w / 2, 99, 28, 20, 210, 330, MUTED);
        } else if (s.playing) {
            const bool previous = std::strcmp(s.action_notice, "PREVIOUS") == 0;
            const bool next = std::strcmp(s.action_notice, "NEXT") == 0;
            const int glance = s.action_flash ? (next ? 7 : (previous ? -7 : 0)) : 0;
            target->fillEllipse(w / 2 - 54, 58 + bob, 19, 28, eye);
            target->fillEllipse(w / 2 + 54, 58 + bob, 19, 28, eye);
            target->fillCircle(w / 2 - 50 + glance, 51 + bob, 6, ACCENT);
            target->fillCircle(w / 2 + 58 + glance, 51 + bob, 6, ACCENT);
            target->drawArc(w / 2, 92 + bob, 29, 22, 30, 150, HOT);
        } else {
            target->fillRoundRect(w / 2 - 76, 58 + bob, 48, 7, 4, eye);
            target->fillRoundRect(w / 2 + 28, 58 + bob, 48, 7, 4, eye);
            target->fillRoundRect(w / 2 - 20, 92 + bob, 40, 6, 3, MUTED);
        }

        const char *expression = s.action_flash && s.action_notice[0]
                                     ? s.action_notice
                                     : (s.body_notice[0] ? s.body_notice
                                        : (connection_lost ? "I LOST THE MUSIC"
                                           : (!s.online ? "CONNECTING..."
                                           : (s.playing ? "I'M INTO THIS"
                                              : (s.body_enabled ? "READY"
                                                 : "BODY OFF")))));
        stackchan_draw_center(target, expression, w / 2, 27, 1,
                              connection_lost ? HOT :
                              (s.action_flash ? HOT :
                               (s.body_enabled ? ACCENT : MUTED)));

        target->fillRect(0, STACKCHAN_FACE_BOTTOM, w,
                         STACKCHAN_VOLUME_TOP - STACKCHAN_FACE_BOTTOM, BG);
        if (s.artwork_pixels) {
            stackchan_draw_artwork(target, 5, 112, 52, 52);
        } else {
            target->fillRoundRect(5, 112, 52, 52, 6, 0x1b2330);
            stackchan_draw_center(target, "NO", 31, 130, 1, MUTED);
            stackchan_draw_center(target, "ART", 31, 145, 1, MUTED);
        }
        stackchan_draw_marquee(target, s.title, 64, 112, w - 69, 1, INK,
                               &s_stackchan_marquees[0]);
        stackchan_draw_marquee(target, s.artist, 64, 130, w - 69, 1, 0x9aa7ba,
                               &s_stackchan_marquees[1]);
        stackchan_draw_marquee(target, s.album, 64, 148, w - 69, 1, MUTED,
                               &s_stackchan_marquees[2]);
        if (s.seek_length > 0) {
            const int progress = std::clamp(
                static_cast<int>((static_cast<int64_t>(s.seek_position) * (w - 69)) /
                                 s.seek_length), 0, w - 69);
            target->fillRect(64, 162, w - 69, 2, 0x293446);
            if (progress > 0) target->fillRect(64, 162, progress, 2, ACCENT);
        }
    }

    target->fillRect(0, STACKCHAN_VOLUME_TOP, w, 30, 0x111722);
    target->fillRoundRect(4, 171, 43, 24, 7, 0x293446);
    target->fillRoundRect(w - 47, 171, 43, 24, 7, 0x293446);
    stackchan_draw_center(target, "-", 25, 183, 2, INK);
    stackchan_draw_center(target, "+", w - 25, 183, 2, INK);
    const float range = std::max(1.0f, s.volume_max - s.volume_min);
    const float ratio = std::clamp((s.volume - s.volume_min) / range, 0.0f, 1.0f);
    target->fillRoundRect(53, 174, w - 106, 18, 5, 0x202a38);
    const int volume_fill = static_cast<int>((w - 106) * ratio);
    if (volume_fill > 0)
        target->fillRoundRect(53, 174, volume_fill, 18, 5,
                              s.action_flash ? HOT : ACCENT);
    char volume[24];
    std::snprintf(volume, sizeof(volume), "VOL %.1f", s.volume);
    stackchan_draw_center(target, volume, w / 2, 183, 1, INK);

    target->fillRect(0, STACKCHAN_TRANSPORT_TOP, w,
                     h - STACKCHAN_TRANSPORT_TOP, surface);
    constexpr int gap = 4;
    const int button_w = (w - 4 * gap) / 3;
    const char *labels[] = {"|<", s.playing ? "II" : ">", ">|"};
    for (int index = 0; index < 3; ++index) {
        const int x = gap + index * (button_w + gap);
        const uint32_t color = index == 1 ? ACCENT : 0x293446;
        target->fillRoundRect(x, STACKCHAN_TRANSPORT_TOP + 2, button_w,
                              h - STACKCHAN_TRANSPORT_TOP - 4, 8, color);
        stackchan_draw_center(target, labels[index], x + button_w / 2,
                              (STACKCHAN_TRANSPORT_TOP + h) / 2, 2,
                              index == 1 ? BG : INK);
    }
    if (s_stackchan_canvas_ready) s_stackchan_canvas.pushSprite(0, 0);
}

/* Kizz is a character first. Information and controls arrive as
 * deliberate temporal layers instead of permanently shrinking the face into
 * the top half of a generic dashboard. */
void render_stackchan_delight() {
    const int w = M5.Display.width();
    const int h = M5.Display.height();
    const int64_t now = esp_timer_get_time();
    const bool connection_lost = !s.online && s.ever_online;
    const bool reveal = s.track_reveal_until > now;
    lgfx::LovyanGFX *target = s_stackchan_canvas_ready
                                 ? static_cast<lgfx::LovyanGFX *>(&s_stackchan_canvas)
                                 : static_cast<lgfx::LovyanGFX *>(&M5.Display);
    const uint32_t mood = connection_lost ? 0x160d12 : STACK_BG;
    target->fillScreen(mood);

    if (s.artwork_pixels && !connection_lost && !reveal) {
        stackchan_draw_artwork(target, 0, 0, w, h,
                               !s.art_mode);
    }

    if (reveal) {
        stackchan_draw_center(target, "NOW PLAYING", w / 2, 20, 1,
                              STACK_ACCENT);
        auto reveal_face = m5_platform_stackchan_face_cue();
        if (reveal_face == M5_PLATFORM_STACKCHAN_FACE_NEUTRAL)
            reveal_face = M5_PLATFORM_STACKCHAN_FACE_ATTENTIVE;
        stackchan_draw_performance_face(
            target, reveal_face, w,
            m5_stackchan_face_variant(reveal_face, s.ambient_face_variant));

        target->fillRect(0, 153, w, h - 153, STACK_BG);
        target->fillRect(0, 153, w, 2, STACK_CONTROL);
        stackchan_draw_marquee(target, s.title, 10, 160, w - 20, 2, STACK_INK,
                               &s_stackchan_marquees[0]);
        stackchan_draw_marquee(target, s.artist, 10, 196, w - 20, 1,
                               STACK_SECONDARY,
                               &s_stackchan_marquees[1]);
        if (s.seek_length > 0) {
            const int progress = std::clamp(static_cast<int>(
                static_cast<int64_t>(s.seek_position) * w / s.seek_length), 0, w);
            target->fillRect(0, h - 4, w, 4, STACK_CONTROL);
            if (progress > 0)
                target->fillRect(0, h - 4, progress, 4, STACK_ACCENT);
        }
        stackchan_draw_voice_diagnostics(target, w, h);
        if (s_stackchan_canvas_ready) s_stackchan_canvas.pushSprite(0, 0);
        return;
    }

    /* Dial's Art mode contract: the sleeve owns every pixel. It is a
     * persistent display state, not a controls timeout or another dashboard. */
    if (s.art_mode && !s.voice_listening && s.artwork_pixels &&
        !connection_lost) {
        stackchan_draw_voice_diagnostics(target, w, h);
        if (s_stackchan_canvas_ready) s_stackchan_canvas.pushSprite(0, 0);
        return;
    }

    target->fillRoundRect(6, 5, 142, 30, 15, STACK_BG);
    stackchan_draw_center(target, s.zone[0] ? s.zone : "NO ROOM", 77, 20, 1,
                          s.online ? STACK_INK : STACK_HOT);
    target->fillRoundRect(w - 70, 5, 64, 30, 15, STACK_BG);
    if (s.battery >= 0) {
        char battery[12];
        std::snprintf(battery, sizeof(battery), "%d%%", s.battery);
        stackchan_draw_center(target, battery, w - 38, 20, 1, STACK_SECONDARY);
    }

    if (s.controls_mode) {
        target->fillRoundRect(10, 40, w - 20, 54, 14, STACK_BG);
        stackchan_draw_marquee(target, s.title, 22, 45, w - 44, 2, STACK_INK,
                               &s_stackchan_marquees[0]);
        stackchan_draw_marquee(target, s.artist, 22, 73, w - 44, 1,
                               STACK_SECONDARY,
                               &s_stackchan_marquees[1]);

        target->fillRoundRect(10, 100, w - 20, 38, 14, STACK_BG);
        target->fillRoundRect(16, 103, 48, 32, 11, STACK_CONTROL);
        target->fillRoundRect(w - 64, 103, 48, 32, 11, STACK_CONTROL);
        stackchan_draw_center(target, "-", 40, 119, 2, STACK_INK);
        stackchan_draw_center(target, "+", w - 40, 119, 2, STACK_INK);
        const float range = std::max(1.0f, s.volume_max - s.volume_min);
        const float ratio = std::clamp((s.volume - s.volume_min) / range,
                                       0.0f, 1.0f);
        target->fillRoundRect(72, 107, w - 144, 24, 8, STACK_CONTROL);
        const int filled = static_cast<int>((w - 144) * ratio);
        if (filled > 0)
            target->fillRoundRect(72, 107, std::min(filled, w - 144), 24, 8,
                                  s.action_flash ? STACK_HOT : STACK_ACCENT);
        char volume[24];
        std::snprintf(volume, sizeof(volume), "%.1f dB", s.volume);
        stackchan_draw_center(target, volume, w / 2, 119, 1, STACK_INK);

        constexpr int gap = 7;
        const int button_w = (w - 4 * gap) / 3;
        const char *icons[] = {"|<", s.playing ? "II" : ">", ">|"};
        const char *names[] = {"PREV", s.playing ? "PAUSE" : "PLAY", "NEXT"};
        for (int index = 0; index < 3; ++index) {
            const int x = gap + index * (button_w + gap);
            target->fillRoundRect(x, 145, button_w, 70, 18,
                                  index == 1 ? STACK_ACCENT : STACK_CONTROL);
            stackchan_draw_center(target, icons[index], x + button_w / 2, 169,
                                  3, index == 1 ? STACK_BG : STACK_INK);
            stackchan_draw_center(target, names[index], x + button_w / 2, 200,
                                  1, index == 1 ? STACK_BG : STACK_SECONDARY);
        }
        const char *hint = s.action_flash && s.action_notice[0]
                               ? s.action_notice
                               : (s.artwork_pixels
                                      ? "TAP THE SLEEVE FOR ART MODE"
                                      : "TAP ABOVE TO RETURN TO FACE");
        stackchan_draw_center(target, hint, w / 2, 229, 1,
                              s.action_flash ? STACK_HOT : STACK_TERTIARY);
        stackchan_draw_voice_diagnostics(target, w, h);
        if (s_stackchan_canvas_ready) s_stackchan_canvas.pushSprite(0, 0);
        return;
    }

    const auto face = stackchan_current_face(connection_lost);
    const uint8_t face_entropy = s.action_flash ? s.action_face_variant
                                                 : s.ambient_face_variant;
    stackchan_draw_performance_face(
        target, face, w, m5_stackchan_face_variant(face, face_entropy));
    const char *expression = s.voice_listening
                                 ? "LISTENING"
                                 : (s.action_flash && s.action_notice[0]
                                 ? s.action_notice
                                 : (s.body_notice[0] ? s.body_notice
                                    : (connection_lost ? "I LOST THE MUSIC"
                                       : (!s.online ? "CONNECTING..."
                                          : (!s.body_enabled ? "BODY OFF"
                                             : "")))));
    stackchan_draw_center(target, expression, w / 2, 40, 1,
                          connection_lost ? STACK_HOT :
                          (s.action_flash ? STACK_HOT : STACK_INK));

    target->fillRect(0, 157, w, h - 157, STACK_BG);
    stackchan_draw_marquee(target, s.title, 9, 162, w - 18, 2, STACK_INK,
                           &s_stackchan_marquees[0]);
    stackchan_draw_marquee(target, s.artist, 9, 193, w - 18, 1,
                           STACK_SECONDARY,
                           &s_stackchan_marquees[1]);
    stackchan_draw_center(target, "TOUCH FOR CONTROLS", w / 2, 224, 1,
                          STACK_TERTIARY);
    if (s.seek_length > 0) {
        const int progress = std::clamp(static_cast<int>(
            static_cast<int64_t>(s.seek_position) * w / s.seek_length), 0, w);
        target->fillRect(0, 156, w, 2, STACK_CONTROL);
        if (progress > 0)
            target->fillRect(0, 156, progress, 2, STACK_ACCENT);
    }
    stackchan_draw_voice_diagnostics(target, w, h);
    if (s_stackchan_canvas_ready) s_stackchan_canvas.pushSprite(0, 0);
}

void render_provisioning() {
    const int w = M5.Display.width();
    const int h = M5.Display.height();
#if HIPHI_M5_TARGET_ID == 4
    M5.Display.fillScreen(STACK_BG);
    M5.Display.fillEllipse(w / 2 - 62, 68, 26, 9, STACK_SECONDARY);
    M5.Display.fillEllipse(w / 2 + 62, 68, 26, 9, STACK_SECONDARY);
    M5.Display.drawArc(w / 2, 116, 34, 24, 210, 330, STACK_SECONDARY);
    stackchan_draw_center(&M5.Display, "I WANT TO CONNECT", w / 2, 20, 1,
                          STACK_HOT);
    stackchan_draw_center(&M5.Display, platform_provisioning_ssid(), w / 2,
                          145, 1, STACK_INK);
    stackchan_draw_center(&M5.Display, "OPEN 192.168.4.1", w / 2, 166, 1,
                          STACK_ACCENT);
    const rk_wifi_scan_state_t scan_state = wifi_mgr_scan_state();
    if (scan_state == RK_WIFI_SCAN_IDLE || scan_state == RK_WIFI_SCAN_FAILED)
        (void)wifi_mgr_scan_start();
    rk_wifi_network_t networks[RK_WIFI_SCAN_MAX_NETWORKS] = {};
    const size_t count = scan_state == RK_WIFI_SCAN_READY
                             ? wifi_mgr_scan_results_copy(
                                   networks, RK_WIFI_SCAN_MAX_NETWORKS)
                             : 0;
    char scan_line[48];
    if (scan_state == RK_WIFI_SCAN_READY)
        std::snprintf(scan_line, sizeof(scan_line), "%u NETWORKS FOUND",
                      static_cast<unsigned>(count));
    else
        copy_text(scan_line, sizeof(scan_line), "SCANNING FOR NETWORKS...");
    stackchan_draw_center(&M5.Display, scan_line, w / 2, 187, 1,
                          STACK_SECONDARY);
    M5.Display.fillRoundRect(36, 207, w - 72, 29, 12, STACK_CONTROL);
    stackchan_draw_center(&M5.Display, "RETRY SAVED WI-FI", w / 2, 221, 1,
                          STACK_INK);
#else
    M5.Display.fillScreen(BG);
    draw_centered("WI-FI SETUP", 28, 2, ACCENT);
    draw_centered("JOIN THIS NETWORK", 66, 1, MUTED);
    draw_centered(platform_provisioning_ssid(), 94, 1, INK);
    draw_centered("OPEN 192.168.4.1", 136, 2, INK);
    draw_centered("TO CONFIGURE WI-FI", 168, 1, MUTED);
    draw_centered("THEN RESTART", h - 24, 1, ACCENT);
#endif
}

void render() {
    if (wifi_mgr_is_ap_mode()) return render_provisioning();
    if (s.picker) return render_picker();
    if (s.settings) {
#if HIPHI_M5_TARGET_ID == 4
        M5.Display.fillScreen(STACK_BG);
        const int w = M5.Display.width();
        stackchan_draw_center(&M5.Display, "PERSONALITY", w/2, 22, 2,
                              STACK_INK);
        M5.Display.fillRoundRect(24, 45, w-48, 40, 12, STACK_CONTROL);
        stackchan_draw_text(&M5.Display, "BODY", 42, 56, 1, STACK_INK);
        stackchan_draw_text(&M5.Display,
                            s.body_enabled ? "ON" : "OFF", w-79, 56, 1,
                            s.body_enabled ? STACK_ACCENT : STACK_TERTIARY);
        M5.Display.fillRoundRect(24, 91, w-48, 40, 12, STACK_CONTROL);
        stackchan_draw_text(&M5.Display, "SOUNDS", 42, 102, 1, STACK_INK);
        stackchan_draw_text(&M5.Display,
                            s.sound_enabled ? "ON" : "OFF", w-79, 102, 1,
                            s.sound_enabled ? STACK_ACCENT : STACK_TERTIARY);
        M5.Display.fillRoundRect(24, 137, w-48, 40, 12, STACK_CONTROL);
        stackchan_draw_text(&M5.Display, "VOICE", 36, 148, 1, STACK_INK);
        static constexpr const char *VOLUME_LABELS[] = {"LOW", "MED", "HIGH"};
        for (int level = 0; level < 3; ++level) {
            const int x = 116 + level * 57;
            const bool selected = static_cast<int>(s.voice_volume) == level;
            M5.Display.fillRoundRect(x, 143, 53, 28, 8,
                                    selected ? STACK_ACCENT : STACK_BG);
            stackchan_draw_center(&M5.Display, VOLUME_LABELS[level], x + 26,
                                  151, 1,
                                  selected ? STACK_BG : STACK_TERTIARY);
        }
        stackchan_draw_center(&M5.Display, "BUTTON x2: SOUND", w/2, 187, 1,
                              STACK_TERTIARY);
        M5.Display.fillRoundRect(42, M5.Display.height()-37,
                                 M5.Display.width()-84, 29, 11, STACK_CONTROL);
        stackchan_draw_center(&M5.Display, "CLOSE", M5.Display.width()/2,
                              M5.Display.height()-30, 1, STACK_INK);
#else
        M5.Display.fillScreen(BG);
        draw_centered("SETUP", M5.Display.height()/2 - 30, 2, ACCENT);
        draw_centered(s.network, M5.Display.height()/2 + 10, 1, INK);
#endif
        return;
    }
#if HIPHI_M5_TARGET_ID == 1
    render_dial();
#elif HIPHI_M5_TARGET_ID == 2
    render_stick();
#elif HIPHI_M5_TARGET_ID == 3
    render_stopwatch();
#else
    render_stackchan_delight();
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
    const bool button_activity = m5_platform_surface_button_event(&buttons);
    m5_platform_touch_event_t touch = {};
    [[maybe_unused]] const bool touched = m5_platform_touch_event(&touch);
    if (button_activity || touched) wake_display();

#if HIPHI_M5_TARGET_ID == 4
    if (wifi_mgr_is_ap_mode()) {
        if (touched && touch.state == M5_PLATFORM_TOUCH_CLICKED && touch.y >= 198) {
            wifi_mgr_stop_ap();
        } else if (touched && touch.state == M5_PLATFORM_TOUCH_HELD &&
                   !s.body_hold_consumed && touch.y < 145) {
            s.body_hold_consumed = true;
            toggle_body_language();
        } else if (touched && touch.state == M5_PLATFORM_TOUCH_RELEASED) {
            s.body_hold_consumed = false;
        }
        return;
    }
#endif

#if HIPHI_M5_TARGET_ID == 1
    int32_t delta = 0;
    m5_platform_encoder_delta(&delta);
    if (delta) wake_display();
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
#else
    if (buttons.double_clicked) {
        toggle_sounds();
        return;
    }
    if (buttons.single_clicked) {
        s.settings = !s.settings;
        s.picker = false;
        s.art_mode = false;
        s.dirty = true;
        return;
    }
    if (touched) {
        s.last_activity_us = now;
        /* Transport remains direct even in Art mode: artwork is the controller,
         * not a lock screen. Only a completed tap exits to the control chrome. */
        if (s.art_mode) {
            if (touch.state == M5_PLATFORM_TOUCH_DRAGGING &&
                !s.gesture_consumed && touch.y < 180 &&
                std::abs(touch.delta_x) > 18) {
                s.gesture_consumed = true;
                s.touch_quarantine_until = now + 450000;
                stackchan_transport(
                    touch.delta_x < 0 ? CONTROLLER_COMMAND_NEXT_TRACK
                                      : CONTROLLER_COMMAND_PREVIOUS_TRACK,
                    touch.delta_x < 0 ? "NEXT" : "PREVIOUS");
                return;
            }
            if (touch.state == M5_PLATFORM_TOUCH_RELEASED) {
                s.body_hold_consumed = false;
                s.gesture_consumed = false;
                return;
            }
            if (touch.state == M5_PLATFORM_TOUCH_CLICKED) {
                s.art_mode = false;
                s.gesture_consumed = true;
                s.touch_quarantine_until = now + 500000;
                s.dirty = true;
                return;
            }
        }
    }
    if (s.picker) {
        if (touched && touch.state == M5_PLATFORM_TOUCH_DRAGGING &&
            std::abs(touch.delta_y) > 8)
            picker_input(touch.delta_y > 0 ? -1 : 1, false);
        if (touched && touch.state == M5_PLATFORM_TOUCH_CLICKED) {
            if (touch.y < 38 && touch.x > M5.Display.width() - 52) {
                touch_ui_hide_zone_picker();
            } else if (s.zone_count > 0 && touch.y >= 35) {
                const int visible = std::min(5, s.zone_count);
                const int first = std::max(0, std::min(
                    s.zone_selected - visible / 2, s.zone_count - visible));
                const int spacing = std::max(
                    28, static_cast<int>((M5.Display.height() - 58) /
                                         std::max(1, visible)));
                const int row = std::clamp((touch.y - 35) / spacing, 0,
                                           std::max(0, visible - 1));
                s.zone_selected = first + row;
                picker_input(0, true);
            }
        }
    } else if (s.settings) {
        if (touched && touch.state == M5_PLATFORM_TOUCH_CLICKED) {
            if (touch.y >= 42 && touch.y < 88) {
                toggle_body_language();
            } else if (touch.y >= 88 && touch.y < 134) {
                toggle_sounds();
            } else if (touch.y >= 134 && touch.y < 181) {
                int level = touch.x < 116
                    ? (static_cast<int>(s.voice_volume) + 1) % 3
                    : std::clamp((touch.x - 116) / 57, 0, 2);
                set_voice_volume(
                    static_cast<m5_platform_stackchan_volume_t>(level));
            } else if (touch.y >= 198) {
                s.settings = false;
                s.dirty = true;
            }
        }
    } else if (touched && touch.state == M5_PLATFORM_TOUCH_DRAGGING &&
               !s.gesture_consumed && touch.y < 160 &&
               std::abs(touch.delta_x) > 18) {
        s.gesture_consumed = true;
        s.touch_quarantine_until = now + 450000;
        stackchan_transport(touch.delta_x < 0 ? CONTROLLER_COMMAND_NEXT_TRACK
                                              : CONTROLLER_COMMAND_PREVIOUS_TRACK,
                            touch.delta_x < 0 ? "NEXT" : "PREVIOUS");
    } else if (touched && touch.state == M5_PLATFORM_TOUCH_DRAGGING &&
               !s.gesture_consumed && touch.delta_y < -18 &&
               touch.y < 160) {
        s.gesture_consumed = true;
        s.touch_quarantine_until = now + 450000;
        simple(CONTROLLER_ACTION_OPEN_ZONE_PICKER);
    } else if (touched && touch.state == M5_PLATFORM_TOUCH_HELD &&
               !s.controls_mode && !s.gesture_consumed && touch.y < 157 &&
               !s.body_hold_consumed) {
        s.body_hold_consumed = true;
        s.gesture_consumed = true;
        s.touch_quarantine_until = now + 600000;
        toggle_body_language();
    } else if (touched && touch.state == M5_PLATFORM_TOUCH_RELEASED) {
        s.body_hold_consumed = false;
        s.gesture_consumed = false;
    } else if (touched && touch.state == M5_PLATFORM_TOUCH_CLICKED &&
               now >= s.touch_quarantine_until) {
        const int w = M5.Display.width();
        if (touch.y < 25) {
            simple(CONTROLLER_ACTION_OPEN_ZONE_PICKER);
        } else if (s.controls_mode) {
            s.controls_until = now + 7000000;
            if (touch.y >= 25 && touch.y < 96 && s.artwork_pixels) {
                s.controls_mode = false;
                s.controls_until = 0;
                s.art_mode = true;
                s.dirty = true;
            } else if (touch.y >= 89 && touch.y < 132) {
                if (touch.x < 65) volume_steps(-1);
                else if (touch.x > w - 65) volume_steps(1);
            } else if (touch.y >= 136 && touch.y < 216) {
                if (touch.x < w / 3)
                    stackchan_transport(CONTROLLER_COMMAND_PREVIOUS_TRACK,
                                        "PREVIOUS");
                else if (touch.x < 2 * w / 3)
                    toggle_playback();
                else
                    stackchan_transport(CONTROLLER_COMMAND_NEXT_TRACK, "NEXT");
            } else {
                s.controls_mode = false;
                s.dirty = true;
            }
        } else if (touch.y >= 157) {
            s.controls_mode = true;
            s.controls_until = now + 7000000;
            s.track_reveal_until = 0;
            s.dirty = true;
        } else {
            toggle_playback();
        }
    }
#endif
}
}

extern "C" void touch_ui_init(void) {
    s.last_activity_us = esp_timer_get_time();
#if HIPHI_M5_TARGET_ID == 4
    s_stackchan_canvas.setColorDepth(16);
    /* Network RGB565 pixels arrive byte-swapped for the panel.  Match the
     * proven Tough sprite path so both artwork and primitive colors survive
     * composition without a second, psychedelic byte swap. */
    s_stackchan_canvas.setSwapBytes(true);
    s_stackchan_canvas_ready = s_stackchan_canvas.createSprite(
        M5.Display.width(), M5.Display.height()) != nullptr;
    if (!s_stackchan_canvas_ready)
        ESP_LOGW(TAG, "Kizz double buffer unavailable; drawing directly");
    bool configured = false;
    const bool wanted = load_body_enabled(&configured);
    s.body_enabled = m5_platform_stackchan_expression_enable(wanted) && wanted;
    ESP_LOGI(TAG, "Kizz body startup: wanted=%d configured=%d enabled=%d faulted=%d",
             wanted, configured, s.body_enabled,
             m5_platform_stackchan_expression_faulted());
    (void)configured;
    bool sound_configured = false;
    const bool sound_wanted = load_sound_enabled(&sound_configured);
    bool volume_configured = false;
    s.voice_volume = load_voice_volume(&volume_configured);
    const bool volume_ready =
        m5_platform_stackchan_sound_volume(s.voice_volume);
    s.sound_enabled =
        m5_platform_stackchan_sound_enable(sound_wanted) && sound_wanted;
    ESP_LOGI(TAG,
             "Kizz sound startup: wanted=%d configured=%d enabled=%d "
             "level=%u level_configured=%d level_ready=%d",
             sound_wanted, sound_configured, s.sound_enabled,
             static_cast<unsigned>(s.voice_volume), volume_configured,
             volume_ready);
#endif
    s.setup_mode = wifi_mgr_is_ap_mode();
    render();
}
extern "C" void touch_ui_process(void) {
    process_input();
    const int64_t now = esp_timer_get_time();
    const bool setup_mode = wifi_mgr_is_ap_mode();
    if (setup_mode != s.setup_mode) {
        s.setup_mode = setup_mode;
        s.dirty = true;
        if (setup_mode && (s.sleeping || s.dimmed)) wake_display();
#if HIPHI_M5_TARGET_ID == 4
        if (setup_mode && s.body_enabled)
            m5_platform_stackchan_expression_trigger(M5_PLATFORM_STACKCHAN_SAD);
#endif
    }
    if (setup_mode && wifi_mgr_scan_state() == RK_WIFI_SCAN_RUNNING)
        s.dirty = true;
    m5_platform_stackchan_expression_process();
    if (s.action_flash && now >= s.action_until) {
        s.action_flash=false;
        s.action_notice[0]=0;
        s.dirty=true;
    }
    if (s.body_notice[0] && now >= s.body_notice_until) {
        s.body_notice[0] = 0; s.dirty = true;
    }
    if (s.body_enabled && m5_platform_stackchan_expression_faulted()) {
        /* The rail is already off and motion is faulted for this boot. Keep
         * the user's preference so a transient fault is retried next boot. */
        s.body_enabled = false;
        body_notice("BODY SAFELY DISABLED");
    }
#if HIPHI_M5_TARGET_ID == 4
    if (now >= s.voice_diagnostics_next) {
        s.voice_diagnostics_next = now + 100000;
        const char *voice_state = m5_platform_voice_state();
        const bool diagnostics = m5_platform_voice_diagnostics_enabled();
        const uint8_t score = static_cast<uint8_t>(std::clamp(
            static_cast<int>(m5_platform_voice_wake_probability() * 100.0f +
                             0.5f), 0, 100));
        const uint8_t cutoff = static_cast<uint8_t>(std::clamp(
            static_cast<int>(m5_platform_voice_wake_cutoff() * 100.0f + 0.5f),
            0, 100));
        if (diagnostics != s.voice_diagnostics) {
            s.voice_diagnostics = diagnostics;
            s.art_timeout_sec = diagnostics ? 0 : s.configured_art_timeout_sec;
            s.dim_timeout_sec = diagnostics ? 0 : s.configured_dim_timeout_sec;
            s.sleep_timeout_sec = diagnostics ? 0 : s.configured_sleep_timeout_sec;
            s.power_off_timeout_sec = diagnostics
                ? 0 : s.configured_power_off_timeout_sec;
            if (diagnostics) {
                s.art_mode = false;
                wake_display();
            }
            s.dirty = true;
        }
        if (std::strcmp(voice_state, s.voice_state) != 0 ||
            score != s.voice_score_percent ||
            cutoff != s.voice_cutoff_percent) {
            copy_text(s.voice_state, sizeof(s.voice_state), voice_state);
            s.voice_score_percent = score;
            s.voice_cutoff_percent = cutoff;
            s.dirty = true;
        }
    }
    const bool voice_listening = m5_platform_voice_is_listening();
    if (voice_listening != s.voice_listening) {
        s.voice_listening = voice_listening;
        if (voice_listening) {
            s.art_mode = false;
            wake_display();
        }
        s.dirty = true;
    }
    if (s.controls_mode && s.controls_until && now >= s.controls_until) {
        s.controls_mode = false;
        s.controls_until = 0;
        s.dirty = true;
    }
    if (s.track_reveal_until && now >= s.track_reveal_until) {
        s.track_reveal_started = 0;
        s.track_reveal_until = 0;
        s.dirty = true;
    }
    const bool art_eligible = !s.setup_mode && !s.picker && !s.settings &&
        !s.controls_mode && !s.art_mode && s.artwork_pixels && s.online &&
        bridge_client_is_ready_for_art_mode() && s.art_timeout_sec > 0;
    if (art_eligible &&
        now - s.last_activity_us >=
            static_cast<int64_t>(s.art_timeout_sec) * 1000000) {
        s.art_mode = true;
        wake_display();
        s.dirty = true;
        ESP_LOGI(TAG, "Kizz entered Art mode after %us",
                 static_cast<unsigned>(s.art_timeout_sec));
    }
    if (s.track_reveal_until || stackchan_marquee_needed()) s.dirty = true;
    const bool artwork_transition_pending = art_eligible && !s.art_mode;
#else
    const bool artwork_transition_pending = false;
#endif
    if (s.sleeping && s.sleep_timeout_sec == 0) wake_display();
    apply_power_policy(now, artwork_transition_pending);
    if (s.dirty && !s.sleeping) { s.dirty=false; render(); }
}
extern "C" void touch_ui_set_status(bool v){
    if(s.online!=v){
        const bool lost = s.online && !v;
        const bool found = !s.online && v;
        s.online=v;if(v)s.ever_online=true;s.dirty=true;
        if (lost && s.body_enabled)
            m5_platform_stackchan_expression_trigger(M5_PLATFORM_STACKCHAN_SAD);
        if (lost)
            m5_platform_stackchan_sound_trigger(M5_PLATFORM_STACKCHAN_SOUND_LOST);
        if (found) flash_action("CONNECTED");
    }
}
extern "C" void touch_ui_set_message(const char *v){touch_ui_set_network_status(v);}
extern "C" void touch_ui_set_zone_name(const char *v){
    const char *zone=v?v:"";
    if(std::strcmp(s.zone,zone)!=0){
        /* Startup can publish the configured label, clear it while resolving,
         * then publish it again. A room response is only truthful after the
         * first playback snapshot has established a stable session. */
        const bool changed=s.zone_seen&&s.playback_seen&&s.online&&zone[0];
        copy_text(s.zone,sizeof(s.zone),zone);s.dirty=true;
        if(zone[0])s.zone_seen=true;
        if(changed)flash_action("NEW ROOM");
    }
}
extern "C" void touch_ui_set_network_status(const char *v){if(std::strcmp(s.network,v?v:"")!=0){copy_text(s.network,sizeof(s.network),v);s.dirty=true;}}
extern "C" void touch_ui_post_zone_name(const char *v){char *c=strdup(v?v:"");platform_task_post_to_ui([](void*p){touch_ui_set_zone_name(static_cast<char*>(p));free(p);},c);}
extern "C" void touch_ui_post_network_status(const char *v){char *c=strdup(v?v:"");platform_task_post_to_ui([](void*p){touch_ui_set_network_status(static_cast<char*>(p));free(p);},c);}
extern "C" void touch_ui_set_artwork(const char *v){
    const char *key=v?v:"";
    if(std::strcmp(s.artwork_key,key)==0){
        if(key[0]&&!s.artwork_pixels)start_stackchan_artwork_fetch();
        return;
    }
    copy_text(s.artwork_key,sizeof(s.artwork_key),key);
    free(s.artwork_pixels);s.artwork_pixels=nullptr;
    s.artwork_width=0;s.artwork_height=0;s.dirty=true;
    if(key[0])start_stackchan_artwork_fetch();
}
extern "C" void touch_ui_post_artwork(const char *v){
    char *c=strdup(v?v:"");
    if(!c||!platform_task_post_to_ui([](void*p){
        touch_ui_set_artwork(static_cast<char*>(p));free(p);},c))free(c);
}
extern "C" void touch_ui_show_volume_change(float v,float step){
    (void)step;
#if HIPHI_M5_TARGET_ID == 4
    const bool up=v>s.volume;
    const bool already=s.action_flash&&
        (std::strcmp(s.action_notice,"VOLUME UP")==0||
         std::strcmp(s.action_notice,"VOLUME DOWN")==0);
    s.volume=v;s.dirty=true;
    if(!already)flash_action(up?"VOLUME UP":"VOLUME DOWN");
#else
    s.volume=v;s.dirty=true;
#endif
#if HIPHI_M5_TARGET_ID == 4
#else
    flash_action();
#endif
}
extern "C" void touch_ui_update(const char *a,const char *b,const char *c,bool p,float v,float min,float max,float step,int pos,int length){
    (void)step;
    const char *title=a?a:""; const char *artist=b?b:""; const char *album=c?c:"";
    /* Recovery can briefly publish empty presentation fields between two
     * otherwise identical polls. Preserve the last non-empty identity so
     * blank -> restored metadata cannot impersonate a new song. Album display
     * refinements also do not define a new track. */
    const bool changed=s.track_seen && title[0] &&
        (std::strcmp(s.track_identity_title,title)!=0 ||
         std::strcmp(s.track_identity_artist,artist)!=0);
    const bool playback_changed=s.playback_seen&&s.playing!=p;
    copy_text(s.title,sizeof(s.title),title);copy_text(s.artist,sizeof(s.artist),artist);
    copy_text(s.album,sizeof(s.album),album);s.playing=p;s.volume=v;
    s.volume_min=min;s.volume_max=max;s.seek_position=std::max(0,pos);
    s.seek_length=std::max(0,length);s.dirty=true;
    if (title[0]) {
        copy_text(s.track_identity_title,sizeof(s.track_identity_title),title);
        copy_text(s.track_identity_artist,sizeof(s.track_identity_artist),artist);
        s.track_seen=true;
    }
    s.playback_seen=true;
    if(playback_changed&&!changed)flash_action(p?"PLAY":"PAUSE");
    if (changed && p && s.online) {
#if HIPHI_M5_TARGET_ID == 4
        s.ambient_face_variant=static_cast<uint8_t>(esp_random());
        s.controls_mode=false;s.controls_until=0;
        s.track_reveal_started=esp_timer_get_time();
        s.track_reveal_until=s.track_reveal_started+7000000;
#endif
        m5_platform_stackchan_sound_trigger(
            M5_PLATFORM_STACKCHAN_SOUND_NEW_TRACK);
        const bool dance_started = s.body_enabled &&
            m5_platform_stackchan_expression_trigger(
                M5_PLATFORM_STACKCHAN_DANCE);
        ESP_LOGI(TAG, "Kizz new-track dance: enabled=%d accepted=%d",
                 s.body_enabled, dance_started);
    }
}
extern "C" void touch_ui_show_zone_picker(const char **n,const char **i,int count,int selected){s.zone_count=std::min(count,18);s.zone_selected=std::max(0,std::min(selected,s.zone_count-1));s.zone_current=s.zone_selected;for(int x=0;x<s.zone_count;x++){copy_text(s.zone_names[x],64,n[x]);copy_text(s.zone_ids[x],64,i[x]);}s.picker=true;s.settings=false;s.dirty=true;}
extern "C" void touch_ui_hide_zone_picker(void){s.picker=false;s.dirty=true;}
extern "C" bool touch_ui_is_zone_picker_visible(void){return s.picker;}
extern "C" void touch_ui_zone_picker_scroll(int d){s.zone_selected=std::max(0,std::min(s.zone_count-1,s.zone_selected+(d>0?1:-1)));s.dirty=true;}
extern "C" void touch_ui_zone_picker_get_selected_id(char *o,size_t l){if(o&&l&&s.zone_selected>=0&&s.zone_selected<s.zone_count)copy_text(o,l,s.zone_ids[s.zone_selected]);}
extern "C" bool touch_ui_zone_picker_is_current_selection(void){return s.zone_selected==s.zone_current;}
extern "C" void touch_ui_update_battery(void){int level=m5_platform_battery_level();if(level!=s.battery){s.battery=level;s.dirty=true;}}
extern "C" void touch_ui_apply_display_config(const rk_cfg_t *cfg,bool charging){
    if (!cfg) return;
    s.configured_dim_timeout_sec = rk_cfg_get_dim_timeout(cfg, charging);
    s.configured_sleep_timeout_sec = rk_cfg_get_sleep_timeout(cfg, charging);
    s.configured_power_off_timeout_sec =
        rk_cfg_get_deep_sleep_timeout(cfg, charging);
    s.dim_timeout_sec = s.voice_diagnostics ? 0 : s.configured_dim_timeout_sec;
    s.sleep_timeout_sec = s.voice_diagnostics ? 0 : s.configured_sleep_timeout_sec;
    s.power_off_timeout_sec = s.voice_diagnostics
        ? 0 : s.configured_power_off_timeout_sec;
    if ((s.sleeping && s.sleep_timeout_sec == 0) ||
        (s.dimmed && !s.sleeping && s.dim_timeout_sec == 0)) {
        wake_display();
    } else {
        s.last_activity_us = esp_timer_get_time();
    }
#if HIPHI_M5_TARGET_ID == 4
    s.configured_art_timeout_sec = rk_cfg_get_art_mode_timeout(cfg, charging);
    s.art_timeout_sec = s.voice_diagnostics ? 0 : s.configured_art_timeout_sec;
    ESP_LOGI(TAG, "Kizz Art mode policy: timeout=%us charging=%s",
             static_cast<unsigned>(s.art_timeout_sec),
             charging ? "yes" : "no");
#endif
    ESP_LOGI(TAG,
             "%s power policy: dim=%us sleep=%us power-off=%us charging=%s",
             m5_platform_board_name(),
             static_cast<unsigned>(s.dim_timeout_sec),
             static_cast<unsigned>(s.sleep_timeout_sec),
             static_cast<unsigned>(s.power_off_timeout_sec),
             charging ? "yes" : "no");
}
extern "C" bool touch_ui_is_display_sleeping(void){return s.sleeping;}
extern "C" void touch_ui_show_settings(void){s.settings=true;s.picker=false;s.art_mode=false;s.dirty=true;}
extern "C" bool touch_ui_stackchan_body_preference(void) {
#if HIPHI_M5_TARGET_ID == 4
    return load_body_enabled(nullptr);
#else
    return false;
#endif
}
extern "C" bool touch_ui_stackchan_sound_preference(void) {
#if HIPHI_M5_TARGET_ID == 4
    return load_sound_enabled(nullptr);
#else
    return false;
#endif
}
extern "C" uint8_t touch_ui_stackchan_voice_volume_preference(void) {
#if HIPHI_M5_TARGET_ID == 4
    return static_cast<uint8_t>(load_voice_volume(nullptr));
#else
    return 0;
#endif
}
extern "C" bool touch_ui_post_stackchan_preferences(bool body_enabled,
                                                      bool sound_enabled,
                                                      uint8_t voice_volume) {
#if HIPHI_M5_TARGET_ID == 4
    if (voice_volume > M5_PLATFORM_STACKCHAN_VOLUME_HIGH) return false;
    struct PreferenceRequest { bool body; bool sound; uint8_t volume; };
    auto *request = static_cast<PreferenceRequest *>(
        calloc(1, sizeof(PreferenceRequest)));
    if (!request) return false;
    request->body = body_enabled;
    request->sound = sound_enabled;
    request->volume = voice_volume;
    if (!platform_task_post_to_ui([](void *arg) {
            auto *preference = static_cast<PreferenceRequest *>(arg);
            set_body_language(preference->body, false);
            set_sounds(preference->sound, false, false);
            set_voice_volume(static_cast<m5_platform_stackchan_volume_t>(
                                 preference->volume),
                             preference->sound, false);
            body_notice("PERSONALITY UPDATED");
            free(preference);
        }, request)) {
        free(request);
        return false;
    }
    /* The HTTP handler redirects immediately after queueing this work. Commit
     * the user's intent here so the redirected Settings page never flashes the
     * previous values while the UI task safely applies hardware changes. */
    save_body_enabled(body_enabled);
    save_sound_enabled(sound_enabled);
    save_voice_volume(static_cast<m5_platform_stackchan_volume_t>(voice_volume));
    return true;
#else
    (void)body_enabled;
    (void)sound_enabled;
    (void)voice_volume;
    return false;
#endif
}
