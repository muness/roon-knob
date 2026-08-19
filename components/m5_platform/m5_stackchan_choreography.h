#pragma once

#include "m5_platform.h"

#include <stdint.h>

/* A music cue is a tiny performance, not a random walk. Every variant has a
 * readable preparation, two musical accents, and a held flourish before the
 * BSP returns home. Angles use StackChan-BSP units (10 = 1 degree); speed uses
 * its documented 0..1000 scale. */
struct m5_stackchan_keyframe_t {
    int16_t yaw_angle;
    int16_t pitch_angle;
    uint16_t speed;
    uint16_t hold_ms;
    m5_platform_stackchan_face_cue_t face;
};

inline constexpr m5_stackchan_keyframe_t M5_STACKCHAN_DANCES[4][4] = {
    /* Sway: anticipate, then a relaxed left-right phrase. */
    {{0, 40, 220, 360, M5_PLATFORM_STACKCHAN_FACE_ANTICIPATE},
     {-300, 80, 270, 680, M5_PLATFORM_STACKCHAN_FACE_BEAM_LEFT},
     {300, 120, 290, 720, M5_PLATFORM_STACKCHAN_FACE_BEAM_RIGHT},
     {-120, 40, 240, 560, M5_PLATFORM_STACKCHAN_FACE_WINK}},
    /* Head-bob: two clear beats with a small sideways pickup. */
    {{120, 30, 230, 340, M5_PLATFORM_STACKCHAN_FACE_ANTICIPATE},
     {120, 240, 300, 560, M5_PLATFORM_STACKCHAN_FACE_POP},
     {-120, 30, 270, 560, M5_PLATFORM_STACKCHAN_FACE_BEAM_LEFT},
     {0, 170, 260, 520, M5_PLATFORM_STACKCHAN_FACE_BEAM_RIGHT}},
    /* Diagonal groove: a slow, legible figure-eight suggestion. */
    {{-100, 50, 220, 360, M5_PLATFORM_STACKCHAN_FACE_ANTICIPATE},
     {-260, 170, 290, 640, M5_PLATFORM_STACKCHAN_FACE_BEAM_LEFT},
     {250, 40, 300, 660, M5_PLATFORM_STACKCHAN_FACE_BEAM_RIGHT},
     {100, 190, 260, 560, M5_PLATFORM_STACKCHAN_FACE_POP}},
    /* Peek: look, answer, then land on a wink. */
    {{-180, 70, 240, 400, M5_PLATFORM_STACKCHAN_FACE_ANTICIPATE},
     {0, 210, 280, 560, M5_PLATFORM_STACKCHAN_FACE_POP},
     {240, 80, 290, 640, M5_PLATFORM_STACKCHAN_FACE_BEAM_RIGHT},
     {-80, 40, 230, 600, M5_PLATFORM_STACKCHAN_FACE_WINK}},
};
