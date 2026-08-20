#pragma once

#include "m5_platform.h"

#include <stddef.h>
#include <stdint.h>

/* Kismet generated expression from a three-dimensional affect space:
 * arousal, valence and stance. StackChan cannot reproduce Kismet's ears,
 * eyelids, brows, lips and jaw independently, but keeping the same dimensions
 * gives our reduced graphic face a coherent family resemblance instead of a
 * bag of unrelated emoji. Values are normalized to -100..100. */
struct m5_stackchan_face_family_t {
    m5_platform_stackchan_face_cue_t cue;
    const char *name;
    int8_t arousal;
    int8_t valence;
    int8_t stance;
    uint8_t variants;
};

inline constexpr m5_stackchan_face_family_t M5_STACKCHAN_FACE_FAMILIES[] = {
    {M5_PLATFORM_STACKCHAN_FACE_NEUTRAL, "neutral", 0, 0, 0, 1},
    {M5_PLATFORM_STACKCHAN_FACE_ANTICIPATE, "anticipate", 65, 25, 70, 2},
    {M5_PLATFORM_STACKCHAN_FACE_BEAM_LEFT, "joy-left", 70, 90, 65, 2},
    {M5_PLATFORM_STACKCHAN_FACE_BEAM_RIGHT, "joy-right", 70, 90, 65, 2},
    {M5_PLATFORM_STACKCHAN_FACE_POP, "surprise", 100, 10, 90, 3},
    {M5_PLATFORM_STACKCHAN_FACE_WINK, "sly", 35, 65, -25, 3},
    {M5_PLATFORM_STACKCHAN_FACE_SAD, "sorrow", -45, -90, -65, 2},
    {M5_PLATFORM_STACKCHAN_FACE_SETTLE, "calm", -30, 40, 20, 2},
    {M5_PLATFORM_STACKCHAN_FACE_ATTENTIVE, "interest", 55, 45, 85, 3},
    {M5_PLATFORM_STACKCHAN_FACE_RESTING, "fatigue", -85, -10, -20, 3},
    {M5_PLATFORM_STACKCHAN_FACE_CURIOUS, "curious", 50, 20, 75, 3},
    {M5_PLATFORM_STACKCHAN_FACE_RELIEVED, "relief", -10, 70, 60, 2},
    {M5_PLATFORM_STACKCHAN_FACE_GLANCE_LEFT, "glance-left", 35, 25, 35, 4},
    {M5_PLATFORM_STACKCHAN_FACE_GLANCE_RIGHT, "glance-right", 35, 25, 35, 4},
    {M5_PLATFORM_STACKCHAN_FACE_LOUD, "more", 85, 75, 80, 4},
    {M5_PLATFORM_STACKCHAN_FACE_HUSH, "less", -45, 25, -10, 4},
    {M5_PLATFORM_STACKCHAN_FACE_PROUD, "proud", 45, 85, 35, 3},
    {M5_PLATFORM_STACKCHAN_FACE_SHY, "shy", 10, 55, -70, 3},
    {M5_PLATFORM_STACKCHAN_FACE_WORRIED, "worried", 55, -65, -55, 3},
    {M5_PLATFORM_STACKCHAN_FACE_CONTENT, "content", -10, 75, 25, 3},
    {M5_PLATFORM_STACKCHAN_FACE_ACCEPTING, "accepting", 25, 55, 100, 3},
    {M5_PLATFORM_STACKCHAN_FACE_STERN, "stern", 20, -25, -100, 3},
    {M5_PLATFORM_STACKCHAN_FACE_ANGER, "anger", 90, -90, 95, 3},
    {M5_PLATFORM_STACKCHAN_FACE_DISGUST, "disgust", 25, -85, -60, 3},
    {M5_PLATFORM_STACKCHAN_FACE_FEAR, "fear", 100, -85, -100, 3},
    {M5_PLATFORM_STACKCHAN_FACE_BORED, "bored", -100, -30, -40, 3},
};

inline constexpr size_t M5_STACKCHAN_FACE_FAMILY_COUNT =
    sizeof(M5_STACKCHAN_FACE_FAMILIES) /
    sizeof(M5_STACKCHAN_FACE_FAMILIES[0]);

inline constexpr uint8_t m5_stackchan_face_variant_count(
    m5_platform_stackchan_face_cue_t cue) {
    for (const auto &family : M5_STACKCHAN_FACE_FAMILIES) {
        if (family.cue == cue) return family.variants;
    }
    return 1;
}

inline constexpr uint8_t m5_stackchan_face_variant(
    m5_platform_stackchan_face_cue_t cue, uint32_t entropy) {
    return static_cast<uint8_t>(entropy % m5_stackchan_face_variant_count(cue));
}
