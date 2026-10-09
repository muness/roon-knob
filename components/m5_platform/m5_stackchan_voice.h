#pragma once

#include "m5_platform.h"

#include <stddef.h>
#include <stdint.h>

/* Kismet's vocal system varied pitch contour, timing and intensity by affect.
 * These compact proto-voice phrases apply the same principle to M5Unified's
 * tone generator. They are intentionally not speech and contain no sampled
 * or copied Kismet audio. */
struct m5_stackchan_voice_note_t {
    uint16_t frequency_hz;
    uint16_t duration_ms;
    uint16_t gap_ms;
};

constexpr size_t M5_STACKCHAN_VOICE_MAX_NOTES = 5;

struct m5_stackchan_voice_phrase_t {
    m5_platform_stackchan_sound_t sound;
    const char *name;
    uint8_t priority;
    uint16_t cooldown_ms;
    uint8_t note_count;
    m5_stackchan_voice_note_t notes[M5_STACKCHAN_VOICE_MAX_NOTES];
};

inline constexpr m5_stackchan_voice_phrase_t M5_STACKCHAN_VOICE_PHRASES[] = {
    {M5_PLATFORM_STACKCHAN_SOUND_MORE, "more-rise", 20, 260, 2,
     {{520, 55, 18}, {680, 75, 0}, {}, {}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_MORE, "more-bloom", 20, 260, 3,
     {{440, 42, 12}, {554, 48, 12}, {740, 70, 0}, {}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_MORE, "more-question", 20, 260, 2,
     {{610, 48, 22}, {820, 82, 0}, {}, {}, {}}},

    {M5_PLATFORM_STACKCHAN_SOUND_LESS, "less-fall", 20, 260, 2,
     {{680, 55, 18}, {520, 80, 0}, {}, {}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_LESS, "less-soft", 20, 260, 3,
     {{720, 38, 12}, {600, 44, 12}, {450, 72, 0}, {}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_LESS, "less-hush", 20, 260, 2,
     {{560, 45, 22}, {390, 86, 0}, {}, {}, {}}},

    {M5_PLATFORM_STACKCHAN_SOUND_PREVIOUS, "previous-turn", 25, 320, 3,
     {{760, 44, 12}, {620, 48, 12}, {470, 72, 0}, {}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_PREVIOUS, "previous-hop", 25, 320, 2,
     {{700, 58, 20}, {440, 92, 0}, {}, {}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_PREVIOUS, "previous-curlicue", 25, 320, 4,
     {{740, 35, 10}, {620, 38, 10}, {680, 35, 10}, {470, 70, 0}, {}}},

    {M5_PLATFORM_STACKCHAN_SOUND_NEXT, "next-turn", 25, 320, 3,
     {{470, 44, 12}, {620, 48, 12}, {760, 72, 0}, {}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_NEXT, "next-hop", 25, 320, 2,
     {{440, 58, 20}, {700, 92, 0}, {}, {}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_NEXT, "next-curlicue", 25, 320, 4,
     {{470, 35, 10}, {680, 38, 10}, {620, 35, 10}, {780, 70, 0}, {}}},

    {M5_PLATFORM_STACKCHAN_SOUND_PLAY, "play-awake", 35, 700, 3,
     {{420, 50, 15}, {560, 55, 15}, {720, 90, 0}, {}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_PLAY, "play-ready", 35, 700, 2,
     {{500, 65, 20}, {750, 105, 0}, {}, {}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_PAUSE, "pause-rest", 35, 700, 3,
     {{660, 55, 18}, {520, 60, 18}, {390, 105, 0}, {}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_PAUSE, "pause-soft", 35, 700, 2,
     {{580, 70, 24}, {410, 120, 0}, {}, {}, {}}},

    {M5_PLATFORM_STACKCHAN_SOUND_CONNECTED, "hello-rise-fall", 60, 1800, 4,
     {{440, 70, 18}, {660, 85, 18}, {820, 95, 18}, {620, 120, 0}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_CONNECTED, "hello-warble", 60, 1800, 5,
     {{520, 55, 14}, {700, 65, 14}, {580, 55, 14},
      {780, 75, 14}, {650, 105, 0}}},
    {M5_PLATFORM_STACKCHAN_SOUND_LOST, "lost-sigh", 100, 1200, 4,
     {{620, 80, 24}, {520, 90, 24}, {430, 105, 28}, {330, 150, 0}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_LOST, "lost-question", 100, 1200, 3,
     {{560, 80, 28}, {410, 120, 30}, {360, 170, 0}, {}, {}}},

    {M5_PLATFORM_STACKCHAN_SOUND_NEW_TRACK, "track-spark", 55, 850, 5,
     {{440, 45, 12}, {554, 48, 12}, {659, 52, 12},
      {880, 70, 16}, {659, 95, 0}}},
    {M5_PLATFORM_STACKCHAN_SOUND_NEW_TRACK, "track-bounce", 55, 850, 5,
     {{520, 45, 12}, {700, 60, 12}, {580, 45, 12},
      {820, 70, 12}, {680, 100, 0}}},
    {M5_PLATFORM_STACKCHAN_SOUND_NEW_TRACK, "track-answer", 55, 850, 4,
     {{480, 65, 20}, {720, 85, 28}, {540, 65, 20}, {810, 110, 0}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_NEW_TRACK, "track-wink", 55, 850, 5,
     {{620, 40, 10}, {740, 42, 10}, {860, 48, 18},
      {700, 55, 12}, {930, 85, 0}}},

    {M5_PLATFORM_STACKCHAN_SOUND_NEW_ROOM, "room-arrival", 45, 1200, 3,
     {{480, 60, 18}, {640, 75, 18}, {560, 105, 0}, {}, {}}},
    {M5_PLATFORM_STACKCHAN_SOUND_NEW_ROOM, "room-found", 45, 1200, 3,
     {{520, 55, 14}, {690, 70, 14}, {610, 100, 0}, {}, {}}},
};

inline constexpr size_t M5_STACKCHAN_VOICE_PHRASE_COUNT =
    sizeof(M5_STACKCHAN_VOICE_PHRASES) /
    sizeof(M5_STACKCHAN_VOICE_PHRASES[0]);
