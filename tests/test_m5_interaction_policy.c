#include "m5_interaction_policy.h"

#include <assert.h>
#include <stdint.h>

int main(void) {
    int64_t next = 0;

    /* Merely moving the handheld must never change volume. */
    assert(m5_interaction_twist_step(false, 180.0f, 1000, &next) == 0);
    assert(next == 0);
    /* Hand tremor and sensor noise stay inside the deadband. */
    assert(m5_interaction_twist_step(true, 44.9f, 1000, &next) == 0);
    assert(m5_interaction_twist_step(true, -44.9f, 1000, &next) == 0);
    /* Intentional twist is signed and rate limited. */
    assert(m5_interaction_twist_step(true, 90.0f, 1000, &next) == 1);
    assert(m5_interaction_twist_step(true, -90.0f, 2000, &next) == 0);
    assert(m5_interaction_twist_step(true, -90.0f, next, &next) == -1);

    assert(!m5_interaction_raise_wake(1.0f, 1.17f));
    assert(m5_interaction_raise_wake(1.0f, 1.19f));
    assert(m5_interaction_raise_wake(1.0f, 0.80f));

    const int64_t start = 1000000;
    assert(m5_interaction_power_action(
               start + 29 * 1000000LL, start, 0, 30, 60, 1200,
               false, false, false, false) == M5_POWER_ACTION_NONE);
    assert(m5_interaction_power_action(
               start + 30 * 1000000LL, start, 0, 30, 60, 1200,
               false, false, false, false) == M5_POWER_ACTION_DIM);
    /* Sleep outranks dim once both thresholds have elapsed. This catches the
     * tempting panel-dim-only implementation that caused the original drain. */
    assert(m5_interaction_power_action(
               start + 60 * 1000000LL, start, 0, 30, 60, 1200,
               false, false, false, false) ==
           M5_POWER_ACTION_CONNECTED_SLEEP);
    /* A pending retained-artwork transition may defer sleep, but not dimming. */
    assert(m5_interaction_power_action(
               start + 60 * 1000000LL, start, 0, 30, 60, 1200,
               false, false, false, true) == M5_POWER_ACTION_DIM);
    assert(m5_interaction_power_action(
               start + 60 * 1000000LL, start, 0, 30, 60, 1200,
               true, false, false, true) == M5_POWER_ACTION_NONE);
    /* Board power-off is staged from actual connected-sleep entry rather than
     * from last activity, so a late panel transition gets its full timeout. */
    const int64_t slept = start + 60 * 1000000LL;
    assert(m5_interaction_power_action(
               slept + 1199 * 1000000LL, start, slept, 30, 60, 1200,
               true, true, false, false) == M5_POWER_ACTION_NONE);
    assert(m5_interaction_power_action(
               slept + 1200 * 1000000LL, start, slept, 30, 60, 1200,
               true, true, false, false) == M5_POWER_ACTION_POWER_OFF);
    assert(m5_interaction_power_action(
               slept + 1200 * 1000000LL, start, slept, 30, 60, 1200,
               true, true, true, false) == M5_POWER_ACTION_NONE);
    return 0;
}
