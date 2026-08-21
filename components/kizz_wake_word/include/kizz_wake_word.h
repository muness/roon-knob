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

#ifdef __cplusplus
}
#endif
