#include "controller_system_frame.h"

#include "wifi_manager.h"

#if defined(CONTROLLER_SYSTEM_FRAME_TEST)
void esp_restart(void);
#define FRAME_SYSTEM_LOGW(tag, ...) ((void)(tag))
#else
#include <esp_log.h>
#include <esp_system.h>
#define FRAME_SYSTEM_LOGW ESP_LOGW
#endif

static const char *TAG = "controller_system";

#if !defined(CONTROLLER_SYSTEM_FRAME_TEST)
typedef struct {
    bool (*start_provisioning)(void);
    void (*restart)(void);
} controller_system_frame_effects_t;
#endif

static void restart_frame(void) {
    esp_restart();
}

static const controller_system_frame_effects_t s_default_effects = {
    .start_provisioning = wifi_mgr_start_provisioning,
    .restart = restart_frame,
};
static const controller_system_frame_effects_t *s_effects = &s_default_effects;

static bool execute_system_action(controller_system_action_t action) {
    if (!s_effects) {
        return false;
    }
    switch (action) {
    case CONTROLLER_SYSTEM_ACTION_START_PROVISIONING:
        FRAME_SYSTEM_LOGW(TAG, "BOOT long press — starting WiFi provisioning");
        return s_effects->start_provisioning &&
               s_effects->start_provisioning();
    case CONTROLLER_SYSTEM_ACTION_RESTART:
        FRAME_SYSTEM_LOGW(TAG, "GP4 long press — restarting");
        if (!s_effects->restart) {
            return false;
        }
        s_effects->restart();
        return true;
    case CONTROLLER_SYSTEM_ACTION_NONE:
    default:
        return false;
    }
}

void controller_system_frame_init(void) {
    s_effects = &s_default_effects;
}

void controller_system_frame_shutdown(void) {
    s_effects = NULL;
}

bool controller_system_frame_dispatch_physical(
    const controller_physical_event_t *event) {
    controller_action_t action = controller_action_none();
    if (!controller_input_resolve_physical(
            event, controller_input_get_context(), &action) ||
        action.kind != CONTROLLER_ACTION_SYSTEM) {
        return false;
    }
    return execute_system_action(action.value.system);
}

#if defined(CONTROLLER_SYSTEM_FRAME_TEST)
void controller_system_frame_set_effects_for_test(
    const controller_system_frame_effects_t *effects) {
    s_effects = effects ? effects : &s_default_effects;
}
#endif
