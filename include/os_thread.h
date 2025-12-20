#pragma once

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef TaskHandle_t os_thread_t;
typedef void (*os_thread_func_t)(void *);

static inline int os_thread_create(os_thread_t *thread, os_thread_func_t func, void *arg) {
    BaseType_t ret = xTaskCreate(
        (TaskFunction_t)func,
        "task",
        8192,
        arg,
        5,
        thread
    );
    return ret == pdPASS ? 0 : -1;
}

static inline int os_thread_join(os_thread_t thread) {
    while (eTaskGetState(thread) != eDeleted) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return 0;
}

#else
#include <pthread.h>
#include <stdlib.h>

typedef pthread_t os_thread_t;
typedef void (*os_thread_func_t)(void *);

struct os_thread_start_ctx {
    os_thread_func_t fn;
    void *arg;
};

static inline void *os_thread_trampoline(void *ctx) {
    struct os_thread_start_ctx start = *(struct os_thread_start_ctx *)ctx;
    free(ctx);
    start.fn(start.arg);
    return NULL;
}

static inline int os_thread_create(os_thread_t *thread, os_thread_func_t func, void *arg) {
    struct os_thread_start_ctx *ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -1;
    }
    ctx->fn = func;
    ctx->arg = arg;
    int ret = pthread_create(thread, NULL, os_thread_trampoline, ctx);
    if (ret != 0) {
        free(ctx);
    }
    return ret;
}

static inline int os_thread_join(os_thread_t thread) {
    return pthread_join(thread, NULL);
}

#endif
