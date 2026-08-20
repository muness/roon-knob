# StackChan personality library

This directory treats StackChan's personality as reusable product data, not as
an effect baked into the current playback screen.

## Catalogs

- `m5_stackchan_faces.h` defines 26 expression families in Kismet's
  arousal/valence/stance space. A renderer may use the supplied discrete cue,
  interpolate between families, or reinterpret the coordinates for different
  hardware. Variant counts let repeated actions avoid looking mechanical.
- `m5_stackchan_choreography.h` defines eight optional four-keyframe dances.
  Motion is intentionally sparse: ordinary controls are face/sound responses;
  the body is reserved for new tracks, connection loss, and explicit delight.
- `m5_stackchan_voice.h` defines 26 original proto-voice pitch contours for ten
  product events. These are authored tone sequences, not Kismet recordings.
- `m5_platform.h` exposes the stable cue, expression, and sound vocabulary.
  `m5_platform.cpp` is the current M5Unified/M5StackChan-BSP playback adapter.

## Adaptive-UI contract

The controller or adaptive UI decides *why* StackChan should respond: volume
changed, transport moved, playback started, a track arrived, the room changed,
or connectivity changed. It then chooses one or more library cues using the
interaction's importance and frequency.

The device adapter decides *how* those cues are rendered. It may draw the
current high-contrast graphic face, animate a richer vector face, omit sound,
or decline body motion entirely. It must not duplicate servo transport, speaker
drivers, calibration, or board pin knowledge; those remain official M5Stack
abstractions.

On the current StackChan adapter, both body and sounds default on and persist
independently. Long-hold the face for body language; double-click the physical
front button for sounds. A single front-button click opens the on-device
Personality page, and the web Settings page exposes the same two switches.

Recommended defaults:

| Interaction | Face | Sound | Body |
| --- | --- | --- | --- |
| Volume up/down | `more` / `less` variant | short, cooldown-limited | none |
| Previous/next | directional glance variant | short, cooldown-limited | none |
| Play/pause | surprise / fatigue | short | none |
| New track | interest plus track reveal | one track phrase | optional dance |
| Room changed | proud | one arrival phrase | none |
| Connected | relief | greeting | none |
| Connection lost | sorrow/worry | high-priority sigh | sad posture |
| Setup/waiting | curiosity/interest | normally silent | none |

## Adding interactions

Add affect families only when the existing 3D space cannot express the intent.
Prefer another variant when an interaction needs freshness but not new meaning.
Sounds should remain under 650 ms, use the event cooldown/priority scheduler,
and stay within the tested pitch range. New dances must keep the physical
bounds and readability assertions in `tests/test_m5_stackchan_choreography.cpp`.

## Design sources

- MIT Kismet framework: <https://groups.csail.mit.edu/lbr/sociable/kismet.html>
- Cynthia Breazeal's MIT thesis, especially Chapters 11 and 12:
  <https://groups.csail.mit.edu/lbr/hrg/2000/phd.pdf>
- MIT Kismet affective-response videos:
  <https://groups.csail.mit.edu/lbr/sociable/videos.html>
