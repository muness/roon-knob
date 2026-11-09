// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The HID worker and the IDF event loop can report completion in either
 * order. Keep the attempt reserved until both have relinquished it. */
typedef enum {
    RK_PAIR_IDLE,
    RK_PAIR_SCANNING,
    RK_PAIR_WAIT_BOTH,
    RK_PAIR_WAIT_OPEN,
    RK_PAIR_WAIT_WORKER,
    RK_PAIR_DRAIN_WORKER,
    RK_PAIR_VERIFYING,
    RK_PAIR_CONNECTED,
    RK_PAIR_WAIT_CLOSE,
} rk_pair_phase_t;

typedef enum {
    RK_PAIR_SCAN,
    RK_PAIR_SCAN_DONE,
    RK_PAIR_BEGIN,
    RK_PAIR_RETRY_DUE,
    RK_PAIR_OPEN_OK,
    RK_PAIR_OPEN_FAILED,
    RK_PAIR_WORKER_OK,
    RK_PAIR_WORKER_FAILED,
    RK_PAIR_LINK_OK,
    RK_PAIR_LINK_FAILED,
    RK_PAIR_LINK_REJECTED,
    RK_PAIR_CLOSED,
    RK_PAIR_CANCEL,
} rk_pair_event_t;

typedef enum {
    RK_PAIR_IGNORED,
    RK_PAIR_ACCEPTED,
    RK_PAIR_VERIFY_LINK,
    RK_PAIR_CLOSE_LINK,
    RK_PAIR_PUBLISH_CONNECTED,
    RK_PAIR_FINISHED,
} rk_pair_effect_t;

typedef struct {
    rk_pair_phase_t phase;
    uint32_t generation;
    bool cancelled;
    bool retry_armed;
    bool retry_blocked;
    uint8_t retry_attempts;
} rk_pair_machine_t;

rk_pair_effect_t rk_pair_step(rk_pair_machine_t *m, rk_pair_event_t event,
                              uint32_t generation);
bool rk_pair_can_start(const rk_pair_machine_t *m);
bool rk_pair_draining(const rk_pair_machine_t *m);
/* Returns the delay in ms, or zero when no retry should be armed. */
uint32_t rk_pair_arm_retry(rk_pair_machine_t *m, bool eligible);
void rk_pair_reset_retry(rk_pair_machine_t *m);
