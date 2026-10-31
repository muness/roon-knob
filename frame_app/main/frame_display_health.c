// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "frame_display_health.h"
#include "os_critical.h"
#include <stdio.h>
#include <string.h>
static os_critical_t lock = OS_CRITICAL_INITIALIZER;
static frame_display_health_t state;
void frame_display_health_attempt(const char *key, uint64_t now) {
    os_critical_enter(&lock);
    snprintf(state.attempted, sizeof(state.attempted), "%s", key ? key : "");
    snprintf(state.phase, sizeof(state.phase), "download");
    state.error[0] = 0; state.busy = true; state.started_ms = now; state.attempts++;
    os_critical_leave(&lock);
}
void frame_display_health_downloaded(const char *key) {
    os_critical_enter(&lock);
    snprintf(state.downloaded, sizeof(state.downloaded), "%s", key ? key : "");
    os_critical_leave(&lock);
}
void frame_display_health_phase(const char *phase) {
    os_critical_enter(&lock);
    snprintf(state.phase, sizeof(state.phase), "%s", phase);
    os_critical_leave(&lock);
}
void frame_display_health_power(bool verified) {
    os_critical_enter(&lock); state.power_verified = verified; os_critical_leave(&lock);
}
void frame_display_health_fail(const char *error) {
    os_critical_enter(&lock);
    snprintf(state.error, sizeof(state.error), "%s", error);
    state.failures++; state.busy = false;
    os_critical_leave(&lock);
}
void frame_display_health_complete(uint64_t now) {
    os_critical_enter(&lock);
    if (state.busy && !state.error[0] && state.power_verified) {
        snprintf(state.displayed, sizeof(state.displayed), "%s", state.attempted);
        snprintf(state.phase, sizeof(state.phase), "complete");
        state.completed++; state.completed_ms = now;
        state.duration_ms = now - state.started_ms; state.busy = false;
    }
    os_critical_leave(&lock);
}
void frame_display_health_snapshot(frame_display_health_t *out) {
    os_critical_enter(&lock); *out = state; os_critical_leave(&lock);
}
