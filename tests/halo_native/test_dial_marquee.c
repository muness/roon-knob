// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "src/widgets/label/lv_label_private.h"

static lv_obj_t *s_setup_logo, *s_track_label, *s_artist_label;
static lv_obj_t *s_volume_label_large;
static bool s_controls_visible = true;
static const lv_font_t *font_normal(void) { return LV_FONT_DEFAULT; }
static const lv_font_t *font_small(void) { return &lv_font_montserrat_20; }
#define UI_TAG "test"
#define ESP_LOGE(...) assert(!"missing label")
#include "dial_metadata.h"

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

static void advance(unsigned ms)
{
    for(unsigned t = 0; t < ms; t += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
}

static int32_t offset(lv_obj_t *label)
{
    return ((lv_label_t *)label)->offset.x;
}

/* Continue the production metadata path while simulated playback updates arrive. */
static void poll_for(struct ui_state *state, unsigned ms)
{
    for(unsigned t = 0; t < ms; t += 100) {
        state->seek_position++;
        state->volume = (float)t;
        apply_state(state);
        advance(100);
    }
}

static void check_initial_pause(struct ui_state *state)
{
    apply_state(state);
    advance(50); /* Allow layout to create the animations. */
    assert(offset(s_track_label) == 0 && offset(s_artist_label) == 0);
    poll_for(state, 1100);
    assert(offset(s_track_label) == 0 && offset(s_artist_label) == 0);
    poll_for(state, 200);
    assert(offset(s_track_label) < 0 && offset(s_artist_label) < 0);
}

static void check_repeat_pause(struct ui_state *state, lv_obj_t *label)
{
    /* Reach the next inter-cycle pause, after the initial delay has elapsed. */
    unsigned elapsed;
    for(elapsed = 0; elapsed < 15000; elapsed += 100) {
        poll_for(state, 100);
        const lv_anim_t *animation = lv_anim_get(label, NULL);
        assert(animation);
        if(animation->act_time < 0) break;
    }
    assert(elapsed < 15000);
    const lv_anim_t *animation = lv_anim_get(label, NULL);
    assert(animation->repeat_delay == 2000);
    assert(animation->act_time <= -1900);
    int32_t held_offset = offset(label);
    for(unsigned t = 0; t < 1800; t += 100) {
        int32_t before = lv_anim_get(label, NULL)->act_time;
        poll_for(state, 100);
        assert(offset(label) == held_offset);
        assert(lv_anim_get(label, NULL)->act_time > before);
    }
    poll_for(state, 400);
    assert(offset(label) > held_offset); /* The next cycle is now moving. */
}

int main(void)
{
    lv_init();
    static _Alignas(8) uint8_t pool[72 * 1024];
    assert(lv_mem_add_pool(pool, sizeof(pool)));
    lv_display_t *display = lv_display_create(360, 360);
    static uint8_t draw[360 * 24 * 4];
    lv_display_set_buffers(display, draw, NULL, sizeof(draw), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    s_track_label = lv_label_create(lv_screen_active());
    s_artist_label = lv_label_create(lv_screen_active());
    s_volume_label_large = lv_label_create(lv_screen_active());
    lv_obj_set_width(s_track_label, 200);
    lv_obj_set_width(s_artist_label, 200);
    configure_marquee(s_track_label);
    configure_marquee(s_artist_label);
    struct ui_state state = {
        .online = true,
        .line1 = "A very long song title whose ending must be visible before the scroll repeats",
        .line2 = "An equally long artist and album description that also needs uninterrupted scrolling"
    };
    check_initial_pause(&state);

    /* Polls and unrelated state updates must neither jump nor restart either
     * marquee. Follow each label through a full cycle, including its ending. */
    lv_point_t title_size, artist_size;
    lv_text_get_size(&title_size, state.line1, font_normal(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&artist_size, state.line2, font_normal(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    bool title_end = false, artist_end = false, title_repeat = false, artist_repeat = false;
    for(unsigned poll = 0; poll < 1000 && !(title_repeat && artist_repeat); poll++) {
        int32_t title_before = offset(s_track_label);
        int32_t artist_before = offset(s_artist_label);
        state.volume = (float)poll;
        state.seek_position = (int)poll;
        apply_state(&state);
        assert(offset(s_track_label) == title_before);
        assert(offset(s_artist_label) == artist_before);
        advance(100);
        int32_t title_after = offset(s_track_label);
        int32_t artist_after = offset(s_artist_label);
        assert(title_after <= title_before || title_end);
        assert(artist_after <= artist_before || artist_end);
        if(-title_after >= title_size.x - lv_obj_get_width(s_track_label)) title_end = true;
        if(-artist_after >= artist_size.x - lv_obj_get_width(s_artist_label)) artist_end = true;
        if(title_end && title_after > title_before) title_repeat = true;
        if(artist_end && artist_after > artist_before) artist_repeat = true;
    }
    assert(title_repeat && artist_repeat);
    check_repeat_pause(&state, s_track_label);
    check_repeat_pause(&state, s_artist_label);

    /* A new phrase starts at its beginning, independently for each label. */
    advance(1000);
    strcpy(state.line1, "A different long song title starts at the first character after the track changes");
    apply_state(&state);
    assert(offset(s_track_label) == 0);
    poll_for(&state, 600);
    int32_t title_time = lv_anim_get(s_track_label, NULL)->act_time;
    strcpy(state.line2, "A changed artist description starts a new pause without restarting the title pause");
    apply_state(&state);
    assert(lv_anim_get(s_track_label, NULL)->act_time == title_time);
    advance(50);
    poll_for(&state, 700);
    assert(offset(s_track_label) < 0);
    assert(offset(s_artist_label) == 0);
    poll_for(&state, 600);
    assert(offset(s_artist_label) < 0);

    /* New phrases replace text immediately during a repeat pause, and receive
     * the initial delay rather than the remainder of the old repeat delay. */
    unsigned waited;
    for(waited = 0; waited < 15000; waited += 100) {
        poll_for(&state, 100);
        if(lv_anim_get(s_track_label, NULL)->act_time < 0) break;
    }
    assert(waited < 15000);
    strcpy(state.line1, "Another long title replaces the previous track while its marquee is resting");
    apply_state(&state);
    assert(strcmp(lv_label_get_text(s_track_label), state.line1) == 0);
    advance(50);
    poll_for(&state, 1100);
    assert(offset(s_track_label) == 0);
    poll_for(&state, 200);
    assert(offset(s_track_label) < 0);

    /* Recovery must re-enable scrolling even with identical metadata. */
    state.online = false;
    apply_state(&state);
    assert(lv_label_get_long_mode(s_track_label) == LV_LABEL_LONG_MODE_WRAP);
    state.online = true;
    apply_state(&state);
    assert(lv_label_get_long_mode(s_track_label) == LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    assert(lv_label_get_long_mode(s_artist_label) == LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    check_initial_pause(&state);

    strcpy(state.line1, "Short");
    strcpy(state.line2, "Artist");
    apply_state(&state);
    advance(1000);
    assert(offset(s_track_label) == 0 && offset(s_artist_label) == 0);
    assert(lv_anim_get(s_track_label, NULL) == NULL);
    assert(lv_anim_get(s_artist_label, NULL) == NULL);
    lv_deinit();
    puts("Dial marquee regression checks passed");
    return 0;
}
