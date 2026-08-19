#include "m5_stackchan_choreography.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {
int magnitude(int value) { return value < 0 ? -value : value; }
}

int main() {
    static_assert(sizeof(M5_STACKCHAN_DANCES) /
                      sizeof(M5_STACKCHAN_DANCES[0]) ==
                  4);

    for (const auto &dance : M5_STACKCHAN_DANCES) {
        assert(dance[0].face == M5_PLATFORM_STACKCHAN_FACE_ANTICIPATE);

        int min_yaw = dance[0].yaw_angle;
        int max_yaw = dance[0].yaw_angle;
        uint32_t duration_ms = 0;
        bool has_delight_cue = false;
        for (const auto &frame : dance) {
            assert(magnitude(frame.yaw_angle) <= 300);
            assert(frame.pitch_angle >= 0 && frame.pitch_angle <= 240);
            assert(frame.speed >= 200 && frame.speed <= 320);
            assert(frame.hold_ms >= 300 && frame.hold_ms <= 750);
            min_yaw = frame.yaw_angle < min_yaw ? frame.yaw_angle : min_yaw;
            max_yaw = frame.yaw_angle > max_yaw ? frame.yaw_angle : max_yaw;
            duration_ms += frame.hold_ms;
            has_delight_cue = has_delight_cue ||
                frame.face == M5_PLATFORM_STACKCHAN_FACE_BEAM_LEFT ||
                frame.face == M5_PLATFORM_STACKCHAN_FACE_BEAM_RIGHT ||
                frame.face == M5_PLATFORM_STACKCHAN_FACE_POP ||
                frame.face == M5_PLATFORM_STACKCHAN_FACE_WINK;
        }

        /* Each move must read as a phrase, not a tremor or a single pose. */
        assert(max_yaw - min_yaw >= 240);
        assert(duration_ms >= 1900 && duration_ms <= 2600);
        assert(has_delight_cue);
    }

    /* The random choice must actually produce recognizably different moves. */
    for (std::size_t first = 0; first < 4; ++first) {
        for (std::size_t second = first + 1; second < 4; ++second) {
            bool different = false;
            for (std::size_t frame = 0; frame < 4; ++frame) {
                const auto &a = M5_STACKCHAN_DANCES[first][frame];
                const auto &b = M5_STACKCHAN_DANCES[second][frame];
                different = different || a.yaw_angle != b.yaw_angle ||
                            a.pitch_angle != b.pitch_angle || a.face != b.face;
            }
            assert(different);
        }
    }
    return 0;
}
