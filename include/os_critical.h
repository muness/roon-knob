// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once
/* Short, statically initialized bookkeeping lock. Never hold across allocation,
 * task creation, another blocking lock, HTTP, flash, or a callback. */
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
typedef portMUX_TYPE os_critical_t;
#define OS_CRITICAL_INITIALIZER portMUX_INITIALIZER_UNLOCKED
static inline void os_critical_enter(os_critical_t *lock) { portENTER_CRITICAL(lock); }
static inline void os_critical_leave(os_critical_t *lock) { portEXIT_CRITICAL(lock); }
#else
#include <pthread.h>
typedef pthread_mutex_t os_critical_t;
#define OS_CRITICAL_INITIALIZER PTHREAD_MUTEX_INITIALIZER
static inline void os_critical_enter(os_critical_t *lock) { (void)pthread_mutex_lock(lock); }
static inline void os_critical_leave(os_critical_t *lock) { (void)pthread_mutex_unlock(lock); }
#endif
