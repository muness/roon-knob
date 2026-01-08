# Dive Session

**Intent:** fix
**Started:** 2026-01-04
**OH Endeavor:** Roon Knob (b95b994a-0930-4cab-8fa1-155ec14950ff)

## Focus

Fix swipe gesture UX issues:

1. **#43** - Swipe gestures don't adapt to 180° rotation
2. **#66** - Improve swipe detection for art mode (add double-tap alternative)

## Context

### Issues

**#43 - Rotation bug:**
- After 180° rotation, swipe-up becomes swipe-down (coordinates inverted)
- Gesture detection uses raw dy without rotation transform

**#66 - Detection improvement:**
- Swipes can be hard to trigger
- User suggestion: add double-tap on artwork area as alternative

### Key Code

| File | Purpose |
|------|---------|
| `idf_app/main/platform_display_idf.c` | Touch callback, swipe detection |
| `common/ui.c` | Art mode toggle, UI state |
| `docs/esp/SWIPE_GESTURES.md` | Gesture implementation docs |

### Current Swipe Logic (from docs)

```c
// Swipe up: negative Y, more vertical than horizontal
if (dy < -SWIPE_MIN_DISTANCE && abs(dy) > abs(dx)) {
    s_pending_art_mode = true;
}
// Swipe down: positive Y, more vertical than horizontal
else if (dy > SWIPE_MIN_DISTANCE && abs(dy) > abs(dx)) {
    s_pending_exit_art_mode = true;
}
```

Problem: This assumes standard orientation. With 180° rotation, user's "swipe up" produces positive dy (raw coordinates), so nothing triggers.

### Solution Approach

1. **#43 fix:** Transform swipe direction based on rotation setting
   - Read rotation from config (`cfg->rotation`)
   - If 180°, invert the dy check

2. **#66 enhancement:** Add double-tap to toggle art mode
   - Track tap count and timing
   - Double-tap on artwork area toggles art mode
   - Supplements (not replaces) swipe gestures

## Workflow

1. Read current gesture code in `platform_display_idf.c`
2. Implement rotation-aware swipe detection
3. Add double-tap detection
4. Test both orientations
5. Update SWIPE_GESTURES.md
6. PR → review → merge

## Constraints

- Touch callback runs in LVGL context - defer state changes
- CST816 is single-touch only
- Don't break existing swipe behavior for 0° rotation
