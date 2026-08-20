#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* StickS3's motion control is intentionally an intent-gated rate control,
 * never an absolute orientation-to-volume mapping. */
int32_t m5_interaction_twist_step(bool armed, float gyro_degrees_per_second,
                                  int64_t now_us, int64_t *next_allowed_us);

/* StopWatch wake detection ignores normal sensor noise but responds to a
 * deliberate wrist movement. */
bool m5_interaction_raise_wake(float previous_accel_magnitude,
                               float current_accel_magnitude);

typedef enum {
    M5_POWER_ACTION_NONE = 0,
    M5_POWER_ACTION_DIM,
    M5_POWER_ACTION_CONNECTED_SLEEP,
    M5_POWER_ACTION_POWER_OFF,
} m5_power_action_t;

/* Pure transition policy for the M5 beta power ladder. Timeouts are staged:
 * dim/sleep are measured from local activity, while power-off is measured
 * only after connected display sleep actually begins. An imminent retained-
 * artwork transition may defer panel sleep, but never prevents dimming. */
m5_power_action_t m5_interaction_power_action(
    int64_t now_us, int64_t last_activity_us, int64_t sleep_started_us,
    uint16_t dim_timeout_sec, uint16_t sleep_timeout_sec,
    uint16_t power_off_timeout_sec, bool dimmed, bool sleeping,
    bool inhibited, bool artwork_transition_pending);

#ifdef __cplusplus
}
#endif
