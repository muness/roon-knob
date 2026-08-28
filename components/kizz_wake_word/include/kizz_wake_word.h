#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*kizz_wake_word_detected_cb_t)(void);

bool kizz_wake_word_start(kizz_wake_word_detected_cb_t detected_cb);
size_t kizz_wake_word_feed(const int16_t *samples, size_t sample_count);
void kizz_wake_word_pause(void);
void kizz_wake_word_resume(void);
const char *kizz_wake_word_runtime_state(void);
uint32_t kizz_wake_word_transition_count(void);
float kizz_wake_word_probability(void);
// Probability captured at the exact detector callback that caused the wake.
// Unlike kizz_wake_word_probability(), this is not the later telemetry peak.
float kizz_wake_word_detection_probability(void);
bool kizz_wake_word_configure(float probability_cutoff,
                              size_t sliding_window);
void kizz_wake_word_get_config(float *probability_cutoff,
                               size_t *sliding_window);

#ifdef __cplusplus
}
#endif
