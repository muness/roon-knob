#include "zone_label_policy.h"

#include <assert.h>
#include <stdio.h>

// (a) count 1 -> 0 -> 1 does not reset the 30s stability timer.
static void test_zero_gap_does_not_reset_timer(void) {
    zone_label_policy_t p;
    zone_label_policy_init(&p);

    uint32_t t = 0;
    zone_label_policy_feed_count(&p, 1, t);
    assert(zone_label_policy_visible(&p, t));  // not yet eligible

    t += 10000;
    zone_label_policy_feed_count(&p, 0, t);  // transient unknown, ignored
    assert(zone_label_policy_visible(&p, t));

    t += 1000;
    zone_label_policy_feed_count(&p, 1, t);  // back to 1; timer must not restart
    assert(zone_label_policy_visible(&p, t));

    // Total elapsed since the original single-zone observation is now 11s.
    // If the 0 had reset anything we'd still be far from 30s either way, so
    // push past 30s measured from the ORIGINAL t=0 mark and confirm it has
    // faded (proving the clock kept running through the 0 gap).
    t = 30001;
    zone_label_policy_feed_count(&p, 0, t);  // still just noise
    assert(!zone_label_policy_visible(&p, t));
}

// (b) count 2 then 1 stays visible forever (sticky multi-zone).
static void test_multi_zone_sticky(void) {
    zone_label_policy_t p;
    zone_label_policy_init(&p);

    uint32_t t = 0;
    zone_label_policy_feed_count(&p, 2, t);
    assert(zone_label_policy_visible(&p, t));

    t += 1000;
    zone_label_policy_feed_count(&p, 1, t);
    assert(zone_label_policy_visible(&p, t));

    t += 1000000;  // long past any stability window
    assert(zone_label_policy_visible(&p, t));
}

// (c) name change during eligibility shows for 5s then hides.
static void test_reveal_window_after_name_change(void) {
    zone_label_policy_t p;
    zone_label_policy_init(&p);

    uint32_t t = 0;
    zone_label_policy_feed_count(&p, 1, t);

    t = ZONE_LABEL_POLICY_STABLE_MS;  // exactly stable, now eligible
    assert(!zone_label_policy_visible(&p, t));  // no reveal pending -> faded

    zone_label_policy_feed_name_changed(&p, t);
    assert(zone_label_policy_visible(&p, t));

    t += ZONE_LABEL_POLICY_REVEAL_MS - 1;
    assert(zone_label_policy_visible(&p, t));

    t += 2;  // past the reveal window
    assert(!zone_label_policy_visible(&p, t));
}

// controls-visible event behaves the same as a name change.
static void test_reveal_window_after_controls_visible(void) {
    zone_label_policy_t p;
    zone_label_policy_init(&p);

    uint32_t t = 0;
    zone_label_policy_feed_count(&p, 1, t);
    t = ZONE_LABEL_POLICY_STABLE_MS + 500;
    assert(!zone_label_policy_visible(&p, t));

    zone_label_policy_feed_controls_visible(&p, t);
    assert(zone_label_policy_visible(&p, t));

    t += ZONE_LABEL_POLICY_REVEAL_MS + 1;
    assert(!zone_label_policy_visible(&p, t));
}

// (d) before 30s of stable single zone, never hides.
static void test_never_hides_before_stable_window(void) {
    zone_label_policy_t p;
    zone_label_policy_init(&p);

    uint32_t t = 0;
    zone_label_policy_feed_count(&p, 1, t);
    assert(zone_label_policy_visible(&p, t));

    t = ZONE_LABEL_POLICY_STABLE_MS - 1;
    assert(zone_label_policy_visible(&p, t));

    // Right at the threshold it becomes eligible (and, with no reveal
    // pending, fades).
    t = ZONE_LABEL_POLICY_STABLE_MS;
    assert(!zone_label_policy_visible(&p, t));
}

// Fresh policy (no counts ever observed) always shows the label.
static void test_default_visible_with_no_data(void) {
    zone_label_policy_t p;
    zone_label_policy_init(&p);
    assert(zone_label_policy_visible(&p, 0));
    assert(zone_label_policy_visible(&p, 1000000));
}

int main(void) {
    test_zero_gap_does_not_reset_timer();
    test_multi_zone_sticky();
    test_reveal_window_after_name_change();
    test_reveal_window_after_controls_visible();
    test_never_hides_before_stable_window();
    test_default_visible_with_no_data();
    puts("zone_label_policy tests passed");
    return 0;
}
