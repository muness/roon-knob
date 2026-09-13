#include "kizz_semantic_native_layout.h"

namespace {

constexpr KizzSemanticHitRegion kRegions[] = {
    // Artwork + bottom transport.
    {2, KizzSemanticAction::PreviousTrack, 5, 199, 98, 35},
    {2, KizzSemanticAction::TogglePlayback, 111, 199, 98, 35},
    {2, KizzSemanticAction::NextTrack, 217, 199, 98, 35},
    // Artwork + side transport.
    {3, KizzSemanticAction::PreviousTrack, 244, 62, 70, 44},
    {3, KizzSemanticAction::TogglePlayback, 244, 113, 70, 52},
    {3, KizzSemanticAction::NextTrack, 244, 172, 70, 44},
    // Artwork + metadata band.
    {4, KizzSemanticAction::TogglePlayback, 220, 214, 94, 22},
    // Balanced split.
    {5, KizzSemanticAction::PreviousTrack, 8, 181, 96, 40},
    {5, KizzSemanticAction::TogglePlayback, 112, 181, 96, 40},
    {5, KizzSemanticAction::NextTrack, 216, 181, 96, 40},
    // Metadata focus.
    {6, KizzSemanticAction::TogglePlayback, 91, 181, 104, 38},
    {6, KizzSemanticAction::NextTrack, 204, 181, 104, 38},
    // Transport focus.
    {7, KizzSemanticAction::PreviousTrack, 10, 78, 96, 70},
    {7, KizzSemanticAction::TogglePlayback, 112, 78, 96, 70},
    {7, KizzSemanticAction::NextTrack, 214, 78, 96, 70},
    // Volume focus.
    {8, KizzSemanticAction::VolumeDown, 18, 145, 84, 42},
    {8, KizzSemanticAction::TogglePlayback, 118, 145, 84, 42},
    {8, KizzSemanticAction::VolumeUp, 218, 145, 84, 42},
    // Zone selection delegates to the existing native room picker.
    {9, KizzSemanticAction::OpenZonePicker, 42, 214, 236, 24},
};

bool contains(const KizzSemanticHitRegion &region, int x, int y) {
    return x >= region.x && y >= region.y &&
           x < region.x + region.width && y < region.y + region.height;
}

} // namespace

size_t kizz_semantic_hit_region_count(uint8_t family_token) {
    size_t count = 0;
    for (const auto &region : kRegions)
        if (region.family_token == family_token) ++count;
    return count;
}

bool kizz_semantic_hit_region(uint8_t family_token, size_t index,
                              KizzSemanticHitRegion *out) {
    if (!out) return false;
    size_t seen = 0;
    for (const auto &region : kRegions) {
        if (region.family_token != family_token) continue;
        if (seen++ == index) {
            *out = region;
            return true;
        }
    }
    return false;
}

bool kizz_semantic_hit_region_for_action(uint8_t family_token,
                                         KizzSemanticAction action,
                                         KizzSemanticHitRegion *out) {
    if (!out) return false;
    for (const auto &region : kRegions) {
        if (region.family_token == family_token && region.action == action) {
            *out = region;
            return true;
        }
    }
    return false;
}

bool kizz_semantic_hit_test(uint8_t family_token, int x, int y,
                            KizzSemanticAction *action) {
    if (!action) return false;
    *action = KizzSemanticAction::None;
    for (const auto &region : kRegions) {
        if (region.family_token == family_token && contains(region, x, y)) {
            *action = region.action;
            return true;
        }
    }
    return false;
}
