#pragma once

#include "controller_input.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t pending_bits;
    controller_input_mailbox_stats_t stats;
} controller_input_safety_latch_t;

void controller_input_safety_latch_reset(
    controller_input_safety_latch_t *latch);
bool controller_input_safety_latch_offer(
    controller_input_safety_latch_t *latch,
    uint32_t bit);
bool controller_input_safety_latch_take(
    controller_input_safety_latch_t *latch,
    uint32_t bit);
void controller_input_safety_latch_retry(
    controller_input_safety_latch_t *latch,
    uint32_t bit);

int32_t controller_input_saturating_add(int32_t left, int32_t right);
void controller_input_stats_add_saturating(uint32_t *counter,
                                           uint32_t increment);

#ifdef __cplusplus
}
#endif
