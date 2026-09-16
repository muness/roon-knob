# BLE HID Remote Host

HiPhi controllers can pair with a separate Bluetooth Low Energy media remote.
The firmware acts as the BLE HID **host**: it scans for a remote that exposes
the HID over GATT Profile (HOGP), receives Consumer Control reports, and maps
supported keys into the shared playback controller.

This is not the older, unimplemented mode where the historical Roon Knob advertised itself as
a BLE keyboard/media controller. The two roles are opposites:

| Role | This firmware | Peer |
| --- | --- | --- |
| BLE HID host | HiPhi Frame or HiPhi Dial | Physical media remote |
| BLE HID device | Not implemented | Phone, computer, or TV |

ESP32-S3 supports Bluetooth LE but not Classic Bluetooth. This feature neither
uses nor claims Classic Bluetooth, A2DP, or AVRCP support.

## Supported input

The shared host maps these Consumer Control usages:

| Remote key | Controller action |
| --- | --- |
| Play/Pause | Toggle playback |
| Next Track | Next track |
| Previous Track | Previous track |
| Volume Up | Increase the selected zone's volume |
| Volume Down | Decrease the selected zone's volume |

Mute is ignored because the shared controller does not currently expose an
explicit mute command. Unknown report formats and usages are ignored safely.
HOGP remotes are not perfectly uniform, so support must be verified with the
specific remote.

## Target behavior

- **HiPhi Frame:** enabled by default to preserve its established behavior.
- **HiPhi Dial:** compiled into the normal ESP32-S3 firmware but disabled by
  default. Enable it from the connected device's settings page.
- Pairing, enablement, and the remembered remote are stored only in that
  device's local NVS. Nothing is copied or synchronized between Frame and
  Dial.

Both targets expose scan, pair, connection status, disable, and forget
operations through target-owned settings. Display code remains target-specific;
the shared Bluetooth component never calls e-ink or LVGL APIs.

Those settings commands are asynchronous. Each form action redirects through a
one-shot watch URL so the browser refreshes after the owner task consumes the
command, even when the first redirected GET still sees the previous steady
state. Transitional states continue refreshing until they settle.

## Lifecycle

The shared `rk_ble_hid_host` component owns NimBLE and `esp_hid` behind one
serialized command queue. Its public lifecycle is:

ESP-IDF posts its HID open callback before it finishes CCCD subscriptions.
The shared owner therefore keeps the public state at `CONNECTING` until both
that callback arrives and the blocking `esp_hidh_dev_open()` call returns.
`CONNECTED` means media-report notifications are actually configured.

```
UNAVAILABLE -> DISABLED -> STARTING -> READY
                                      |-> SCANNING
                                      |-> CONNECTING -> CONNECTED
active state -> STOPPING -> DISABLED
```

Disable cancels outstanding work, closes and frees HID devices, deinitializes
the HID host, stops the NimBLE host task, waits for its exit acknowledgement,
and then deinitializes the NimBLE port. If bounded teardown cannot complete,
the service reports an error that requires reboot instead of pretending it is
safe to re-enable.

Startup NVS, NimBLE, HID, and host-sync failures are reported by name and retried
up to five times with bounded backoff. A successful sync clears the retry
budget. Exhausted startup retries remain in `ERROR`; teardown failures remain
reboot-required.

Forget is stronger than disconnect: it invalidates reconnect work, clears the
local remembered-device metadata, and deletes the NimBLE peer security record.

## Pairing

1. Connect the controller to Wi-Fi.
2. Open its settings page in a browser.
3. On Dial, enable **BLE Media Remote** if it is disabled.
4. Put the physical remote into pairing mode.
5. Start a scan and select the remote.
6. Confirm transport and volume keys operate the selected playback zone.

Pairing uses the BLE “Just Works” security model because these controllers and
typical media remotes have no shared display/PIN-entry path. The bond is kept
in NVS so the host can reconnect after restart.

ESP-IDF 5.5's NimBLE HID wrapper changes the host I/O capability to keyboard
input during initialization. The shared host restores `NO_INPUT_OUTPUT`
after `esp_hidh_init()` and leaves connection security initiation to the HID
wrapper. A separate GAP listener observes encryption completion and handles
repeat pairing by deleting a stale peer bond; it must not start a competing
security procedure.

### ESP-IDF HID interoperability override

The original Frame implementation built against the February 2026
`release-v5.5` wrapper. ESP-IDF commit `85bee88b27f5` subsequently malformed the
branch nesting between Device Information and HID characteristic discovery;
the pinned v5.5.5 wrapper can therefore discover the peer's services without
constructing a valid HID report list. A later change also made the wrapper read
and subscribe to an optional Battery Service before subscribing to HID input
reports. Some media remotes never answer that battery CCCD write. In either
state `esp_hidh_dev_open()` remains blocked, the public state correctly stays
`CONNECTING`, and no media-key report can reach the application.

The repository therefore overrides only the pinned `nimble_hidh.c` translation
unit. It restores the pre-regression HID branch structure, resets per-characteristic
report ownership, bounds report-list traversal, retains the one-shot battery
read, skips battery notifications, and continues with every HID report
subscription. The override also clears the wrapper's successful-read scratch
pointer when ownership moves to its caller; otherwise disabling BLE after a
read can free the same buffer again during HID deinitialization. Each source
transformation fails at CMake configuration if the expected ESP-IDF 5.5.5 block
changes, and CI verifies that both production targets compile the generated
translation unit.

On hardware, a successful open should proceed from
`Skipping optional Battery Service notification subscription` through the HID
CCCD writes to `CONNECTED`. A button press must then produce a raw HID input
log and a mapped Consumer Control action. ESP-IDF 5.5 may label that event's
usage as `GENERIC` even when its bytes contain a standard Consumer Control
usage. The shared host therefore follows the proven Frame behavior and decodes
the payload itself: only the explicitly supported media usages are accepted,
regardless of the wrapper's metadata; zero/release and all unknown payloads are
still ignored. Discovery and encryption alone are not sufficient evidence that
the remote is usable.

## Build configuration

The shipping Frame and Dial ESP32-S3 artifacts enable the shared host
capability:

```text
CONFIG_RK_BLE_HID_HOST=y
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_NVS_PERSIST=y
CONFIG_BT_NIMBLE_SM_SC=y
CONFIG_BT_NIMBLE_HID_SERVICE=y
CONFIG_BT_NIMBLE_GATT_MAX_PROCS=2
```

Targets that intentionally exclude Bluetooth depend on a stub component instead
of `rk_ble_hid_host`. The stub exposes the same stable API and reports
`UNAVAILABLE`; NimBLE and `esp_hid` are not compiled. The stub component wrapper
is kept under `rk_ble_hid_host/optional/`, outside the production top-level
component-discovery set, so it cannot silently win link-order resolution in a
Bluetooth-enabled artifact. Targets opt into that directory explicitly. CI
builds an ESP32-S3 fixture for this profile.

### Production-link invariant

The production targets must link `rk_ble_hid_host.c`; a stub that merely
returns `UNAVAILABLE` is only valid for an explicit BLE-disabled target. This
is easy to get wrong because ESP-IDF discovers top-level components by
directory. The optional stub therefore lives outside that discovery set and a
BLE-disabled target names it explicitly.

CI enforces both sides of the contract from the generated build files and link
map:

- Dial and Frame compile the real host and resolve `rk_ble_hid_host_init` from
  its object file;
- neither production map contains the stub; and
- the BLE-off fixture links only the stub and no NimBLE or `esp_hid` object.

Do not accept a green compile alone as evidence that a BLE build contains the
real implementation.

### Resource and scheduler budget

Dial shares internal RAM and radio time between an LVGL/QSPI display, Wi-Fi,
and NimBLE. The shipping profile is intentionally narrow:

The cross-subsystem allocation rules are centralized in [ESP32-S3 Memory
Architecture](MEMORY.md); this section records the BLE-specific budget.

| Resource | Production setting | Reason |
| --- | --- | --- |
| Active connections | 1 | One paired media remote is the product requirement. |
| Preferred ATT MTU | 128 bytes | Consumer-control reports do not need the former 256-byte default. |
| Concurrent GATT procedures | 2 | ESP-IDF's HID client can begin service discovery while MTU exchange is still active; one slot makes discovery fail immediately. |
| NimBLE mbuf/ACL/event pools | Reduced to the single-remote budget | Avoid reserving internal heap for unused throughput and links. |
| NimBLE dynamic allocations | PSRAM (`MEM_ALLOC_MODE_EXTERNAL`) | Protects internal/DMA heap needed by LVGL and display DMA. |
| UI task | Core 1, 16 KiB internal stack | Keeps rendering and its stack away from radio work. |
| NimBLE host/service tasks | Core 0, internal stacks | Co-locates BLE with Wi-Fi while retaining cache-safe stacks for bonding/NVS paths. |
| Bridge network worker | 16 KiB PSRAM stack | Reclaims a contiguous internal block after endpoint persistence was moved to the internal UI task. |
| LVGL object heap | 24 KiB internal + 72 KiB PSRAM | Reclaims 40 KiB of fixed internal `.bss` while retaining a 96 KiB total UI-object budget. |
| JSON/adaptive UI payloads | Bounded PSRAM bodies, parse trees, and queued views | Keeps both large responses and many small transient allocations out of controller/DMA memory. |
| Wi-Fi task | Core 0 | Explicit ESP-IDF configuration. |

Task stacks that execute flash, NVS, OTA, display DMA, or radio-controller paths
are deliberately **not** moved to PSRAM. ESP-IDF can disable the external-memory
cache during those operations, so those stacks remain internal. The bridge
worker is the narrow exception: it performs network fetch/parse work only and
hands its former mDNS endpoint commit to the internal UI task. “Use PSRAM for
NimBLE” still means NimBLE's dynamic pools, not its task stacks.

ESP-IDF's current `esp_hid` BLE-host wrapper has a non-obvious configuration
dependency: its symbols are compiled behind `BT_NIMBLE_HID_SERVICE`, which in
turn requires the NimBLE GATT-server and peripheral switches. They remain
enabled solely to satisfy that wrapper even though the application does not
advertise a local HID device. Broadcaster and unrelated standard services stay
disabled. The persistent bond store is set to two entries: ESP-IDF 5.5's
one-entry configuration triggers an out-of-bounds compiler diagnostic in its
sorting implementation. This does not increase the one-active-connection
limit.

### Instrumented coexistence checks

Boot logs report internal/DMA/PSRAM free space and largest blocks after display
allocation, UI initialization, UI-task creation, bridge start, and BLE-host
initialization. They also report early and periodic stack high-water marks and
the actual core for the UI, BLE service, and NimBLE host tasks.

When diagnosing a static Dial display, first establish whether the log reaches
`UI loop task started on core 1`. The pre-BLE portion of boot is already a
useful test: the HID service is created only after Wi-Fi obtains an IP, so a
static display before provisioning does not prove an active BLE connection is
the immediate cause. Compare the before/after UI and BLE memory checkpoints,
then test BLE pairing separately after the UI loop is healthy.

In particular, use the **largest internal block**, not total internal free
heap, when assessing UI/BLE coexistence. LVGL DMA buffers can fragment the
internal heap enough to prevent the UI stack from being created even while
total free memory appears generous; see [Display Subsystem](DISPLAY.md) for
the concrete Dial failure and acceptance rule.

The first BLE-controller failure was observed with only 4,867 bytes of internal
heap free and a 1,600-byte largest block immediately before
`nimble_port_init()`. Moving the bridge worker and transient HTTP/HTML buffers
to PSRAM improved that checkpoint to 21,055 bytes free and a 10,240-byte
largest block, but `esp_bt_controller_init` still failed with `-4`. Map analysis
then identified LVGL's fixed 64 KiB internal `.bss` pool as the remaining large
reservation. Dial now keeps a 24 KiB internal LVGL base and registers a 72 KiB
PSRAM expansion pool, reclaiming 40 KiB of contiguous internal memory without
shrinking the measured 16 KiB UI stack. Hardware acceptance requires the new
pre-NimBLE checkpoint and a successful controller start; a green build alone
does not establish either.

The first split-pool artifacts never reached that checkpoint:
`lv_mem_add_pool()` rejected the original 64 KiB external pool because LVGL's compiled
`LV_MEM_POOL_EXPAND_SIZE` was still zero. An explicitly aligned allocation
failed identically, ruling out alignment. The effective build must therefore
assert both the 24 KiB base and 72 KiB expansion Kconfig values.

## Coexistence verification

ESP32-S3 shares its 2.4 GHz radio between Wi-Fi and BLE. A successful compile
does not prove coexistence. Before a firmware is released, the exact artifact
must be exercised on its target hardware for:

- cold boot and Wi-Fi provisioning;
- scan, pair, reconnect, disable/re-enable, and forget/reboot;
- playback-state and artwork traffic while remote keys are active;
- Wi-Fi loss and recovery during BLE activity;
- heap and task-stack headroom;
- absence of reset loops or off-task display access.

Frame and Dial require separate hardware evidence even though they share the
same BLE service.
