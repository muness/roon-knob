#include "m5_interaction_policy.h"

#include <math.h>

#define TWIST_DEADBAND_DPS 45.0f
#define TWIST_REPEAT_US 140000
#define RAISE_DELTA_G 0.18f

int32_t m5_interaction_twist_step(bool armed, float gyro_degrees_per_second,
                                  int64_t now_us, int64_t *next_allowed_us) {
    if (!armed || !next_allowed_us || now_us < *next_allowed_us ||
        fabsf(gyro_degrees_per_second) <= TWIST_DEADBAND_DPS) {
        return 0;
    }
    *next_allowed_us = now_us + TWIST_REPEAT_US;
    return gyro_degrees_per_second > 0 ? 1 : -1;
}

bool m5_interaction_raise_wake(float previous_accel_magnitude,
                               float current_accel_magnitude) {
    return fabsf(current_accel_magnitude - previous_accel_magnitude) >
           RAISE_DELTA_G;
}

static bool timeout_elapsed(int64_t now_us, int64_t since_us,
                            uint16_t timeout_sec) {
    return timeout_sec > 0 && since_us > 0 && now_us >= since_us &&
           now_us - since_us >= (int64_t)timeout_sec * 1000000;
}

m5_power_action_t m5_interaction_power_action(
    int64_t now_us, int64_t last_activity_us, int64_t sleep_started_us,
    uint16_t dim_timeout_sec, uint16_t sleep_timeout_sec,
    uint16_t power_off_timeout_sec, bool dimmed, bool sleeping,
    bool inhibited, bool artwork_transition_pending) {
    if (inhibited) return M5_POWER_ACTION_NONE;

    if (sleeping) {
        return timeout_elapsed(now_us, sleep_started_us,
                               power_off_timeout_sec)
                   ? M5_POWER_ACTION_POWER_OFF
                   : M5_POWER_ACTION_NONE;
    }

    if (!artwork_transition_pending &&
        timeout_elapsed(now_us, last_activity_us, sleep_timeout_sec)) {
        return M5_POWER_ACTION_CONNECTED_SLEEP;
    }
    if (!dimmed && timeout_elapsed(now_us, last_activity_us,
                                   dim_timeout_sec)) {
        return M5_POWER_ACTION_DIM;
    }
    return M5_POWER_ACTION_NONE;
}
