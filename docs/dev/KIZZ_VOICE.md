# Kizz voice: wake word, transcription, and action

Kizz is a music controller with voice support. A person says a wake phrase, then a
music request such as “play Mulatu Astatke in the kitchen.” Kizz recognizes the
wake phrase locally, listens for the request, sends one bounded audio turn to a
LAN voice gateway, shows what it heard, and lets the gateway ask the existing
music-control MCP tools to carry out the request.

This document explains the implementation and its current evidence. The separate [HiPhi Kizz training recipe](https://github.com/open-horizon-labs/microWakeWord/tree/codex/issue-231-kizz/recipes/kizz) explains how we build and evaluate the on-device wake-word model.

## The path a voice request takes

```t
person speaks
    -> Kizz microphone and M5Unified audio frontend
    -> local microWakeWord model recognizes “HiPhi Kizz”
    -> Kizz changes to Listening and buffers the spoken command
    -> LAN WebSocket: UHC /voice/v1
    -> streaming speech-to-text providers
    -> transcript + zone + now-playing context
    -> persistent Codex App Server conversation
    -> UHC MCP music tools
    -> Kizz receives transcript, character response, and state
```

The wake model and the post-wake command path are deliberately separate. A wake
failure points to microphone/model/threshold behavior. A valid wake followed by
“speech recognition unavailable” points later in the path: capture, transport,
speech recognition, App Server, or a music command.

## What runs on Kizz

### Microphone and wake word

Kizz uses the supported M5Unified microphone path at 16 kHz. The microphone
frames feed two consumers:

- the local `microWakeWord` detector, which is always armed when Kizz is ready;
- the ESP-SR audio frontend, which supplies voice activity detection (VAD) for
  the post-wake turn.

The firmware packages the model behind `components/kizz_wake_word`. It exposes
the detector state, score, threshold, sliding window, and transition count so a
physical test can distinguish poor recall from a paused or faulted detector.
The current model and configuration can also be changed at runtime by a
`wake_config` message; reflashing is not required for a threshold experiment.

The training recipe is the source of truth for its corpus, split rules,
augmentation, and physical tests.

### Wake evidence and configurable capture diagnostics

The production decision is made directly by the ordered-state `HiPhi Kizz`
model. There is no second runtime verifier and no post-detection score gate:
once the ordered model fires, the device transitions to Listening immediately.
The firmware retains a bounded PSRAM evidence snapshot only so the resulting
audio can be quarantined and reviewed; evidence capture cannot reject a wake.

The enrollment service accepts the capture diagnostics `c_min_rms_dbfs`,
`c_max_clip_percent`, and `capture_all_wakes`. These fields describe and select
quarantined evidence; they do not alter the production wake decision.

When `capture_all_wakes` is enabled, each wake accepted by the
single enrollment upload worker is sent to the independent enrollment
WebSocket as `wake_observation`; a wake arriving while that worker is busy is
not preserved in this first slice. Command-leading
wakes are stored under `observations/wakes/`; no-command wakes remain in
`observations/false-wakes/`. Neither path changes `device-corpus.json`.
Promotion into a hard negative is a separate human-reviewed operation.

Each observation now includes one second of pre-wake audio from a PSRAM ring
buffer in the same WAV, with `pre_wake_ms`, `pre_wake_samples`, and
`post_wake_samples` metadata. C metrics remain calculated from post-wake audio
only. The training fork can correlate observations with UHC's recent STT race
using `tools/analyze_wake_observations.py`; its output is weak-label evidence,
not corpus truth.

### Listening and command capture

When the local detector fires, Kizz shows its listening state and sends a `start`
message containing its current zone. It pauses wake-word detection only while it
owns the command turn, then resumes it after a response or recovery.

Kizz waits for the wake phrase to end, then uses local VAD to decide when the
spoken command has finished. It buffers the command in PSRAM first and sends it
to the gateway after capture, in bounded WebSocket chunks. This avoids allowing
network backpressure to silently discard microphone frames while someone speaks.

The firmware reports the timing and byte counts for each turn:

- wake to command commit;
- command duration and trailing silence;
- captured versus transmitted audio bytes;
- VAD speech and silence frames;
- detector state and current score.

Those fields are necessary when a command looks like an STT failure but Kizz
actually captured too little speech or transmitted too little audio.

### Device feedback and recovery

The server may send a transcript, an end-of-turn hint, or a state such as
`thinking`, `clarify`, or `idle`. Kizz uses those to render the listening face,
thinking response, transcript bubble, response bubble, and error state. If the
gateway disconnects or a response times out, Kizz returns the microphone to the
listener rather than leaving wake-word detection paused.

Voice lifecycle feedback is intentionally quiet: listening, thinking, success,
and clarify states do not play a sound. An empty or false-wake turn is a quiet
visual recovery, not an error performance. StackChan sways use a smaller motion
envelope and lower speed; expressive sound cues remain for explicit physical
controls and attention-worthy connectivity events.

The exact voice WebSocket URI is a StackChan-only firmware setting,
`M5_PLATFORM_STACKCHAN_VOICE_WS_URI`. It can point at any LAN host. The training
enrollment URI is a separate setting and is never derived from the production
gateway host.

## What runs in UHC

UHC exposes three LAN endpoints:

| Endpoint | Purpose |
| --- | --- |
| `GET /voice/v1` (WebSocket upgrade) | One Kizz command turn: control messages plus 16 kHz mono PCM audio. |
| `GET` / `POST /voice/provider` | Read or change the preferred STT provider at runtime. |
| `GET /voice/reliability` | In-memory connection, completion, failure, partial-transcript, and latency records. |

The `start` message includes Kizz’s current zone. Before handing a transcript to
Codex App Server, UHC also reads the current title, artist, and album for that
zone. The resulting prompt gives the agent the listener’s likely playback
context without making the device guess a room or track itself.

UHC keeps a single Codex App Server conversation warm after the HTTP listener
starts. That conversation has access to UHC’s music MCP server. It is not a
device-side language model and it does not receive a raw open microphone stream:
it receives a completed transcript plus bounded contextual information.

## Speech-to-text providers and models

Every turn currently opens all three configured streaming recognizers. The
runtime “provider” setting determines their order, not whether the other two
run. This was intentional for comparison and fallback work, but it means current
telemetry is not a fair vendor benchmark and it costs three streaming sessions
per command.

| Provider | Model wired today | How UHC uses it | Current conclusion |
| --- | --- | --- | --- |
| Deepgram | `flux-general-en` | Receives 16 kHz PCM and keyterms `HiPhi`, `Kizz`, `Roon`. Its end-of-turn signal is recorded as timing telemetry, but does **not** commit a Kizz request. | Flux end-of-turn was too eager for conversational Kizz audio. Keep it as an observation until a controlled endpointing test says otherwise. |
| AssemblyAI | `u3-rt-pro` | Receives coalesced 80 ms PCM chunks. On local/VAD commit, UHC sends `ForceEndpoint` and can use a non-empty final transcript. | Produces usable finals in live turns, but finalization has also timed out or returned empty results. It is not yet reliable enough to call the production default. |
| ElevenLabs | `scribe_v2_realtime` by default; configurable with `ELEVENLABS_STT_MODEL` | Receives 100 ms base64 PCM messages and a manual commit. The first non-empty final can win the turn. | Produces usable finals, but the current protocol path also closes streams or commits empty transcripts. It is an active experiment, not a proven replacement. |

### Latest live reliability snapshot

The following is a snapshot from UHC’s in-memory `/voice/reliability` endpoint
on 2026-08-23. It covers the endpoint’s most recent 200 provider events, not a
scripted equal-audio benchmark. Several turns were partial, empty, or unattended;
the data therefore describes **this integration’s present health**, not provider
recognition accuracy.

| Provider | Distinct turns in the retained records | Turns with a final or endpoint hint | Turns with a first partial | Turns with a recorded failure | Median final/hint latency |
| --- | ---: | ---: | ---: | ---: | ---: |
| Deepgram Flux | 29 | 4 endpoint hints | 4 | 25 | 5.9 s |
| AssemblyAI | 30 | 8 finals | 10 | 21 | 8.3 s |
| ElevenLabs | 30 | 8 finals | 11 | 22 | 8.7 s |

The counts overlap: one turn can have a partial, a final, and a later failure
from a competing provider. They must not be turned into a single success rate.
The failure records include empty turns, finalization timeouts, and closed
streams. The next evaluation should send the same labeled Kizz recordings to
each provider separately, measure transcript accuracy and three latency points
(first partial, final transcript, command result), then choose a default from
that data.

## Configuration and operations

The launcher, `scripts/run_kizz_voice.sh` in UHC, loads LAN voice credentials
from `~/.config/open-horizon-labs/voice.env`, starts UHC on port 8088, and waits
for `/voice/reliability` to respond. Keep API keys in that environment file, not
in firmware, source, or documentation.

The useful runtime variables are:

| Variable | Meaning | Current default |
| --- | --- | --- |
| `KIZZ_STT_PROVIDER` | Provider ordering: `deepgram`, `assemblyai`, or `elevenlabs` | `deepgram` |
| `DEEPGRAM_EOT_TIMEOUT_MS` | Deepgram Flux endpoint timeout | `3000` ms in the launcher |
| `ASSEMBLYAI_MIN_TURN_SILENCE_MS` | AssemblyAI’s minimum silence setting | `1200` ms in the launcher |
| `ASSEMBLYAI_MAX_TURN_SILENCE_MS` | AssemblyAI’s maximum silence setting | `3000` ms in the launcher |
| `ELEVENLABS_STT_MODEL` | ElevenLabs realtime STT model | `scribe_v2_realtime` |

Kizz’s command-end silence and maximum utterance duration are runtime-configured
firmware settings and appear in its turn telemetry. The device-side VAD remains
the authority for Kizz’s normal commit; Deepgram’s endpoint is only a recorded
hint.

## What still needs proof

- A controlled provider comparison with identical device-recorded utterances.
- A provider selection policy that chooses one recognizer for production rather
  than opening all three on every turn.
- Per-turn durable telemetry that joins Kizz capture/transport data, provider
  transcripts, App Server result, and actual music-command result.
- Physical tests that show a failed STT turn always restores the ARMED listener.
- Wake-word qualification against the recipe’s held-out people, rooms, and
  long false-wake guards before calling the model ready.
