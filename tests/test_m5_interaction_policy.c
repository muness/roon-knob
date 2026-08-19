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
    return 0;
}
