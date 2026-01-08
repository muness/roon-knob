# Dive Session - roon_client → bridge_client Rename

**Intent:** plan (refactoring)
**Started:** 2026-01-07T21:00:00Z

## Investigation Complete - Evidence Gathered

### Summary

Comprehensive codebase analysis reveals **13 public API functions** and **1 internal thread function** with `roon_` prefix, plus **1 struct type** that should be renamed to reflect the multi-backend bridge architecture.

### Files Impacted

**Source files to rename (2 files):**
1. `common/roon_client.c` (1,458 lines) → `common/bridge_client.c`
2. `common/roon_client.h` (22 lines) → `common/bridge_client.h`

**Files with #include references (8 files):**
1. `common/app_main.c` - Line 7: `#include "roon_client.h"`
2. `common/ui.c` - Line 15: `#include "roon_client.h"`
3. `idf_app/main/ui_network.c` - Line 13: `#include "roon_client.h"`
4. `idf_app/main/main_idf.c` - Line 13: `#include "roon_client.h"`
5. `idf_app/main/display_sleep.c` - Line 4: `#include "roon_client.h"`
6. `idf_app/main/config_server.c` - Line 7: `#include "roon_client.h"`
7. `idf_app/main/platform_display_idf.c` - Line 4: `#include "roon_client.h"`
8. (roon_client.c includes itself)

**Build files (1 file):**
1. `idf_app/main/CMakeLists.txt` - Line 27: `"../../common/roon_client.c"`

**Documentation files (5 files):**
1. `docs/esp/BLE_HID.md` - References deleting `roon_client.c`
2. `docs/dev/NVS_STORAGE.md` - Example calls `roon_client_set_zone`
3. `docs/dev/testing/CODE_REVIEW_FINDINGS.md` - Multiple references to `roon_client.c` lines
4. `docs/dev/DEVELOPMENT.md` - Architecture diagram shows `roon_client.c`
5. `docs/howto/PORTING.md` - Port guide references `common/roon_client.c`

### Symbols Catalog

**Public API functions (11 functions):**
All in `common/roon_client.h`:
1. `roon_client_start()` - Initialize client (line 8)
2. `roon_client_handle_input()` - Process UI input (line 9)
3. `roon_client_handle_volume_rotation()` - Velocity-sensitive volume (line 10)
4. `roon_client_set_network_ready()` - Network status callback (line 11)
5. `roon_client_get_artwork_url()` - Build artwork URL (line 12)
6. `roon_client_is_ready_for_art_mode()` - Check art mode readiness (line 13)
7. `roon_client_set_device_ip()` - Store device IP for recovery (line 16)
8. `roon_client_get_bridge_retry_count()` - Bridge retry attempts (line 17)
9. `roon_client_get_bridge_retry_max()` - Max retry threshold (line 18)
10. `roon_client_get_bridge_url()` - Get configured bridge URL (line 19)
11. `roon_client_is_bridge_connected()` - Bridge connection status (line 20)
12. `roon_client_is_bridge_mdns()` - Was bridge discovered via mDNS? (line 21)

**Internal functions with roon_ prefix (1 function):**
1. `roon_poll_thread()` - Background polling thread (static)

**Internal types with roon_ prefix (1 struct):**
1. `struct roon_state` - Internal state struct (line 65 in .c file)

**Special constants (NO rename needed):**
- `ZONE_ID_BACK`, `ZONE_ID_SETTINGS` - These are special zone IDs, not roon-specific

### String Literals - STABILITY CRITICAL

**✅ NO hard-coded "roon" strings found in config keys or NVS storage**

This means:
- No NVS keys like `"roon_bridge_url"` that must remain stable
- No HTTP endpoint paths with "/roon/" that clients depend on
- No JSON keys like `"roon_status"` in API responses

**Result: Rename is backward-compatible at binary level**

### Function Call Sites

**27 call sites across 7 files:**

`common/app_main.c` (2 calls):
- Line 22: `ui_set_input_handler(roon_client_handle_input);`
- Line 24: `roon_client_start(&cfg);`

`common/ui.c` (2 calls):
- Line 1103: `roon_client_handle_volume_rotation(ticks);`
- Line 1216: `roon_client_get_artwork_url(url, ...);`

`idf_app/main/ui_network.c` (2 calls):
- Line 177: `roon_client_get_bridge_url(bridge_url, ...);`
- Line 184: `roon_client_is_bridge_mdns()`

`idf_app/main/main_idf.c` (4 calls):
- Line 102: `roon_client_set_device_ip(ip_opt);`
- Line 103: `roon_client_set_network_ready(true);`
- Line 120: `roon_client_set_network_ready(false);`
- Line 130: `roon_client_set_network_ready(false);`

`idf_app/main/display_sleep.c` (1 call):
- Line 301: `roon_client_is_ready_for_art_mode()`

`idf_app/main/config_server.c` (3 calls):
- Line 183: `roon_client_is_bridge_connected()`
- Line 184: `roon_client_get_bridge_retry_count()`
- Line 185: `roon_client_get_bridge_retry_max()`

`idf_app/main/platform_display_idf.c` (2 calls):
- Line 410: `roon_client_is_ready_for_art_mode()`
- Line 435: `roon_client_is_ready_for_art_mode()`

### Architecture Context

From BRIDGE_DRAFT_SPEC.md (just documented):
- ESP32 firmware speaks HTTP to unified-hifi-control bridge
- Bridge supports multiple backends: Roon, LMS, OpenHome, UPnP
- Zone IDs are prefixed: `roon:abc`, `lms:xyz`, `openhome:123`, `upnp:456`
- Firmware is backend-agnostic - doesn't know/care about backend protocol

From docs/meta/decisions/2025-12-22_DECISION_BRIDGE_ARCHITECTURE.md:
- Bridge is mandatory (ESP32 can't run Roon protocol directly due to memory/complexity)
- Bridge abstracts all backend protocols behind simple HTTP REST API
- Name "roon_client" is historical artifact from when it only supported Roon

### Rename Scope Decision

**RENAME (14 symbols):**
1. `roon_client.c` → `bridge_client.c` (file)
2. `roon_client.h` → `bridge_client.h` (file)
3. `roon_client_start` → `bridge_client_start`
4. `roon_client_handle_input` → `bridge_client_handle_input`
5. `roon_client_handle_volume_rotation` → `bridge_client_handle_volume_rotation`
6. `roon_client_set_network_ready` → `bridge_client_set_network_ready`
7. `roon_client_get_artwork_url` → `bridge_client_get_artwork_url`
8. `roon_client_is_ready_for_art_mode` → `bridge_client_is_ready_for_art_mode`
9. `roon_client_set_device_ip` → `bridge_client_set_device_ip`
10. `roon_client_get_bridge_retry_count` → `bridge_client_get_bridge_retry_count`
11. `roon_client_get_bridge_retry_max` → `bridge_client_get_bridge_retry_max`
12. `roon_client_get_bridge_url` → `bridge_client_get_bridge_url`
13. `roon_client_is_bridge_connected` → `bridge_client_is_bridge_connected`
14. `roon_client_is_bridge_mdns` → `bridge_client_is_bridge_mdns`
15. `roon_poll_thread` → `bridge_poll_thread` (internal)
16. `struct roon_state` → `struct bridge_state` (internal)

**DO NOT RENAME:**
- `ZONE_ID_BACK` - Generic constant, not Roon-specific
- `ZONE_ID_SETTINGS` - Generic constant, not Roon-specific
- Any NVS keys (none found with "roon" prefix)
- Any HTTP paths (none found)

### Risk Analysis

**LOW RISK:**
- No ABI breaks: No structs/enums exposed in public API
- No config key changes: No NVS storage keys to preserve
- No wire protocol changes: HTTP endpoints unchanged
- Git history preserved: Use `git mv` for renames
- Pure symbol rename: No behavior changes

**TESTING REQUIRED:**
- ESP32-S3 firmware build (idf.py build)
- PC simulator build (if exists - not found in investigation)
- Flash to device and verify basic functionality
- Verify zone switching works
- Verify volume control works
- Verify artwork loading works

### Implementation Plan

**Phase 1: Rename files (preserve git history)**
```bash
git mv common/roon_client.c common/bridge_client.c
git mv common/roon_client.h common/bridge_client.h
```

**Phase 2: Update includes (8 files)**
Search/replace `"roon_client.h"` → `"bridge_client.h"`

**Phase 3: Update function names**
Search/replace (case-sensitive):
- `roon_client_` → `bridge_client_`
- `roon_poll_thread` → `bridge_poll_thread`
- `struct roon_state` → `struct bridge_state`

**Phase 4: Update build config**
`idf_app/main/CMakeLists.txt` line 27:
- `"../../common/roon_client.c"` → `"../../common/bridge_client.c"`

**Phase 5: Update documentation (5 files)**
Update references in docs to use new names

**Phase 6: Test**
- Clean build: `rm -rf build && idf.py build`
- Flash to device
- Verify all functions work
- Check for compiler warnings

### Estimated Impact

- **Lines changed:** ~60-80 lines (mostly includes and function calls)
- **Files modified:** 16 files total
- **Complexity:** LOW (pure rename, no logic changes)
- **Risk:** LOW (no ABI/config/protocol changes)
- **Test effort:** MEDIUM (requires device testing)

### Next Steps

1. Create GitHub issue with this analysis
2. Wait for user approval
3. Execute rename in feature branch
4. Test on actual hardware
5. Create PR with checklist

## Sources
- Code: common/roon_client.{c,h} (full read)
- Code: All 8 files with includes (grep analysis)
- Build: idf_app/main/CMakeLists.txt (full read)
- Docs: BRIDGE_DRAFT_SPEC.md, DECISION_BRIDGE_ARCHITECTURE.md
- Evidence: Grep searches for symbols, strings, references
