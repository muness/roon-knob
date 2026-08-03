#include "rlcd_ui.h"

#include "rlcd_display.h"
#include "rlcd_art_dither.h"
#include "rlcd_text.h"
#include "bridge_client.h"
#include "platform/platform_http.h"
#include "rk_cfg.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#define RLCD_REFRESH_DEBOUNCE_MS 300
#define RLCD_PICKER_MAX_ZONES 18
#define RLCD_CONTENT_WIDTH (RLCD_WIDTH - 28)
#define RLCD_CONTENT_HEIGHT (RLCD_HEIGHT - 28)
#define RLCD_ART_SOURCE_SIZE 400
#define RLCD_ART_RENDER_WIDTH 340
#define RLCD_ART_CROP_HEIGHT 166
#define RLCD_ART_CROP_TOP 0
#define RLCD_ART_CROP_LEFT 0
/* Content coordinates: these line up with the KEY and BOOT hardware columns. */
#define RLCD_KEY_CONTROL_X 130
#define RLCD_BOOT_CONTROL_X 246
#define RLCD_TOP_CONTROL_Y -10

static const char *TAG = "rlcd_ui";

typedef struct {
    char zone[64];
    char track[128];
    char artist[128];
    char album[128];
    char message[128];
    char network[128];
    float volume;
    float volume_min;
    float volume_max;
    int seek_position;
    int length;
    bool playing;
    bool online;
    bool ble_connected;
    bool setup_mode;
    bool usage_key_visible;
    char image_key[128];
    bool picker_visible;
    int picker_count;
    int picker_selected;
    int picker_current;
    char picker_names[RLCD_PICKER_MAX_ZONES][64];
    char picker_ids[RLCD_PICKER_MAX_ZONES][64];
    bool dirty;
    uint64_t last_change_ms;
} rlcd_view_t;

static rlcd_view_t s_view;
static lv_obj_t *s_track;
static lv_obj_t *s_artist;
static lv_obj_t *s_status;
static lv_obj_t *s_volume;
static lv_obj_t *s_picker_controls;
static lv_obj_t *s_picker_position;
static lv_obj_t *s_play_control;
static lv_obj_t *s_artwork;
static lv_obj_t *s_volume_bar;
static lv_obj_t *s_seek_bar;
static uint8_t *s_draw_buffer;
static lv_display_t *s_display;
static lv_obj_t *s_screen;
static uint8_t *s_artwork_data;
static uint8_t *s_artwork_mono;
static lv_image_dsc_t s_artwork_dsc;
static lv_image_dsc_t s_artwork_full_dsc;
static bool s_art_mode;
static bool s_art_mode_rendered;
/* A controller full refresh must be paired with a full LVGL repaint.  The
 * panel's framebuffer can otherwise contain a stale/partially rendered image
 * even though the ST7305 is about to transmit all of it. */
static bool s_full_redraw_pending;
static bool s_key_track_mode;
static uint32_t s_art_mode_timeout_seconds = RK_DEFAULT_ART_MODE_BATTERY_TIMEOUT_SEC;
static uint64_t s_art_mode_deadline_ms;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000ULL; }

static void copy_text(char *out, size_t length, const char *in) {
    rlcd_text_normalize(out, length, in);
}

static bool set_text_if_changed(char *out, size_t length, const char *in) {
    char normalized[128];
    if (length > sizeof(normalized)) return false;
    rlcd_text_normalize(normalized, length, in);
    if (strncmp(out, normalized, length) == 0) return false;
    copy_text(out, length, normalized);
    return true;
}

static void mark_dirty(void) {
    s_view.dirty = true;
    s_view.last_change_ms = now_ms();
}

static void request_full_redraw(void) {
    rlcd_display_request_full_refresh();
    s_full_redraw_pending = true;
}

static void hide_meters(void) {
    lv_obj_add_flag(s_volume_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_seek_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_play_control, LV_OBJ_FLAG_HIDDEN);
}

static int bounded_percent(float value, float minimum, float maximum) {
    if (maximum <= minimum) return 0;
    int percent = (int)(((value - minimum) * 100.0f) / (maximum - minimum));
    if (percent < 0) return 0;
    if (percent > 100) return 100;
    return percent;
}

static bool is_routine_message(const char *message) {
    static const char prefix[] = "Hi-Fi Control:";
    return message && strncmp(message, prefix, strlen(prefix)) == 0;
}

static bool render_artwork_dither(void) {
    const size_t expected = RLCD_ART_SOURCE_SIZE * RLCD_ART_SOURCE_SIZE * 2;
    if (!s_artwork_data) return false;
    if (!s_artwork_mono) {
        s_artwork_mono = heap_caps_malloc(expected,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_artwork_mono) {
        ESP_LOGE(TAG, "Could not allocate monochrome artwork cache");
        return false;
    }
    uint64_t started = now_ms();
    if (!rlcd_art_dither_rgb565(s_artwork_data, s_artwork_mono,
                                RLCD_ART_SOURCE_SIZE, RLCD_ART_SOURCE_SIZE,
                                RLCD_ART_DITHER_ATKINSON)) {
        ESP_LOGE(TAG, "Atkinson artwork dither failed");
        return false;
    }
    ESP_LOGI(TAG, "Rendered Atkinson artwork in %llu ms",
             (unsigned long long)(now_ms() - started));
    s_artwork_dsc.data = s_artwork_mono +
        ((size_t)RLCD_ART_CROP_TOP * RLCD_ART_SOURCE_SIZE + RLCD_ART_CROP_LEFT) * 2;
    s_artwork_dsc.header.w = RLCD_ART_RENDER_WIDTH;
    s_artwork_dsc.header.h = RLCD_ART_CROP_HEIGHT;
    s_artwork_dsc.data_size =
        (size_t)RLCD_ART_SOURCE_SIZE * RLCD_ART_CROP_HEIGHT * 2;
    s_artwork_full_dsc = s_artwork_dsc;
    s_artwork_full_dsc.header.w = RLCD_ART_SOURCE_SIZE;
    s_artwork_full_dsc.header.h = RLCD_HEIGHT;
    s_artwork_full_dsc.data = s_artwork_mono;
    s_artwork_full_dsc.data_size =
        (size_t)RLCD_ART_SOURCE_SIZE * RLCD_HEIGHT * 2;
    if (s_artwork) lv_obj_invalidate(s_artwork);
    return true;
}

static bool load_artwork(void) {
    char url[256];
    char *image = NULL;
    size_t image_len = 0;
    const size_t expected = RLCD_ART_SOURCE_SIZE * RLCD_ART_SOURCE_SIZE * 2;
    const char *art_url = bridge_client_get_artwork_url_for_format(
        url, sizeof(url), RLCD_ART_SOURCE_SIZE, RLCD_ART_SOURCE_SIZE, 0, "rgb565");
    if (!art_url || platform_http_get_image(art_url, &image, &image_len) != 0 ||
        !image || image_len != expected) {
        platform_http_free(image);
        ESP_LOGW(TAG, "Artwork unavailable (%u bytes, expected %u)",
                 (unsigned)image_len, (unsigned)expected);
        return false;
    }
    if (!s_artwork_data) {
        s_artwork_data = heap_caps_malloc(expected, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_artwork_data) {
        platform_http_free(image);
        ESP_LOGE(TAG, "Could not allocate artwork cache");
        return false;
    }
    memcpy(s_artwork_data, image, expected);
    platform_http_free(image);
    s_artwork_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_artwork_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_artwork_dsc.header.flags = 0;
    s_artwork_dsc.header.w = RLCD_ART_RENDER_WIDTH;
    s_artwork_dsc.header.h = RLCD_ART_CROP_HEIGHT;
    s_artwork_dsc.header.stride = RLCD_ART_SOURCE_SIZE * 2;
    s_artwork_dsc.data_size = expected;
    s_artwork_dsc.data = s_artwork_data;
    if (!render_artwork_dither()) {
        s_artwork_dsc.data = s_artwork_data +
            ((size_t)RLCD_ART_CROP_TOP * RLCD_ART_SOURCE_SIZE + RLCD_ART_CROP_LEFT) * 2;
        s_artwork_full_dsc = s_artwork_dsc;
        s_artwork_full_dsc.header.w = RLCD_ART_SOURCE_SIZE;
        s_artwork_full_dsc.header.h = RLCD_HEIGHT;
        s_artwork_full_dsc.data = s_artwork_data;
        s_artwork_full_dsc.data_size =
            (size_t)RLCD_ART_SOURCE_SIZE * RLCD_HEIGHT * 2;
    }
    lv_image_set_src(s_artwork, &s_artwork_dsc);
    return true;
}

static void flush(lv_display_t *display, const lv_area_t *area,
                  uint8_t *pixels) {
    const uint16_t *source = (const uint16_t *)pixels;
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            rlcd_display_set_rgb565((uint16_t)x, (uint16_t)y, *source++);
        }
    }
    lv_display_flush_ready(display);
}

static void apply_view(void) {
    char status[256];
    char volume[64];
    char secondary[260];
    if (s_view.usage_key_visible) {
        lv_obj_add_flag(s_picker_controls, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_picker_position, LV_OBJ_FLAG_HIDDEN);
        hide_meters();
        lv_obj_add_flag(s_artwork, LV_OBJ_FLAG_HIDDEN);
        /* Shift the physical-label strip to match the board's center PWR key. */
        lv_obj_set_pos(s_track, 18, 8);
        lv_obj_set_width(s_track, 372);
        lv_obj_set_height(s_track, LV_SIZE_CONTENT);
        lv_label_set_long_mode(s_track, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(s_track, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_artist, 0, 50);
        lv_obj_set_width(s_artist, 372);
        lv_obj_set_height(s_artist, LV_SIZE_CONTENT);
        lv_label_set_long_mode(s_artist, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(s_artist, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(s_volume, 0, 122);
        lv_obj_set_style_text_align(s_volume, LV_TEXT_ALIGN_LEFT, 0);
        copy_text(status, sizeof(status), "Press KEY to continue");
        volume[0] = '\0';
        lv_label_set_text(s_track, "KEY | PWR | BOOT");
        lv_label_set_text(s_artist,
                          "KEY: single = volume down\n"
                          "KEY: double click = volume up\n"
                          "KEY: long press = toggle volume / track mode\n"
                          "PWR: on / off\n"
                          "BOOT: single = play / pause\n"
                          "BOOT: double click = choose zone\n"
                          "BOOT: long press = album art");
    } else if (s_art_mode && s_artwork_dsc.data) {
        if (s_art_mode_rendered) {
            return;
        }
        lv_obj_add_flag(s_picker_controls, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_picker_position, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_artist, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_volume, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_volume_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_seek_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_play_control, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_artwork, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_pad_all(s_screen, 0, 0);
        lv_obj_set_pos(s_artwork, -2, -2);
        lv_image_set_src(s_artwork, &s_artwork_full_dsc);
        s_art_mode_rendered = true;
    } else if (s_view.picker_visible) {
        lv_obj_set_style_pad_all(s_screen, 14, 0);
        char current[80];
        lv_obj_remove_flag(s_picker_controls, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_picker_position, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_picker_controls, 18, 4);
        lv_obj_set_width(s_picker_controls, 372);
        lv_obj_set_height(s_picker_controls, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(s_picker_controls, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_picker_controls, "KEY | PWR | BOOT");
        /* Keep the gesture legend adjacent to the physical button labels. */
        lv_obj_set_pos(s_volume, 0, 32);
        lv_obj_set_width(s_volume, RLCD_CONTENT_WIDTH);
        lv_obj_set_height(s_volume, 48);
        lv_obj_set_style_text_font(s_volume, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_align(s_volume, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(s_picker_position, 0, 84);
        lv_obj_set_width(s_picker_position, RLCD_CONTENT_WIDTH);
        lv_obj_set_height(s_picker_position, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(s_picker_position, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(s_picker_position, LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_align(s_track, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(s_track, 0, 112);
        lv_obj_set_width(s_track, RLCD_CONTENT_WIDTH);
        lv_obj_set_height(s_track, 52);
        lv_label_set_long_mode(s_track, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(s_artist, 0, 178);
        lv_obj_set_width(s_artist, RLCD_CONTENT_WIDTH);
        lv_obj_set_height(s_artist, LV_SIZE_CONTENT);
        lv_label_set_long_mode(s_artist, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(s_artist, &lv_font_montserrat_20, 0);
        lv_obj_set_pos(s_volume, 0, 32);
        lv_obj_set_width(s_volume, RLCD_CONTENT_WIDTH);
        lv_obj_set_height(s_volume, 48);
        lv_label_set_long_mode(s_volume, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(s_volume, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_font(s_volume, &lv_font_montserrat_20, 0);
        hide_meters();
        lv_obj_add_flag(s_artwork, LV_OBJ_FLAG_HIDDEN);
        const char *selected = s_view.picker_count > 0
                                   ? s_view.picker_names[s_view.picker_selected]
                                   : "No zones available";
        if (s_view.picker_count > 0) {
            snprintf(status, sizeof(status), "Zone %d of %d",
                     s_view.picker_selected + 1, s_view.picker_count);
            lv_label_set_text(s_picker_position, status);
            if (s_view.picker_selected == s_view.picker_current) {
                copy_text(current, sizeof(current), "Current zone");
            } else {
                snprintf(current, sizeof(current), "Current: %s",
                         s_view.picker_names[s_view.picker_current]);
            }
            copy_text(volume, sizeof(volume),
                      "KEY: next | double: previous\n"
                      "BOOT: cancel | double: select");
            lv_label_set_text(s_track, selected);
            lv_label_set_text(s_artist, current);
        } else {
            copy_text(status, sizeof(status), "No zones available");
            lv_label_set_text(s_picker_position, status);
            copy_text(current, sizeof(current), "No zone can be selected");
            copy_text(volume, sizeof(volume), "BOOT: cancel");
            lv_label_set_text(s_track, selected);
            lv_label_set_text(s_artist, current);
        }
    } else if (s_view.setup_mode) {
        lv_obj_set_style_pad_all(s_screen, 14, 0);
        lv_obj_add_flag(s_picker_controls, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_picker_position, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_status, LV_ALIGN_BOTTOM_LEFT, 0, -2);
        lv_obj_set_style_text_align(s_track, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(s_track, 0, 42);
        lv_obj_set_height(s_track, LV_SIZE_CONTENT);
        lv_label_set_long_mode(s_track, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(s_artist, 0, 130);
        lv_obj_set_height(s_artist, LV_SIZE_CONTENT);
        lv_label_set_long_mode(s_artist, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(s_artist, &lv_font_montserrat_20, 0);
        hide_meters();
        lv_obj_add_flag(s_artwork, LV_OBJ_FLAG_HIDDEN);
        copy_text(status, sizeof(status), "Connect  •  Configure  •  Listen");
        copy_text(volume, sizeof(volume), "");
        lv_label_set_text(s_track, "Connect to\nhiphi-rlcd-setup");
        lv_label_set_text(s_artist, "Then open 192.168.4.1");
    } else {
        lv_obj_set_style_pad_all(s_screen, 14, 0);
        lv_obj_add_flag(s_picker_controls, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_picker_position, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_artist, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_status, LV_ALIGN_BOTTOM_LEFT, 0, -2);
        lv_obj_set_style_text_align(s_track, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_font(s_artist, &lv_font_montserrat_20, 0);
        bool show_controls = !s_view.playing &&
            (!s_view.track[0] || strcmp(s_view.track, "Nothing playing") == 0);
        bool show_artwork = s_view.image_key[0] && s_artwork_dsc.data;
        lv_obj_remove_flag(s_volume, LV_OBJ_FLAG_HIDDEN);
        if (show_artwork) {
            lv_obj_remove_flag(s_artwork, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_artwork,
                           (RLCD_CONTENT_WIDTH - RLCD_ART_RENDER_WIDTH) / 2, 34);
            lv_image_set_src(s_artwork, &s_artwork_dsc);
            lv_obj_set_pos(s_track, 0, 208);
            lv_obj_set_width(s_track, RLCD_CONTENT_WIDTH);
            lv_obj_set_height(s_track, 28);
            lv_label_set_long_mode(s_track, LV_LABEL_LONG_DOT);
            lv_obj_set_pos(s_artist, 0, 240);
            lv_obj_set_width(s_artist, RLCD_CONTENT_WIDTH);
            lv_obj_set_height(s_artist, 28);
            lv_label_set_long_mode(s_artist, LV_LABEL_LONG_DOT);
            lv_obj_set_style_bg_color(s_track, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(s_track, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(s_artist, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(s_artist, LV_OPA_COVER, 0);
        } else {
            lv_obj_add_flag(s_artwork, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_track, 0, 42);
            lv_obj_set_width(s_track, RLCD_CONTENT_WIDTH);
            lv_obj_set_height(s_track, LV_SIZE_CONTENT);
            lv_label_set_long_mode(s_track, LV_LABEL_LONG_WRAP);
            lv_obj_set_pos(s_artist, 0, 130);
            lv_obj_set_width(s_artist, RLCD_CONTENT_WIDTH);
            lv_obj_set_height(s_artist, LV_SIZE_CONTENT);
            lv_label_set_long_mode(s_artist, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_bg_opa(s_track, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_opa(s_artist, LV_OPA_TRANSP, 0);
        }
        (void)show_controls;
        if (s_key_track_mode) {
            snprintf(volume, sizeof(volume), "%s / %s",
                     LV_SYMBOL_PREV, LV_SYMBOL_NEXT);
        } else {
            copy_text(volume, sizeof(volume), "- / +");
        }
        /* These follow the physical KEY / BOOT columns above the panel. */
        lv_obj_set_pos(s_volume, RLCD_KEY_CONTROL_X, RLCD_TOP_CONTROL_Y);
        lv_obj_set_pos(s_play_control, RLCD_BOOT_CONTROL_X, RLCD_TOP_CONTROL_Y);
        lv_obj_remove_flag(s_play_control, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_play_control,
                          s_view.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        lv_obj_remove_flag(s_volume_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_volume_bar, bounded_percent(
            s_view.volume, s_view.volume_min, s_view.volume_max), LV_ANIM_OFF);
        if (s_view.length > 0 && s_view.seek_position >= 0) {
            lv_obj_remove_flag(s_seek_bar, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(s_seek_bar, bounded_percent(
                (float)s_view.seek_position, 0.0f, (float)s_view.length), LV_ANIM_OFF);
        } else {
            lv_obj_add_flag(s_seek_bar, LV_OBJ_FLAG_HIDDEN);
        }
        status[0] = '\0';
        lv_label_set_text(s_track, s_view.track[0] ? s_view.track : "Nothing playing");
        if (show_controls) {
            lv_label_set_text(s_artist,
                              "BOOT: play/pause\nBOOT double click: choose zone");
        } else if (!s_view.online) {
            lv_label_set_text(s_artist, "Offline");
        } else if (!s_view.track[0] && s_view.message[0] &&
                   !is_routine_message(s_view.message)) {
            lv_label_set_text(s_artist, s_view.message);
        } else {
            if (s_view.artist[0] && s_view.album[0]) {
                snprintf(secondary, sizeof(secondary), "%s / %s",
                         s_view.artist, s_view.album);
            } else {
                snprintf(secondary, sizeof(secondary), "%s",
                         s_view.artist[0] ? s_view.artist : s_view.album);
            }
            lv_label_set_text(s_artist, secondary);
        }
    }
    if (!s_art_mode) {
        lv_label_set_text(s_status, status);
        lv_label_set_text(s_volume, volume);
    }
    lv_obj_set_style_text_color(s_track, lv_color_black(), 0);
}

void rlcd_ui_init(void) {
    lv_init();
    s_display = lv_display_create(RLCD_WIDTH, RLCD_HEIGHT);
    s_draw_buffer = lv_malloc(RLCD_WIDTH * 20 * sizeof(lv_color_t));
    lv_display_set_buffers(s_display, s_draw_buffer, NULL,
                           RLCD_WIDTH * 20 * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, flush);

    lv_obj_t *screen = lv_screen_active();
    s_screen = screen;
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);
    lv_obj_set_style_pad_all(screen, 14, 0);
    s_view.usage_key_visible = true;
    s_artwork = lv_image_create(screen);
    lv_obj_set_pos(s_artwork, 0, 0);
    lv_obj_add_flag(s_artwork, LV_OBJ_FLAG_HIDDEN);
    s_track = lv_label_create(screen);
    lv_obj_set_width(s_track, RLCD_CONTENT_WIDTH);
    lv_label_set_long_mode(s_track, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_track, &lv_font_montserrat_20, 0);
    lv_obj_align(s_track, LV_ALIGN_TOP_LEFT, 0, 8);
    s_artist = lv_label_create(screen);
    lv_obj_set_width(s_artist, RLCD_CONTENT_WIDTH);
    lv_label_set_long_mode(s_artist, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_artist, &lv_font_montserrat_20, 0);
    lv_obj_align(s_artist, LV_ALIGN_TOP_LEFT, 0, 95);
    s_volume = lv_label_create(screen);
    lv_obj_set_style_text_font(s_volume, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_volume, RLCD_KEY_CONTROL_X, RLCD_TOP_CONTROL_Y);
    s_picker_controls = lv_label_create(screen);
    lv_obj_set_style_text_font(s_picker_controls, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(s_picker_controls, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_picker_controls, LV_OBJ_FLAG_HIDDEN);
    s_picker_position = lv_label_create(screen);
    lv_obj_set_style_text_font(s_picker_position, &lv_font_montserrat_20, 0);
    lv_obj_add_flag(s_picker_position, LV_OBJ_FLAG_HIDDEN);
    s_play_control = lv_label_create(screen);
    lv_obj_set_style_text_font(s_play_control, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_play_control, RLCD_BOOT_CONTROL_X, RLCD_TOP_CONTROL_Y);
    lv_obj_add_flag(s_play_control, LV_OBJ_FLAG_HIDDEN);
    s_volume_bar = lv_bar_create(screen);
    lv_obj_set_pos(s_volume_bar, 0, 16);
    lv_obj_set_size(s_volume_bar, RLCD_CONTENT_WIDTH, 6);
    lv_bar_set_range(s_volume_bar, 0, 100);
    lv_obj_set_style_radius(s_volume_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_volume_bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_volume_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_volume_bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_volume_bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_volume_bar, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_add_flag(s_volume_bar, LV_OBJ_FLAG_HIDDEN);
    s_seek_bar = lv_bar_create(screen);
    lv_obj_set_pos(s_seek_bar, 0, RLCD_CONTENT_HEIGHT - 6);
    lv_obj_set_size(s_seek_bar, RLCD_CONTENT_WIDTH, 6);
    lv_bar_set_range(s_seek_bar, 0, 100);
    lv_obj_set_style_radius(s_seek_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_seek_bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_seek_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_seek_bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_seek_bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_seek_bar, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_add_flag(s_seek_bar, LV_OBJ_FLAG_HIDDEN);
    s_status = lv_label_create(screen);
    lv_obj_set_width(s_status, RLCD_CONTENT_WIDTH);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_20, 0);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_LEFT, 0, -2);
    mark_dirty();
}

void rlcd_ui_process(void) {
    if (!s_art_mode && !s_view.usage_key_visible && !s_view.picker_visible &&
        !s_view.setup_mode && s_art_mode_timeout_seconds > 0 &&
        s_view.image_key[0] && s_artwork_dsc.data &&
        now_ms() >= s_art_mode_deadline_ms) {
        s_art_mode = true;
        s_art_mode_rendered = false;
        request_full_redraw();
        s_view.dirty = true;
    }
    if (!s_view.dirty) return;
    if (s_art_mode && s_art_mode_rendered) {
        /* Metadata/seek polling must not redraw or transmit a stable art frame. */
        s_view.dirty = false;
        return;
    }
    if (!s_view.picker_visible &&
        now_ms() - s_view.last_change_ms < RLCD_REFRESH_DEBOUNCE_MS) return;
    s_view.dirty = false;
    bool full_redraw = s_full_redraw_pending;
    if (full_redraw) {
        lv_obj_invalidate(s_screen);
    }
    apply_view();
    /* This panel has no animations or touch input.  Do not run LVGL's normal
     * periodic render loop: it can invalidate and reflush autonomously.  A
     * state transition explicitly renders once, then the ST7305 is left
     * undisturbed until the next visible transition. */
    lv_refr_now(s_display);
    if (full_redraw) {
        s_full_redraw_pending = false;
    }
    rlcd_display_refresh();
}

void rlcd_ui_set_status(bool online) {
    if (s_view.online != online) { s_view.online = online; mark_dirty(); }
}
void rlcd_ui_set_art_mode_timeout(uint32_t timeout_seconds) {
    if (timeout_seconds > 86400U) timeout_seconds = 86400U;
    s_art_mode_timeout_seconds = timeout_seconds;
    s_art_mode_deadline_ms = timeout_seconds ?
        now_ms() + (uint64_t)timeout_seconds * 1000ULL : 0;
    if (!timeout_seconds && s_art_mode) {
        s_art_mode = false;
        request_full_redraw();
    }
}
bool rlcd_ui_handle_activity(void) {
    if (!s_art_mode) {
        if (s_art_mode_timeout_seconds && !s_view.usage_key_visible &&
            !s_view.picker_visible && !s_view.setup_mode) {
            s_art_mode_deadline_ms = now_ms() +
                (uint64_t)s_art_mode_timeout_seconds * 1000ULL;
        }
        return false;
    }
    s_art_mode = false;
    s_art_mode_rendered = false;
    s_art_mode_deadline_ms = s_art_mode_timeout_seconds ?
        now_ms() + (uint64_t)s_art_mode_timeout_seconds * 1000ULL : 0;
    request_full_redraw();
    mark_dirty();
    return true;
}
void rlcd_ui_set_key_track_mode(bool track_mode) {
    if (s_key_track_mode == track_mode) return;
    s_key_track_mode = track_mode;
    mark_dirty();
}
bool rlcd_ui_enter_art_mode(void) {
    if (s_art_mode || !s_view.image_key[0] || !s_artwork_dsc.data ||
        s_view.usage_key_visible || s_view.picker_visible || s_view.setup_mode) {
        return false;
    }
    s_art_mode = true;
    s_art_mode_rendered = false;
    request_full_redraw();
    mark_dirty();
    return true;
}
void rlcd_ui_set_message(const char *value) {
    if (set_text_if_changed(s_view.message, sizeof(s_view.message), value)) mark_dirty();
}
void rlcd_ui_set_zone_name(const char *value) {
    if (set_text_if_changed(s_view.zone, sizeof(s_view.zone), value)) mark_dirty();
}
void rlcd_ui_set_network_status(const char *value) {
    if (set_text_if_changed(s_view.network, sizeof(s_view.network), value)) mark_dirty();
}
void rlcd_ui_set_setup_mode(bool enabled) {
    if (s_view.setup_mode != enabled) {
        s_view.setup_mode = enabled;
        request_full_redraw();
        mark_dirty();
    }
}
void rlcd_ui_set_artwork(const char *value) {
    const char *key = value ? value : "";
    if (!set_text_if_changed(s_view.image_key, sizeof(s_view.image_key), key)) return;
    if (key[0] && !load_artwork()) {
        s_artwork_dsc.data = NULL;
    } else if (!key[0]) {
        s_artwork_dsc.data = NULL;
    }
    request_full_redraw();
    if (key[0] && s_art_mode_timeout_seconds) {
        s_art_mode_deadline_ms = now_ms() +
            (uint64_t)s_art_mode_timeout_seconds * 1000ULL;
    }
    s_art_mode_rendered = false;
    mark_dirty();
}

bool rlcd_ui_dismiss_usage_key(void) {
    if (!s_view.usage_key_visible) return false;
    s_view.usage_key_visible = false;
    request_full_redraw();
    mark_dirty();
    return true;
}

void rlcd_ui_show_volume_change(float volume, float volume_step) {
    (void)volume_step;
    if (s_view.volume != volume) { s_view.volume = volume; mark_dirty(); }
}
void rlcd_ui_set_ble_status(bool connected) {
    if (s_view.ble_connected != connected) { s_view.ble_connected = connected; mark_dirty(); }
}
void rlcd_ui_update_battery(void) { }

void rlcd_ui_show_zone_picker(const char **names, const char **ids,
                              int count, int selected) {
    if (count < 0) count = 0;
    if (count > RLCD_PICKER_MAX_ZONES) count = RLCD_PICKER_MAX_ZONES;
    s_view.picker_count = count;
    s_view.picker_selected = (selected >= 0 && selected < count) ? selected : 0;
    s_view.picker_current = s_view.picker_selected;
    for (int i = 0; i < count; ++i) {
        copy_text(s_view.picker_names[i], sizeof(s_view.picker_names[i]),
                  names && names[i] ? names[i] : "Unnamed zone");
        copy_text(s_view.picker_ids[i], sizeof(s_view.picker_ids[i]),
                  ids && ids[i] ? ids[i] : "");
    }
    s_view.picker_visible = true;
    request_full_redraw();
    mark_dirty();
}

void rlcd_ui_hide_zone_picker(void) {
    if (s_view.picker_visible) {
        s_view.picker_visible = false;
        request_full_redraw();
        mark_dirty();
    }
}

bool rlcd_ui_is_zone_picker_visible(void) { return s_view.picker_visible; }

void rlcd_ui_zone_picker_scroll(int delta) {
    if (!s_view.picker_visible || s_view.picker_count == 0 || delta == 0) return;
    int next = s_view.picker_selected + (delta > 0 ? 1 : -1);
    if (next < 0) next = s_view.picker_count - 1;
    if (next >= s_view.picker_count) next = 0;
    if (next != s_view.picker_selected) {
        s_view.picker_selected = next;
        mark_dirty();
    }
}

bool rlcd_ui_zone_picker_is_current_selection(void) {
    return s_view.picker_visible &&
           s_view.picker_selected == s_view.picker_current;
}

void rlcd_ui_zone_picker_get_selected_id(char *out, size_t length) {
    if (!out || length == 0) return;
    if (!s_view.picker_visible || s_view.picker_selected < 0 ||
        s_view.picker_selected >= s_view.picker_count) {
        out[0] = '\0';
        return;
    }
    copy_text(out, length, s_view.picker_ids[s_view.picker_selected]);
}

void rlcd_ui_update(const char *line1, const char *line2, const char *line3,
                    bool playing, float volume, float volume_min,
                    float volume_max, float volume_step, int seek_position,
                    int length) {
    (void)volume_step;
    bool changed = set_text_if_changed(s_view.track, sizeof(s_view.track), line1);
    changed |= set_text_if_changed(s_view.artist, sizeof(s_view.artist), line2);
    changed |= set_text_if_changed(s_view.album, sizeof(s_view.album), line3);
    changed |= s_view.playing != playing;
    changed |= s_view.volume != volume;
    changed |= s_view.volume_min != volume_min;
    changed |= s_view.volume_max != volume_max;
    changed |= s_view.seek_position != seek_position;
    changed |= s_view.length != length;
    s_view.playing = playing;
    s_view.volume = volume;
    s_view.volume_min = volume_min;
    s_view.volume_max = volume_max;
    s_view.seek_position = seek_position;
    s_view.length = length;
    if (changed) {
        mark_dirty();
    }
}
