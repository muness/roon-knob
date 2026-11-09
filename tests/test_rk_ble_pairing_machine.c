// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include <assert.h>
#include "rk_ble_pairing_machine.h"

static void both_completion_orders(void) {
    for (int order = 0; order < 2; ++order) {
        rk_pair_machine_t m = {0};
        const rk_pair_event_t first = order ? RK_PAIR_OPEN_OK : RK_PAIR_WORKER_OK;
        const rk_pair_event_t second = order ? RK_PAIR_WORKER_OK : RK_PAIR_OPEN_OK;
        assert(rk_pair_step(&m, RK_PAIR_BEGIN, 4) == RK_PAIR_ACCEPTED);
        assert(rk_pair_step(&m, first, 4) == RK_PAIR_ACCEPTED);
        assert(rk_pair_step(&m, RK_PAIR_LINK_OK, 4) == RK_PAIR_IGNORED);
        assert(rk_pair_step(&m, RK_PAIR_SCAN, 5) == RK_PAIR_IGNORED);
        assert(rk_pair_step(&m, RK_PAIR_BEGIN, 5) == RK_PAIR_IGNORED);
        assert(rk_pair_step(&m, second, 3) == RK_PAIR_IGNORED);
        assert(rk_pair_step(&m, second, 4) == RK_PAIR_VERIFY_LINK);
        assert(rk_pair_step(&m, RK_PAIR_LINK_OK, 4) == RK_PAIR_PUBLISH_CONNECTED);
        assert(rk_pair_step(&m, RK_PAIR_LINK_OK, 4) == RK_PAIR_IGNORED);
        assert(rk_pair_step(&m, RK_PAIR_CLOSED, 4) == RK_PAIR_FINISHED);
    }
}

static void disconnect_during_open(void) {
    rk_pair_machine_t m = {0};
    rk_pair_step(&m, RK_PAIR_BEGIN, 1);
    rk_pair_step(&m, RK_PAIR_OPEN_OK, 1);
    assert(rk_pair_step(&m, RK_PAIR_CLOSED, 1) == RK_PAIR_ACCEPTED);
    assert(rk_pair_draining(&m));
    assert(rk_pair_step(&m, RK_PAIR_BEGIN, 2) == RK_PAIR_IGNORED);
    assert(rk_pair_step(&m, RK_PAIR_WORKER_OK, 1) == RK_PAIR_FINISHED);
    assert(rk_pair_can_start(&m));
}

static void rejected_security_waits_for_close(void) {
    rk_pair_machine_t m = {0};
    rk_pair_step(&m, RK_PAIR_BEGIN, 1);
    rk_pair_step(&m, RK_PAIR_WORKER_OK, 1);
    assert(rk_pair_step(&m, RK_PAIR_OPEN_OK, 1) == RK_PAIR_VERIFY_LINK);
    assert(rk_pair_step(&m, RK_PAIR_LINK_REJECTED, 1) == RK_PAIR_CLOSE_LINK);
    assert(rk_pair_step(&m, RK_PAIR_LINK_OK, 1) == RK_PAIR_IGNORED);
    assert(rk_pair_step(&m, RK_PAIR_BEGIN, 2) == RK_PAIR_IGNORED);
    assert(rk_pair_step(&m, RK_PAIR_CLOSED, 1) == RK_PAIR_FINISHED);
    assert(rk_pair_arm_retry(&m, true) == 0);
    rk_pair_reset_retry(&m);
    assert(rk_pair_arm_retry(&m, true) == 2000);
}

static void cancel_mid_open(void) {
    for (int order = 0; order < 2; ++order) {
        rk_pair_machine_t m = {0};
        rk_pair_step(&m, RK_PAIR_BEGIN, 2);
        rk_pair_step(&m, RK_PAIR_CANCEL, 0);
        if (order) {
            assert(rk_pair_step(&m, RK_PAIR_OPEN_OK, 2) == RK_PAIR_CLOSE_LINK);
            assert(rk_pair_step(&m, RK_PAIR_CLOSED, 2) == RK_PAIR_ACCEPTED);
            assert(rk_pair_draining(&m));
            assert(rk_pair_step(&m, RK_PAIR_WORKER_OK, 2) == RK_PAIR_ACCEPTED);
        } else {
            assert(rk_pair_step(&m, RK_PAIR_WORKER_OK, 2) == RK_PAIR_ACCEPTED);
            assert(rk_pair_step(&m, RK_PAIR_OPEN_OK, 2) == RK_PAIR_CLOSE_LINK);
            assert(rk_pair_draining(&m));
            assert(rk_pair_step(&m, RK_PAIR_CLOSED, 2) == RK_PAIR_ACCEPTED);
        }
        assert(rk_pair_can_start(&m));
    }
}

static void retry_and_scan_races(void) {
    rk_pair_machine_t m = {0};
    assert(rk_pair_arm_retry(&m, false) == 0);
    assert(rk_pair_arm_retry(&m, true) == 2000);
    assert(rk_pair_arm_retry(&m, true) == 0);
    assert(rk_pair_step(&m, RK_PAIR_SCAN, 1) == RK_PAIR_ACCEPTED);
    assert(rk_pair_step(&m, RK_PAIR_RETRY_DUE, 0) == RK_PAIR_IGNORED);
    assert(rk_pair_step(&m, RK_PAIR_SCAN_DONE, 0) == RK_PAIR_IGNORED);
    assert(rk_pair_step(&m, RK_PAIR_SCAN_DONE, 1) == RK_PAIR_FINISHED);
    assert(rk_pair_arm_retry(&m, true) == 4000);
    assert(rk_pair_step(&m, RK_PAIR_RETRY_DUE, 1) == RK_PAIR_ACCEPTED);
    assert(rk_pair_step(&m, RK_PAIR_RETRY_DUE, 1) == RK_PAIR_IGNORED);
    assert(rk_pair_step(&m, RK_PAIR_BEGIN, 2) == RK_PAIR_ACCEPTED);
    assert(rk_pair_step(&m, RK_PAIR_SCAN_DONE, 1) == RK_PAIR_IGNORED);
    assert(rk_pair_step(&m, RK_PAIR_WORKER_FAILED, 2) == RK_PAIR_FINISHED);
    assert(rk_pair_arm_retry(&m, true) == 8000);
    rk_pair_step(&m, RK_PAIR_CANCEL, 0);
    assert(rk_pair_step(&m, RK_PAIR_RETRY_DUE, 2) == RK_PAIR_IGNORED);
    for (unsigned i = 0; i < 300; ++i) {
        const uint32_t expected = i < 4 ? 2000u << i : 30000u;
        assert(rk_pair_arm_retry(&m, true) == expected);
        assert(rk_pair_step(&m, RK_PAIR_RETRY_DUE, 2) == RK_PAIR_ACCEPTED);
    }
    assert(rk_pair_arm_retry(&m, true) == 30000);
}

int main(void) {
    both_completion_orders();
    disconnect_during_open();
    rejected_security_waits_for_close();
    cancel_mid_open();
    retry_and_scan_races();
}
