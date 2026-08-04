#include "controller_command.h"
#include "controller_view.h"
#include "controller_view_compat.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARTWORK_TRACE_CAPACITY 16

typedef struct {
    int count;
    char line1[CONTROLLER_MEDIA_TEXT_CAPACITY];
    char line2[CONTROLLER_MEDIA_TEXT_CAPACITY];
    char line3[CONTROLLER_MEDIA_TEXT_CAPACITY];
    bool playing;
    float volume;
    float volume_min;
    float volume_max;
    float volume_step;
    int position;
    int duration;
} presentation_trace_t;

static presentation_trace_t s_presentation;
static char s_artwork_trace[ARTWORK_TRACE_CAPACITY]
                           [CONTROLLER_ARTWORK_REF_CAPACITY];
static int s_artwork_count;

static void copy_trace(char *destination, size_t capacity,
                       const char *source) {
    snprintf(destination, capacity, "%s", source ? source : "");
}

void controller_presentation_update(
    const char *line1,
    const char *line2,
    const char *line3,
    bool playing,
    float volume,
    float volume_min,
    float volume_max,
    float volume_step,
    int position,
    int duration) {
    ++s_presentation.count;
    copy_trace(s_presentation.line1, sizeof(s_presentation.line1), line1);
    copy_trace(s_presentation.line2, sizeof(s_presentation.line2), line2);
    copy_trace(s_presentation.line3, sizeof(s_presentation.line3), line3);
    s_presentation.playing = playing;
    s_presentation.volume = volume;
    s_presentation.volume_min = volume_min;
    s_presentation.volume_max = volume_max;
    s_presentation.volume_step = volume_step;
    s_presentation.position = position;
    s_presentation.duration = duration;
}

void controller_presentation_set_artwork(const char *image_key) {
    assert(s_artwork_count < ARTWORK_TRACE_CAPACITY);
    copy_trace(s_artwork_trace[s_artwork_count],
               sizeof(s_artwork_trace[s_artwork_count]), image_key);
    ++s_artwork_count;
}

static void reset_trace(void) {
    memset(&s_presentation, 0, sizeof(s_presentation));
    memset(s_artwork_trace, 0, sizeof(s_artwork_trace));
    s_artwork_count = 0;
}

static void fill_long_text(char *text, size_t length, char value) {
    assert(length > 0);
    memset(text, value, length - 1);
    text[length - 1] = '\0';
}

static void test_owned_bounded_values(void) {
    char primary[256];
    char artwork[256];
    fill_long_text(primary, sizeof(primary), 'p');
    fill_long_text(artwork, sizeof(artwork), 'a');

    controller_media_view_t media;
    controller_media_view_init(
        &media, primary, NULL, "tertiary", false,
        -FLT_MAX, FLT_MAX, -0.0f, FLT_MIN,
        INT32_MIN, INT32_MAX, artwork, UINT32_MAX);

    primary[0] = 'x';
    artwork[0] = 'x';
    assert(media.primary[0] == 'p');
    assert(media.artwork_ref[0] == 'a');
    assert(strlen(media.primary) == CONTROLLER_MEDIA_TEXT_CAPACITY - 1);
    assert(strlen(media.artwork_ref) == CONTROLLER_ARTWORK_REF_CAPACITY - 1);
    assert(media.primary[CONTROLLER_MEDIA_TEXT_CAPACITY - 1] == '\0');
    assert(media.secondary[0] == '\0');
    assert(strcmp(media.tertiary, "tertiary") == 0);
    assert(media.progress_position == INT32_MIN);
    assert(media.progress_duration == INT32_MAX);
    assert(media.artwork_generation == UINT32_MAX);

    char headline[192];
    fill_long_text(headline, sizeof(headline), 'h');
    controller_connectivity_view_t connectivity;
    controller_connectivity_view_init(&connectivity, headline, NULL);
    headline[0] = 'x';
    assert(connectivity.headline[0] == 'h');
    assert(strlen(connectivity.headline) ==
           CONTROLLER_CONNECTIVITY_TEXT_CAPACITY - 1);
    assert(connectivity.headline[
               CONTROLLER_CONNECTIVITY_TEXT_CAPACITY - 1] == '\0');
    assert(connectivity.detail[0] == '\0');

    controller_media_view_init(
        NULL, NULL, NULL, NULL, false,
        0.0f, 0.0f, 0.0f, 0.0f, 0, 0, NULL, 0);
    controller_connectivity_view_init(NULL, NULL, NULL);

    assert(sizeof(controller_media_view_t) <= 576);
    assert(sizeof(controller_connectivity_view_t) <= 192);
    assert(sizeof(controller_command_t) <= 8);
}

static controller_media_view_t media_with_artwork(
    const char *artwork_ref, uint32_t generation) {
    controller_media_view_t media;
    controller_media_view_init(
        &media, "primary", "secondary", "tertiary", true,
        -FLT_MAX, -FLT_MIN, FLT_MAX, FLT_MIN,
        INT32_MIN, INT32_MAX, artwork_ref, generation);
    return media;
}

static void test_media_forwarding_and_artwork(void) {
    reset_trace();
    controller_view_compat_reset();

    controller_media_view_t media = media_with_artwork("art-a", 7);
    controller_view_compat_apply_media(&media);
    assert(s_presentation.count == 1);
    assert(strcmp(s_presentation.line1, "primary") == 0);
    assert(strcmp(s_presentation.line2, "secondary") == 0);
    assert(strcmp(s_presentation.line3, "tertiary") == 0);
    assert(s_presentation.playing);
    assert(s_presentation.volume == -FLT_MAX);
    assert(s_presentation.volume_min == -FLT_MIN);
    assert(s_presentation.volume_max == FLT_MAX);
    assert(s_presentation.volume_step == FLT_MIN);
    assert(s_presentation.position == INT32_MIN);
    assert(s_presentation.duration == INT32_MAX);
    assert(s_artwork_count == 1);
    assert(strcmp(s_artwork_trace[0], "art-a") == 0);

    controller_view_compat_apply_media(&media);
    assert(s_artwork_count == 1);

    media.artwork_generation = 8;
    controller_view_compat_apply_media(&media);
    assert(s_artwork_count == 3);
    assert(strcmp(s_artwork_trace[1], "") == 0);
    assert(strcmp(s_artwork_trace[2], "art-a") == 0);

    snprintf(media.artwork_ref, sizeof(media.artwork_ref), "%s", "art-b");
    controller_view_compat_apply_media(&media);
    assert(s_artwork_count == 4);
    assert(strcmp(s_artwork_trace[3], "art-b") == 0);

    media.artwork_ref[0] = '\0';
    controller_view_compat_apply_media(&media);
    assert(s_artwork_count == 5);
    assert(strcmp(s_artwork_trace[4], "") == 0);

    media.artwork_generation = 9;
    controller_view_compat_apply_media(&media);
    assert(s_artwork_count == 5);

    controller_view_compat_reset();
    media = media_with_artwork("art-a", 7);
    controller_view_compat_apply_media(&media);
    assert(s_artwork_count == 6);
    assert(strcmp(s_artwork_trace[5], "art-a") == 0);

    int presentation_count = s_presentation.count;
    controller_view_compat_apply_media(NULL);
    assert(s_presentation.count == presentation_count);
    assert(s_artwork_count == 6);
}

static void test_connectivity_mapping(void) {
    reset_trace();
    controller_connectivity_view_t connectivity;
    controller_connectivity_view_init(
        &connectivity, "Testing Bridge", "Attempt 2 of 5");

    controller_view_compat_apply_connectivity(&connectivity);
    assert(s_presentation.count == 1);
    assert(strcmp(s_presentation.line1, "Testing Bridge") == 0);
    assert(strcmp(s_presentation.line2, "Attempt 2 of 5") == 0);
    assert(strcmp(s_presentation.line3, "") == 0);
    assert(!s_presentation.playing);
    assert(s_presentation.volume == 0.0f);
    assert(s_presentation.volume_min == 0.0f);
    assert(s_presentation.volume_max == 100.0f);
    assert(s_presentation.volume_step == 1.0f);
    assert(s_presentation.position == 0);
    assert(s_presentation.duration == 0);
    assert(s_artwork_count == 0);

    controller_view_compat_apply_connectivity(NULL);
    assert(s_presentation.count == 1);
}

static void test_command_values(void) {
    controller_command_t empty = {0};
    controller_command_t toggle =
        controller_command_make(CONTROLLER_COMMAND_TOGGLE_PLAYBACK);
    controller_command_t next =
        controller_command_make(CONTROLLER_COMMAND_NEXT_TRACK);
    controller_command_t previous =
        controller_command_make(CONTROLLER_COMMAND_PREVIOUS_TRACK);
    controller_command_t volume =
        controller_command_adjust_volume(INT32_MIN);

    assert(empty.kind == CONTROLLER_COMMAND_NONE);
    assert(empty.volume_steps == 0);
    assert(toggle.kind == CONTROLLER_COMMAND_TOGGLE_PLAYBACK);
    assert(toggle.volume_steps == 0);
    assert(next.kind == CONTROLLER_COMMAND_NEXT_TRACK);
    assert(next.volume_steps == 0);
    assert(previous.kind == CONTROLLER_COMMAND_PREVIOUS_TRACK);
    assert(previous.volume_steps == 0);
    assert(volume.kind == CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS);
    assert(volume.volume_steps == INT32_MIN);
}

int main(void) {
    test_owned_bounded_values();
    test_media_forwarding_and_artwork();
    test_connectivity_mapping();
    test_command_values();
    puts("controller value contracts passed");
    return 0;
}
