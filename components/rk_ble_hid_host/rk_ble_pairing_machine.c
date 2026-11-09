// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "rk_ble_pairing_machine.h"

bool rk_pair_can_start(const rk_pair_machine_t *m) {
    return m->phase == RK_PAIR_IDLE;
}

bool rk_pair_draining(const rk_pair_machine_t *m) {
    return m->phase != RK_PAIR_IDLE && m->phase != RK_PAIR_SCANNING &&
           m->phase != RK_PAIR_CONNECTED;
}

uint32_t rk_pair_arm_retry(rk_pair_machine_t *m, bool eligible) {
    if (!eligible || !rk_pair_can_start(m) || m->retry_armed || m->retry_blocked) return 0;
    const uint8_t attempt = m->retry_attempts;
    if (m->retry_attempts < 4) ++m->retry_attempts;
    m->retry_armed = true;
    return attempt >= 4 ? 30000 : (2000u << attempt);
}

void rk_pair_reset_retry(rk_pair_machine_t *m) {
    m->retry_attempts = 0;
    m->retry_blocked = false;
    m->retry_armed = false;
}

static rk_pair_effect_t finish(rk_pair_machine_t *m) {
    m->phase = RK_PAIR_IDLE;
    return m->cancelled ? RK_PAIR_ACCEPTED : RK_PAIR_FINISHED;
}

static rk_pair_effect_t verify(rk_pair_machine_t *m) {
    if (m->cancelled) return finish(m);
    m->phase = RK_PAIR_VERIFYING;
    return RK_PAIR_VERIFY_LINK;
}

rk_pair_effect_t rk_pair_step(rk_pair_machine_t *m, rk_pair_event_t event,
                              uint32_t generation) {
    if (event == RK_PAIR_CANCEL) {
        m->cancelled = true;
        rk_pair_reset_retry(m);
        if (m->phase == RK_PAIR_SCANNING) return finish(m);
        return RK_PAIR_ACCEPTED;
    }
    if (event == RK_PAIR_SCAN || event == RK_PAIR_BEGIN) {
        if (!rk_pair_can_start(m)) return RK_PAIR_IGNORED;
        m->phase = event == RK_PAIR_SCAN ? RK_PAIR_SCANNING : RK_PAIR_WAIT_BOTH;
        m->generation = generation;
        m->cancelled = false;
        m->retry_armed = false;
        return RK_PAIR_ACCEPTED;
    }
    if (generation != m->generation) return RK_PAIR_IGNORED;
    if (event == RK_PAIR_RETRY_DUE && m->retry_armed && rk_pair_can_start(m)) {
        m->retry_armed = false;
        return RK_PAIR_ACCEPTED;
    }

    switch (m->phase) {
    case RK_PAIR_SCANNING:
        if (event == RK_PAIR_SCAN_DONE) return finish(m);
        break;
    case RK_PAIR_WAIT_BOTH:
        if (event == RK_PAIR_OPEN_OK) {
            m->phase = RK_PAIR_WAIT_WORKER;
            return m->cancelled ? RK_PAIR_CLOSE_LINK : RK_PAIR_ACCEPTED;
        }
        if (event == RK_PAIR_OPEN_FAILED) {
            m->phase = RK_PAIR_DRAIN_WORKER;
            return RK_PAIR_ACCEPTED;
        }
        if (event == RK_PAIR_WORKER_OK) {
            m->phase = RK_PAIR_WAIT_OPEN;
            return RK_PAIR_ACCEPTED;
        }
        if (event == RK_PAIR_WORKER_FAILED) return finish(m);
        break;
    case RK_PAIR_WAIT_OPEN:
        if (event == RK_PAIR_OPEN_OK) {
            if (m->cancelled) {
                m->phase = RK_PAIR_WAIT_CLOSE;
                return RK_PAIR_CLOSE_LINK;
            }
            return verify(m);
        }
        if (event == RK_PAIR_OPEN_FAILED) return finish(m);
        break;
    case RK_PAIR_WAIT_WORKER:
        if (event == RK_PAIR_WORKER_OK && !m->cancelled) return verify(m);
        if (event == RK_PAIR_WORKER_OK || event == RK_PAIR_WORKER_FAILED) {
            m->phase = RK_PAIR_WAIT_CLOSE;
            return RK_PAIR_CLOSE_LINK;
        }
        if (event == RK_PAIR_CLOSED) {
            m->phase = RK_PAIR_DRAIN_WORKER;
            return RK_PAIR_ACCEPTED;
        }
        break;
    case RK_PAIR_DRAIN_WORKER:
        if (event == RK_PAIR_WORKER_OK || event == RK_PAIR_WORKER_FAILED) return finish(m);
        break;
    case RK_PAIR_VERIFYING:
        if (event == RK_PAIR_LINK_OK && !m->cancelled) {
            m->phase = RK_PAIR_CONNECTED;
            return RK_PAIR_PUBLISH_CONNECTED;
        }
        if (event == RK_PAIR_LINK_FAILED || event == RK_PAIR_LINK_REJECTED ||
            event == RK_PAIR_LINK_OK) {
            m->retry_blocked = event == RK_PAIR_LINK_REJECTED;
            m->phase = RK_PAIR_WAIT_CLOSE;
            return RK_PAIR_CLOSE_LINK;
        }
        if (event == RK_PAIR_CLOSED) return finish(m);
        break;
    case RK_PAIR_CONNECTED:
    case RK_PAIR_WAIT_CLOSE:
        if (event == RK_PAIR_CLOSED) return finish(m);
        break;
    case RK_PAIR_IDLE:
        break;
    }
    return RK_PAIR_IGNORED;
}
