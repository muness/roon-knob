#include "m5_stackchan_choreography.h"
#include "m5_stackchan_faces.h"
#include "m5_stackchan_voice.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {
int magnitude(int value) { return value < 0 ? -value : value; }
}

int main() {
    static_assert(M5_STACKCHAN_FACE_FAMILY_COUNT == 26);
    for (std::size_t index = 0; index < M5_STACKCHAN_FACE_FAMILY_COUNT; ++index) {
        const auto &family = M5_STACKCHAN_FACE_FAMILIES[index];
        assert(family.name && family.name[0]);
        assert(family.arousal >= -100 && family.arousal <= 100);
        assert(family.valence >= -100 && family.valence <= 100);
        assert(family.stance >= -100 && family.stance <= 100);
        assert(family.variants >= 1 && family.variants <= 4);
        assert(m5_stackchan_face_variant_count(family.cue) == family.variants);
        for (std::size_t other = index + 1;
             other < M5_STACKCHAN_FACE_FAMILY_COUNT; ++other)
            assert(family.cue != M5_STACKCHAN_FACE_FAMILIES[other].cue);
    }

    /* Repeated controls must actually have several readable variations. */
    assert(m5_stackchan_face_variant_count(
               M5_PLATFORM_STACKCHAN_FACE_GLANCE_LEFT) >= 3);
    assert(m5_stackchan_face_variant_count(
               M5_PLATFORM_STACKCHAN_FACE_GLANCE_RIGHT) >= 3);
    assert(m5_stackchan_face_variant_count(
               M5_PLATFORM_STACKCHAN_FACE_LOUD) >= 3);
    assert(m5_stackchan_face_variant_count(
               M5_PLATFORM_STACKCHAN_FACE_HUSH) >= 3);

    static_assert(sizeof(M5_STACKCHAN_DANCES) /
                      sizeof(M5_STACKCHAN_DANCES[0]) ==
                  8);

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
                frame.face == M5_PLATFORM_STACKCHAN_FACE_WINK ||
                frame.face == M5_PLATFORM_STACKCHAN_FACE_PROUD ||
                frame.face == M5_PLATFORM_STACKCHAN_FACE_SHY ||
                frame.face == M5_PLATFORM_STACKCHAN_FACE_CURIOUS ||
                frame.face == M5_PLATFORM_STACKCHAN_FACE_SETTLE;
        }

        /* Each move must read as a phrase, not a tremor or a single pose. */
        assert(max_yaw - min_yaw >= 240);
        assert(duration_ms >= 1900 && duration_ms <= 2800);
        assert(has_delight_cue);
    }

    /* The random choice must actually produce recognizably different moves. */
    for (std::size_t first = 0; first < 8; ++first) {
        for (std::size_t second = first + 1; second < 8; ++second) {
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

    int phrase_counts[M5_PLATFORM_STACKCHAN_SOUND_NEW_ROOM + 1] = {};
    for (const auto &phrase : M5_STACKCHAN_VOICE_PHRASES) {
        assert(phrase.name && phrase.name[0]);
        assert(phrase.sound >= M5_PLATFORM_STACKCHAN_SOUND_MORE);
        assert(phrase.sound <= M5_PLATFORM_STACKCHAN_SOUND_NEW_ROOM);
        assert(phrase.priority >= 20 && phrase.priority <= 100);
        assert(phrase.cooldown_ms >= 250 && phrase.cooldown_ms <= 2000);
        assert(phrase.note_count >= 2 &&
               phrase.note_count <= M5_STACKCHAN_VOICE_MAX_NOTES);
        ++phrase_counts[phrase.sound];
        uint32_t duration_ms = 0;
        for (uint8_t note = 0; note < phrase.note_count; ++note) {
            assert(phrase.notes[note].frequency_hz >= 300);
            assert(phrase.notes[note].frequency_hz <= 1000);
            assert(phrase.notes[note].duration_ms >= 35);
            assert(phrase.notes[note].duration_ms <= 180);
            assert(phrase.notes[note].gap_ms <= 40);
            duration_ms += phrase.notes[note].duration_ms +
                           phrase.notes[note].gap_ms;
        }
        assert(duration_ms <= 650);
    }
    assert(phrase_counts[M5_PLATFORM_STACKCHAN_SOUND_MORE] >= 3);
    assert(phrase_counts[M5_PLATFORM_STACKCHAN_SOUND_LESS] >= 3);
    assert(phrase_counts[M5_PLATFORM_STACKCHAN_SOUND_PREVIOUS] >= 3);
    assert(phrase_counts[M5_PLATFORM_STACKCHAN_SOUND_NEXT] >= 3);
    assert(phrase_counts[M5_PLATFORM_STACKCHAN_SOUND_NEW_TRACK] >= 4);
    return 0;
}
