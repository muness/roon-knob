#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "os_thread.h"

typedef void (*platform_task_fn_t)(void *arg);

void platform_task_init(void);
int platform_task_start(platform_task_fn_t fn, void *arg);
int platform_task_start_configured(const char *name, uint32_t stack_size,
                                   platform_task_fn_t fn, void *arg);
/* For background work that never writes flash/NVS and never runs while the
 * external-memory cache is disabled. The TCB remains in internal RAM. */
int platform_task_start_external_stack(const char *name, uint32_t stack_size,
                                       platform_task_fn_t fn, void *arg);
size_t platform_task_current_stack_free_bytes(void);
size_t platform_task_internal_heap_free_bytes(void);
size_t platform_task_internal_heap_largest_free_block_bytes(void);
bool platform_task_post_to_ui(platform_task_fn_t fn, void *arg);
void platform_task_run_pending(void);
