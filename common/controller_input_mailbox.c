#include "controller_input_mailbox.h"

#include <limits.h>

void controller_input_stats_add_saturating(uint32_t *counter,
                                           uint32_t increment) {
    if (!counter || increment == 0) {
        return;
    }
    if (*counter > UINT32_MAX - increment) {
        *counter = UINT32_MAX;
    } else {
        *counter += increment;
    }
}

void controller_input_safety_latch_reset(
    controller_input_safety_latch_t *latch) {
    if (!latch) {
        return;
    }
    *latch = (controller_input_safety_latch_t){0};
}

bool controller_input_safety_latch_offer(
    controller_input_safety_latch_t *latch,
    uint32_t bit) {
    if (!latch || bit == 0 || (bit & (bit - 1)) != 0) {
        return false;
    }
    if ((latch->pending_bits & bit) != 0) {
        controller_input_stats_add_saturating(&latch->stats.coalesced, 1);
    } else {
        latch->pending_bits |= bit;
        controller_input_stats_add_saturating(&latch->stats.accepted, 1);
    }
    return true;
}

bool controller_input_safety_latch_take(
    controller_input_safety_latch_t *latch,
    uint32_t bit) {
    if (!latch || bit == 0 || (bit & (bit - 1)) != 0 ||
        (latch->pending_bits & bit) == 0) {
        return false;
    }
    latch->pending_bits &= ~bit;
    return true;
}

void controller_input_safety_latch_retry(
    controller_input_safety_latch_t *latch,
    uint32_t bit) {
    if (!latch || bit == 0 || (bit & (bit - 1)) != 0) {
        return;
    }
    latch->pending_bits |= bit;
}

int32_t controller_input_saturating_add(int32_t left, int32_t right) {
    int64_t sum = (int64_t)left + (int64_t)right;
    if (sum > INT32_MAX) {
        return INT32_MAX;
    }
    if (sum < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)sum;
}
