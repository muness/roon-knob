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
