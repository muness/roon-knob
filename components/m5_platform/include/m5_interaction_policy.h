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

#ifdef __cplusplus
}
#endif
