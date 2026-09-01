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
    -> local streaming detector finds a “Kizz Control” candidate
    -> compact fixed-window verifier rejects obvious collisions
    -> ordered-state verifier makes the final wake decision
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
The display probability cutoff and sliding window can also be changed at
runtime by a `wake_config` message. The provenance-bound detector and verifier
thresholds are compiled into the firmware and require a rebuilt artifact.

The training recipe is the source of truth for its corpus, split rules,
augmentation, and physical tests.

### Wake evidence and configurable capture diagnostics

Wake detection is a three-stage `Kizz Control` cascade. An ordered-state INT8
detector runs continuously on 30 ms feature windows. A detector candidate
freezes 260 feature frames (2.2 seconds before the trigger through 390 ms after
it). A compact INT8 depthwise-separable verifier rejects obvious collisions;
only candidates that pass it reach an independent ordered-state verifier for
the final decision. A rejected candidate re-arms the detector without opening
a voice turn.

All three networks use fixed generated C execution graphs with statically
planned arenas and ESP-NN kernels where applicable. The detector keeps a 16 KiB
arena in internal RAM. The compact verifier uses a 96 KiB PSRAM arena and the
ordered verifier a separate 16 KiB PSRAM arena, so their transient activation
memory does not consume the RAM required by Wi-Fi, HTTP, mDNS, UI, and voice
tasks.

The active v10 compact verifier excludes the 54 short-lead v9 device captures,
whose 0.55-second playback lead created impossible zero-padded prefixes. V10
was retrained from 31 qualified full-pre-roll StackChan captures in a clean
26,986-row candidate corpus. Its threshold was capped at `0.0` on 12/12
voice-disjoint validation captures before opening a fresh test set, where the
detector and compact gate retained 12/12 recall.

The exact v10 StackChan binary was built and flashed with ESP-IDF 5.5.5 on an
ESP32-S3 revision 0.2. All three startup AOT/reference checks passed, followed
by 12/12 physical speaker-replay accepts. The continuous detector ran at about
8 ms p99 per 10 ms hop; compact verification took 95–123 ms and ordered
verification 296–432 ms when reached. The audio queue peaked at 2,048 of 16,384
bytes with zero ring overflows, partial writes, or partial feature reads. The
compact arena used 82,480 of 98,304 PSRAM bytes; detector and ordered arenas
used 12,316 bytes each.

On the unchanged locked 100.47-hour LibriSpeech negative corpus, v10 produced
23 full-cascade false wakes (`0.229/hour`, one-sided 95% upper bound
`0.324/hour`). The compact gate forwarded 833 of 19,105 detector candidates
(4.36%). This meets the maintainer-accepted practical ceiling of `0.4/hour`,
but not the formal `0.1/hour` upper-confidence gate.

The first wake-plus-voice coexistence run exposed a production configuration
fault rather than a model fault: the optional enrollment client kept reconnecting
while the voice gateway, Wi-Fi, audio frontend, and cascade were active. That
run reached a 16-byte internal-heap low-water mark, failed one socket
allocation, and dropped enough queued detector audio to invalidate product
qualification.

The production StackChan profile now leaves the independent enrollment URI
empty. Enrollment remains an explicit directed-training build option. The exact
replacement firmware binary (`91f8c6162628d1f3823800e35d52ba8f27a350e092c0f1e87e30452d958a0a59`)
was built with ESP-IDF 5.5.5, flashed to ESP32-S3 MAC
`7c:4f:ad:af:e7:38`, and exercised against a live UHC voice gateway. A
12-source physical positive replay accepted 11/12 wakes while internal heap
stayed above 11,896 bytes. A subsequent event-gated physical command test
accepted “Kizz Control,” captured “Set the kitchen volume to 38%,” obtained
usable transcripts from all three configured STT providers, issued the Roon
action, and independently read Kitchen back at 38%. That run kept 12,944 bytes
of internal-heap low-water, had no socket-allocation failure, reboot, ring
overflow, or partial audio read/write, and restored the armed listener after
the response.

The command run's detector queue peaked at 14,848 of 16,384 bytes. Its 274,432
reported dropped bytes accumulated while the detector was intentionally paused
during accepted wake turns and command handling; the command stream itself sent
all 267,776 captured bytes. Candidate-triggered full-cascade hops remain much
longer than the 10 ms continuous budget (about 420–460 ms), while the continuous
detector remained about 7.5 ms at p99. Queue, ring, and partial-I/O counters—not
the paused-source drop counter alone—are therefore the coexistence gate.

Wake-transition samples reported as dropped are intentional: the wake source
microphone is stopped and reset after acceptance while the same PCM continues
into the AFE/STT path. They are distinct from verifier starvation, for which
the queue and ring counters remained clean.

The bounded PSRAM snapshot is also retained for quarantined evidence and
review. Evidence upload remains independent of the production decision.

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

Each observation now includes three seconds of pre-wake audio from a PSRAM ring
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
gateway host. It is empty in the production StackChan profile; a directed
enrollment build must opt in with an explicit URI override.

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
- Repeat physical wake testing with human voices, multiple rooms, distances,
  and playback noise; the 12/12 speaker replay establishes the device execution
  path, not general human recall.
