#pragma once

// Pure C decision module for whether the now-playing header's zone label
// should be shown or allowed to fade to a dim glyph. No LVGL dependency;
// callers (ui.c) drive it with timestamps and read back a visibility bit.
//
// Rules:
//  - A zone count that has ever been >= 2 in this session makes the label
//    sticky: always shown, forever (single-zone households never see this
//    module fade anything once a second zone has appeared).
//  - A count of 0 is "unknown" (zones can vanish transiently on a flaky
//    bridge poll) and never changes the decision, and never resets timers.
//  - With count == 1, the label becomes eligible to auto-fade only after
//    the count has been continuously 1 for at least
//    ZONE_LABEL_POLICY_STABLE_MS.
//  - Once eligible, a zone-name change or a "controls became visible" event
//    shows the name for ZONE_LABEL_POLICY_REVEAL_MS before it fades again.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZONE_LABEL_POLICY_STABLE_MS  30000  // count==1 must hold this long before eligible
#define ZONE_LABEL_POLICY_REVEAL_MS   5000  // shown this long after a reveal trigger

typedef struct {
    bool multi_zone_seen;       // sticky: true once count >= 2 observed
    bool have_count;            // true once any count > 0 has been observed
    int last_nonzero_count;     // last observed count > 0 (0 == unknown/never seen)
    uint32_t single_since_ms;   // timestamp count became (or stayed) 1; 0 if not currently 1
    bool currently_single;      // true if last nonzero observation was exactly 1
    uint32_t reveal_until_ms;   // now_ms below which the label is force-shown
    bool reveal_active;         // whether reveal_until_ms is meaningful
} zone_label_policy_t;

// Initialize/reset state. Call once at startup.
void zone_label_policy_init(zone_label_policy_t *policy);

// Feed a new zone count observation with its timestamp (ms, monotonic).
// count == 0 is treated as "unknown" and is a no-op other than being ignored;
// it never resets the single-zone stability timer nor clears multi_zone_seen.
void zone_label_policy_feed_count(zone_label_policy_t *policy, int count, uint32_t now_ms);

// Feed a zone-name-changed event. If the policy is currently eligible to
// auto-fade (single zone stable for >= ZONE_LABEL_POLICY_STABLE_MS), this
// starts (or restarts) a reveal window of ZONE_LABEL_POLICY_REVEAL_MS.
void zone_label_policy_feed_name_changed(zone_label_policy_t *policy, uint32_t now_ms);

// Feed a "controls became visible" event (e.g. leaving art mode). Same
// reveal-window behavior as a name change.
void zone_label_policy_feed_controls_visible(zone_label_policy_t *policy, uint32_t now_ms);

// Query whether the label should be visible right now.
bool zone_label_policy_visible(const zone_label_policy_t *policy, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
