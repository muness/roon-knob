#include "platform/platform_task.h"
#include "os_mutex.h"

#include <stdbool.h>
#include <stddef.h>

#define UI_TASK_QUEUE_SIZE 64

static os_mutex_t s_ui_mutex = OS_MUTEX_INITIALIZER;

typedef struct {
    platform_task_fn_t fn;
    void *arg;
} ui_task_t;

static ui_task_t s_ui_queue[UI_TASK_QUEUE_SIZE];
static size_t s_head;
static size_t s_tail;
static bool s_initialized;

void platform_task_init(void) {
    if (s_initialized) {
        return;
    }
    s_head = 0;
    s_tail = 0;
    s_initialized = true;
}

int platform_task_start(platform_task_fn_t fn, void *arg) {
    return platform_task_start_configured("task", 8192, fn, arg);
}

int platform_task_start_configured(const char *name, uint32_t stack_size,
                                   platform_task_fn_t fn, void *arg) {
    if (!name || name[0] == '\0' || stack_size == 0 || !fn) {
        return -1;
    }
    os_thread_t thread;
    int err = os_thread_create_configured(&thread, fn, arg, name, stack_size);
    (void)thread;
    return err;
}

int platform_task_start_external_stack(const char *name, uint32_t stack_size,
                                       platform_task_fn_t fn, void *arg) {
    if (!name || name[0] == '\0' || stack_size == 0 || !fn) {
        return -1;
    }
    os_thread_t thread;
    int err = os_thread_create_external_stack(&thread, fn, arg, name,
                                              stack_size);
    (void)thread;
    return err;
}

size_t platform_task_current_stack_free_bytes(void) {
    return os_thread_current_stack_free_bytes();
}

size_t platform_task_internal_heap_free_bytes(void) {
    return os_internal_heap_free_bytes();
}

size_t platform_task_internal_heap_largest_free_block_bytes(void) {
    return os_internal_heap_largest_free_block_bytes();
}

static bool ui_queue_push(platform_task_fn_t fn, void *arg) {
    if (!fn) {
        return false;
    }
    os_mutex_lock(&s_ui_mutex);
    size_t next = (s_tail + 1) % UI_TASK_QUEUE_SIZE;
    if (next == s_head) {
        os_mutex_unlock(&s_ui_mutex);
        return false;
    }
    s_ui_queue[s_tail].fn = fn;
    s_ui_queue[s_tail].arg = arg;
    s_tail = next;
    os_mutex_unlock(&s_ui_mutex);
    return true;
}

bool platform_task_post_to_ui(platform_task_fn_t fn, void *arg) {
    if (!s_initialized) {
        platform_task_init();
    }
    return ui_queue_push(fn, arg);
}

void platform_task_run_pending(void) {
    if (!s_initialized) {
        return;
    }
    while (true) {
        os_mutex_lock(&s_ui_mutex);
        if (s_head == s_tail) {
            os_mutex_unlock(&s_ui_mutex);
            break;
        }
        ui_task_t task = s_ui_queue[s_head];
        s_head = (s_head + 1) % UI_TASK_QUEUE_SIZE;
        os_mutex_unlock(&s_ui_mutex);
        if (task.fn) {
            task.fn(task.arg);
        }
    }
}
