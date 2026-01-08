# Dive Session

**Intent:** fix
**Started:** 2026-01-05
**OH Context:** Cloud Atlas AI - Hi-Fi / Roon Knob Initiative

## Focus

Fix: Knob doesn't pick up late-arriving zones (#79)

## Problem

The knob's zone list is refreshed **only once at startup** (when `zone_resolved` is false).
After that, it never refreshes unless rebooted.

**Root cause:** `common/roon_client.c:676-677` - zones only fetched when `!zone_resolved`

## Solution

Use SHA-based change detection (same pattern as config_sha):
1. **Bridge**: Compute `zones_sha` from zone IDs, include in `/now_playing`
2. **Knob**: Compare `zones_sha` on each poll, re-fetch `/zones` when changed

## Artifacts

| Type | Link |
|------|------|
| Issue | https://github.com/muness/roon-knob/issues/79 |
| Knob PR | https://github.com/muness/roon-knob/pull/80 |
| Bridge PR | https://github.com/cloud-atlas-ai/unified-hifi-control/pull/23 |
| OH Log | bd0ba368-57ea-4428-b11b-06135f188a98 |

## Progress

- [x] GitHub issue created (#79)
- [x] Branches created (knob + bridge)
- [x] Bridge: Added `getZonesSha()` to bus
- [x] Bridge: Added `zones_sha` to `/now_playing` response
- [x] Bridge: Fixed missing SHA invalidation in `unregisterBackend()`
- [x] Knob: Parse `zones_sha` from response
- [x] Knob: Add `check_zones_sha()` to trigger zone refresh
- [x] Superego review (found unregisterBackend bug)
- [x] PRs created for CodeRabbit review
- [ ] CodeRabbit review / merge
- [ ] User testing

## Key Decisions

1. **Use zone IDs only for SHA** - name changes shouldn't trigger refresh
2. **Backward compatible** - knob ignores missing `zones_sha` from old bridges
3. **Lazy SHA computation** - only compute when requested, cache until zones change

## Sources

- Knob: `common/roon_client.c`
- Bridge: `src/bus/index.js`, `src/knobs/routes.js`
- OH: Roon Knob initiative (b95b994a-0930-4cab-8fa1-155ec14950ff)
