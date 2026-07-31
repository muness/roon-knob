#include "http_server_lifecycle.h"

#include "os_mutex.h"

static os_mutex_t s_lifecycle_lock = OS_MUTEX_INITIALIZER;
static http_server_owner_t s_owner = HTTP_SERVER_OWNER_NONE;

#ifdef ESP_PLATFORM
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_lifecycle_lock_storage;

static bool ensure_lifecycle_lock(void) {
    bool ready;
    taskENTER_CRITICAL(&s_init_lock);
    if (s_lifecycle_lock == NULL) {
        s_lifecycle_lock = xSemaphoreCreateMutexStatic(
            &s_lifecycle_lock_storage);
    }
    ready = s_lifecycle_lock != NULL;
    taskEXIT_CRITICAL(&s_init_lock);
    return ready;
}
#else
static bool ensure_lifecycle_lock(void) {
    return true;
}
#endif

bool http_server_lifecycle_lock(void) {
    return ensure_lifecycle_lock() && os_mutex_lock(&s_lifecycle_lock) == 0;
}

void http_server_lifecycle_unlock(void) {
    (void)os_mutex_unlock(&s_lifecycle_lock);
}

http_server_owner_t http_server_lifecycle_owner_locked(void) {
    return s_owner;
}

void http_server_lifecycle_claim_locked(http_server_owner_t owner) {
    s_owner = owner;
}

void http_server_lifecycle_release_locked(http_server_owner_t owner) {
    if (s_owner == owner) {
        s_owner = HTTP_SERVER_OWNER_NONE;
    }
}
