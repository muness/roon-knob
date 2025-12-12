/**
 * @file controller_mode.c
 * @brief Controller mode abstraction implementation
 */

#include "controller_mode.h"
#include "platform/platform_log.h"

#include <string.h>

/* We use the zone_id to implicitly determine mode:
 * - zone_id == ZONE_ID_BLUETOOTH -> Bluetooth mode
 * - zone_id == anything else -> Roon mode
 *
 * This avoids needing a separate mode field in the config struct.
 * The zone picker naturally handles mode switching.
 */

static controller_mode_t s_current_mode = CONTROLLER_MODE_ROON;
static controller_mode_change_cb_t s_callback = NULL;
static void *s_callback_user_data = NULL;

void controller_mode_init(void) {
    /* Mode is determined by zone_id, which is loaded by the caller.
     * This function exists for future expansion if needed. */
    LOGI("[ctrl_mode] Controller mode module initialized");
}

controller_mode_t controller_mode_get(void) {
    return s_current_mode;
}

bool controller_mode_set(controller_mode_t mode) {
    if (mode == s_current_mode) {
        return false;  /* Already in this mode */
    }

    controller_mode_t old_mode = s_current_mode;
    s_current_mode = mode;

    LOGI("[ctrl_mode] Mode changed: %s -> %s",
         controller_mode_name(old_mode),
         controller_mode_name(mode));

    /* Notify callback if registered */
    if (s_callback) {
        s_callback(mode, s_callback_user_data);
    }

    return true;
}

bool controller_mode_bluetooth_available(void) {
#ifdef CONFIG_ROON_KNOB_BLUETOOTH_MODE
    return true;
#else
    return false;
#endif
}

void controller_mode_register_callback(controller_mode_change_cb_t callback, void *user_data) {
    s_callback = callback;
    s_callback_user_data = user_data;
}

const char *controller_mode_name(controller_mode_t mode) {
    switch (mode) {
        case CONTROLLER_MODE_ROON:
            return "Roon";
        case CONTROLLER_MODE_BLUETOOTH:
            return "Bluetooth";
        default:
            return "Unknown";
    }
}

bool controller_mode_is_bluetooth_zone(const char *zone_id) {
    if (!zone_id) {
        return false;
    }
    return strcmp(zone_id, ZONE_ID_BLUETOOTH) == 0;
}
