# Native HALO UI checks

This harness compiles the production `halo_navigation.c` renderer with HALO's pinned LVGL 9.6.0 and bitmap fonts. Pointer/scroll processing and widget rendering are real LVGL; hardware, queues, bridge state and HTTP are controlled substitutes. The network worker is not run. Fixtures are synthetic and no playback reaches a real server.

```sh
cmake -S tests/halo_native -B /tmp/halo-native-build -DLVGL_SOURCE="$PWD/halo_app/managed_components/lvgl__lvgl" -DCJSON_SOURCE="$IDF_PATH/components/json/cJSON" -DHALO_SANITIZE=ON
cmake --build /tmp/halo-native-build -j8
/tmp/halo-native-build/halo_native_checks
```

Without `CJSON_SOURCE`, CMake uses `pkg-config libcjson`; CI installs `libcjson-dev`. CI fetches the exact upstream LVGL 9.6.0 commit, matching the target's pinned version. Pass one existing output directory as the executable's optional argument to export round-masked PPM previews of Menu, Settings, Zones and the text library and playlist actions.

The harness checks slow pointer presses, vertical scrolling without accidental activation or navigation, 64-zone focus, touch/encoder scrolling, text-only album rows, selected-item Play/Queue and collection Open, artwork-mode volume reveal, Back availability, cancellation on Home and chooser Back, stale image and old-zone browse results, repeated image URLs and excess detents across page boundaries. It uses the HALO's 24 KiB initial plus 72 KiB expansion pool. Native host object sizes and RGB888 rendering differ from the ESP's 32-bit objects/RGB565 display; device qualification remains mandatory.

The audit follow-up adds setup/offline/storage recovery and artwork restoration, native network/bridge entry validation, timeout feedback, saved-network confirmation and cancellation, brightness debounce/load/save boundary checks, and Search filtering with preserved offsets and automatic continuation past unsupported-only pages. Shared UI, actual flash erase and NVS are boundary substitutes in this harness; hardware persistence and physical keyboard usability remain device checks.

The same source builds `dial_native_checks` with Dial device-control capabilities enabled. CI runs both profiles. It additionally checks native 180-degree pointer navigation, optional device-controls scrolling, and latching the whole panel-wake contact until release. Neither host profile emulates the real Dial sleep engine, panel or BLE/OTA hardware.

The Shortcuts fixtures check the first menu entry, visible room, row selection, encoder focus, one pending launch, uncertain outcomes without replay, and Back. Production shared-backend tests separately cover bounded catalog parsing, UTF-8 truncation, old bridge schemas, stable IDs, single-flight admission shared with BLE, and endpoint/room/network/interaction fencing.
