# Native HALO UI checks

This harness compiles the production `halo_navigation.c` renderer with HALO's pinned LVGL 9.6.0 and bitmap fonts. Pointer/scroll processing and widget rendering are real LVGL; hardware, queues, bridge state and HTTP are controlled substitutes. The network worker is not run. Fixtures are synthetic and no playback reaches a real server.

```sh
cmake -S tests/halo_native -B /tmp/halo-native-build -DLVGL_SOURCE="$PWD/halo_app/managed_components/lvgl__lvgl" -DCJSON_SOURCE="$IDF_PATH/components/json/cJSON" -DHALO_SANITIZE=ON
cmake --build /tmp/halo-native-build -j8
/tmp/halo-native-build/halo_native_checks
```

Without `CJSON_SOURCE`, CMake uses `pkg-config libcjson`; CI installs `libcjson-dev`. CI fetches the exact upstream LVGL 9.6.0 commit, matching the target's pinned version. Pass one existing output directory as the executable's optional argument to export round-masked PPM previews of Menu, Settings, Zones, the text Library, Details and command/recovery states.

The harness checks slow pointer presses, vertical scrolling without accidental activation or navigation, 64-zone focus, touch/encoder scrolling, text-only album rows, selected-item Play/Queue and collection Open, artwork-mode volume reveal, Back availability, cancellation on Home and chooser Back, stale image and old-zone browse results, repeated image URLs and excess detents across page boundaries. It uses the HALO's 24 KiB initial plus 72 KiB expansion pool. Native host object sizes and RGB888 rendering differ from the ESP's 32-bit objects/RGB565 display; device qualification remains mandatory.

The shared Dial/HALO Library proofs cover a stable vertical text list, including artwork-bearing albums, with a 216px viewport and separate single-line title and artist/subtitle boxes. Row height follows two small-font line heights plus 8px; layout is checked with Lato 22 and a second real font, including long artist credits and embedded line breaks. Folder-only rows open directly with an Open cue; playable rows open Details with fully wrapping title, artist/subtitle and selected room. Compact Back/Home navigation remains visible, and rotary input scrolls Details rather than requiring a physical select click. Actions are explicit Open, Play and Queue according to item capabilities.

Focused overflowing title and artist/subtitle labels use circular marquee with a 1200ms initial pause and 2000ms repeat pause; unfocused labels use DOT ellipsis. Assertions check actual LVGL animation movement, timing, focus changes and clock preservation when focus is reapplied. Scroll-begin pauses all row marquees and settled focus resumes the reading pause. Details, sending, Queued, uncertain-command, loading and failed-browse states have round-masked preview fixtures. Queue retains the list, folder location, depth, offset and selection with a local acknowledgement; Play retains that context while opening Now playing. Sending and failed commands retain selected metadata. Failure notices precede retained content; stale browse rows are disabled. Retry library is asserted to send only BROWSE for the retained location/offset, never replaying Play or Queue. Failed commands also expose a direct Now playing action for playback verification.

These fixtures validate the production shared renderer's source/layout and controlled LVGL behavior for Dial and HALO. They do not qualify a physical actuator, touch panel, network transaction or exact firmware artifact. Search keyboard interaction is unsupported; no physical knob click is assumed. Frame and Bluetooth behavior are outside this Library proof scope.

The audit follow-up adds setup/offline/storage recovery and artwork restoration, native network/bridge entry validation, timeout feedback, saved-network confirmation and cancellation, brightness debounce/load/save boundary checks, and Search filtering with preserved offsets and automatic continuation past unsupported-only pages. Shared UI, actual flash erase and NVS are boundary substitutes in this harness; hardware persistence and physical keyboard usability remain device checks.

The same source builds `dial_native_checks` with Dial device-control capabilities enabled. CI runs both profiles. It additionally checks native 180-degree pointer navigation, optional device-controls scrolling, and latching the whole panel-wake contact until release. Neither host profile emulates the real Dial sleep engine, panel or BLE/OTA hardware.

The Shortcuts fixtures check the first menu entry, visible room, row selection, encoder focus, one pending launch, uncertain outcomes without replay, and Back. Production shared-backend tests separately cover bounded catalog parsing, UTF-8 truncation, old bridge schemas, stable IDs, single-flight admission shared with BLE, and endpoint/room/network/interaction fencing.
