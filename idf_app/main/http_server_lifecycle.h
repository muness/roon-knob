#pragma once

#include <stdbool.h>

typedef enum {
    HTTP_SERVER_OWNER_NONE = 0,
    HTTP_SERVER_OWNER_CONFIG,
    HTTP_SERVER_OWNER_CAPTIVE_PORTAL,
} http_server_owner_t;

/* The target's two port-80 services share this lifecycle lock.  Functions
 * suffixed `_locked` require the caller to hold it. */
bool http_server_lifecycle_lock(void);
void http_server_lifecycle_unlock(void);
http_server_owner_t http_server_lifecycle_owner_locked(void);
void http_server_lifecycle_claim_locked(http_server_owner_t owner);
void http_server_lifecycle_release_locked(http_server_owner_t owner);
