#pragma once

#include "controller_action.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void controller_action_router_init(void);
bool controller_action_router_handle(const controller_action_t *action);

#ifdef __cplusplus
}
#endif
