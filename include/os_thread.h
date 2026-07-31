#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

typedef TaskHandle_t os_thread_t;
typedef void (*os_thread_func_t)(void *);

static inline int os_thread_create_configured(os_thread_t *thread,
                                               os_thread_func_t func,
                                               void *arg,
                                               const char *name,
                                               uint32_t stack_size) {
    BaseType_t ret = xTaskCreate(
        (TaskFunction_t)func,
        name,
        stack_size,
        arg,
        5,
        thread
    );
    return ret == pdPASS ? 0 : -1;
}

static inline int os_thread_create_external_stack(os_thread_t *thread,
                                                   os_thread_func_t func,
                                                   void *arg,
                                                   const char *name,
                                                   uint32_t stack_size) {
    BaseType_t ret = xTaskCreateWithCaps(
        (TaskFunction_t)func,
        name,
        stack_size,
        arg,
        5,
        thread,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    return ret == pdPASS ? 0 : -1;
}

static inline int os_thread_create(os_thread_t *thread, os_thread_func_t func,
                                   void *arg) {
    return os_thread_create_configured(thread, func, arg, "task", 8192);
}

static inline size_t os_thread_current_stack_free_bytes(void) {
    return (size_t)uxTaskGetStackHighWaterMark(NULL);
}

static inline size_t os_internal_heap_free_bytes(void) {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static inline size_t os_internal_heap_largest_free_block_bytes(void) {
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                            MALLOC_CAP_8BIT);
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

static inline int os_thread_create_configured(os_thread_t *thread,
                                               os_thread_func_t func,
                                               void *arg,
                                               const char *name,
                                               uint32_t stack_size) {
    (void)name;
    (void)stack_size;
    return os_thread_create(thread, func, arg);
}


static inline int os_thread_create_external_stack(os_thread_t *thread,
                                                   os_thread_func_t func,
                                                   void *arg,
                                                   const char *name,
                                                   uint32_t stack_size) {
    return os_thread_create_configured(thread, func, arg, name, stack_size);
}

static inline size_t os_thread_current_stack_free_bytes(void) {
    return SIZE_MAX;
}

static inline size_t os_internal_heap_free_bytes(void) {
    return SIZE_MAX;
}

static inline size_t os_internal_heap_largest_free_block_bytes(void) {
    return SIZE_MAX;
}

static inline int os_thread_join(os_thread_t thread) {
    return pthread_join(thread, NULL);
}

#endif
