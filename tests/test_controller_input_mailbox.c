#include "controller_input_mailbox.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

static void test_saturating_rotation_coalescing(void) {
    assert(controller_input_saturating_add(2, -1) == 1);
    assert(controller_input_saturating_add(-2, 1) == -1);
    assert(controller_input_saturating_add(7, -7) == 0);
    assert(controller_input_saturating_add(INT32_MAX, 1) == INT32_MAX);
    assert(controller_input_saturating_add(INT32_MIN, -1) == INT32_MIN);
    assert(controller_input_saturating_add(INT32_MAX, INT32_MIN) == -1);
}

static void test_safety_latch_saturation_and_retry(void) {
    controller_input_safety_latch_t latch;
    controller_input_safety_latch_reset(&latch);

    assert(!controller_input_safety_latch_offer(NULL, 1));
    assert(!controller_input_safety_latch_offer(&latch, 0));
    assert(!controller_input_safety_latch_offer(&latch, 3));

    assert(controller_input_safety_latch_offer(&latch, 1));
    assert(controller_input_safety_latch_offer(&latch, 1));
    assert(controller_input_safety_latch_offer(&latch, 2));
    assert(latch.pending_bits == 3);
    assert(latch.stats.accepted == 2);
    assert(latch.stats.coalesced == 1);
    assert(latch.stats.dropped == 0);
    assert(latch.stats.overflow == 0);

    assert(controller_input_safety_latch_take(&latch, 1));
    assert(!controller_input_safety_latch_take(&latch, 3));
    assert(!controller_input_safety_latch_take(&latch, 1));
    assert(latch.pending_bits == 2);

    /* Failed dispatch re-arms without manufacturing a new accepted event. */
    controller_input_safety_latch_retry(&latch, 1);
    controller_input_safety_latch_retry(&latch, 3);
    assert(latch.pending_bits == 3);
    assert(latch.stats.accepted == 2);
    assert(controller_input_safety_latch_take(&latch, 1));
    assert(controller_input_safety_latch_take(&latch, 2));
    assert(latch.pending_bits == 0);

    latch.stats.accepted = UINT32_MAX - 1;
    controller_input_stats_add_saturating(&latch.stats.accepted, 5);
    assert(latch.stats.accepted == UINT32_MAX);
}

int main(void) {
    test_saturating_rotation_coalescing();
    test_safety_latch_saturation_and_retry();
    puts("controller input mailbox contracts passed");
    return 0;
}
