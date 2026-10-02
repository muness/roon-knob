#include "zone_label_policy.h"

#include <string.h>

void zone_label_policy_init(zone_label_policy_t *policy) {
    if (!policy) return;
    memset(policy, 0, sizeof(*policy));
}

void zone_label_policy_feed_count(zone_label_policy_t *policy, int count, uint32_t now_ms) {
    if (!policy) return;

    if (count <= 0) {
        // Unknown / transient absence. Never touches any state.
        return;
    }

    policy->have_count = true;
    policy->last_nonzero_count = count;

    if (count >= 2) {
        policy->multi_zone_seen = true;
        policy->currently_single = false;
        policy->single_since_ms = 0;
        return;
    }

    // count == 1
    if (!policy->currently_single) {
        policy->currently_single = true;
        policy->single_since_ms = now_ms;
    }
    // If already single (including after a 0 gap), leave single_since_ms
    // untouched so hysteresis is not reset by transient disappearances.
}

static void reveal(zone_label_policy_t *policy, uint32_t now_ms) {
    policy->reveal_active = true;
    policy->reveal_until_ms = now_ms + ZONE_LABEL_POLICY_REVEAL_MS;
}

void zone_label_policy_feed_name_changed(zone_label_policy_t *policy, uint32_t now_ms) {
    if (!policy) return;
    reveal(policy, now_ms);
}

void zone_label_policy_feed_controls_visible(zone_label_policy_t *policy, uint32_t now_ms) {
    if (!policy) return;
    reveal(policy, now_ms);
}

static bool eligible_to_fade(const zone_label_policy_t *policy, uint32_t now_ms) {
    if (policy->multi_zone_seen) return false;
    if (!policy->currently_single) return false;
    uint32_t elapsed = now_ms - policy->single_since_ms;  // wraps safely (unsigned)
    return elapsed >= ZONE_LABEL_POLICY_STABLE_MS;
}

bool zone_label_policy_visible(const zone_label_policy_t *policy, uint32_t now_ms) {
    if (!policy) return true;

    if (!eligible_to_fade(policy, now_ms)) {
        return true;
    }

    if (policy->reveal_active && now_ms < policy->reveal_until_ms) {
        return true;
    }

    return false;
}
