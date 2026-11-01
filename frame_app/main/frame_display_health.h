// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    char attempted[128], downloaded[128], displayed[128];
    char phase[24], error[64];
    bool power_verified, busy;
    uint32_t attempts, completed, failures;
    uint64_t started_ms, completed_ms, duration_ms;
} frame_display_health_t;
void frame_display_health_attempt(const char *key, uint64_t now);
void frame_display_health_downloaded(const char *key);
void frame_display_health_phase(const char *phase);
void frame_display_health_power(bool verified);
void frame_display_health_fail(const char *error);
void frame_display_health_complete(uint64_t now);
void frame_display_health_snapshot(frame_display_health_t *out);
