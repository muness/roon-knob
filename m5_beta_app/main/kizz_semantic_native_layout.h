#pragma once

#include <stddef.h>
#include <stdint.h>

/* Target-local native composition vocabulary.  This header is deliberately
 * outside the public semantic ABI: the host sees roles and capabilities, not
 * these family names or action labels. */
enum class KizzSemanticAction : uint8_t {
    None = 0,
    PreviousTrack,
    TogglePlayback,
    NextTrack,
    VolumeDown,
    VolumeUp,
    OpenZonePicker,
};

struct KizzSemanticHitRegion {
    uint8_t family_token;
    KizzSemanticAction action;
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
};

size_t kizz_semantic_hit_region_count(uint8_t family_token);
bool kizz_semantic_hit_region(uint8_t family_token, size_t index,
                              KizzSemanticHitRegion *out);
bool kizz_semantic_hit_region_for_action(uint8_t family_token,
                                         KizzSemanticAction action,
                                         KizzSemanticHitRegion *out);
bool kizz_semantic_hit_test(uint8_t family_token, int x, int y,
                            KizzSemanticAction *action);
