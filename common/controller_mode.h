/**
 * @file controller_mode.h
 * @brief Controller mode abstraction for switching between Roon and Bluetooth modes
 *
 * This module manages the active controller mode (Roon WiFi or Bluetooth HID)
 * and provides callbacks for mode change notifications.
 */

#ifndef CONTROLLER_MODE_H
#define CONTROLLER_MODE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Controller mode enum
 */
typedef enum {
    CONTROLLER_MODE_ROON,      /**< WiFi + HTTP to Roon bridge (bidirectional) */
    CONTROLLER_MODE_BLUETOOTH  /**< BLE HID to any device (send-only) */
} controller_mode_t;

/**
 * @brief Mode change callback type
 * @param new_mode The mode being switched to
 * @param user_data User-provided context
 */
typedef void (*controller_mode_change_cb_t)(controller_mode_t new_mode, void *user_data);

/**
 * @brief Initialize controller mode module
 *
 * Loads saved mode from NVS. If no saved mode, defaults to ROON.
 */
void controller_mode_init(void);

/**
 * @brief Get current controller mode
 * @return Current mode (ROON or BLUETOOTH)
 */
controller_mode_t controller_mode_get(void);

/**
 * @brief Set controller mode
 *
 * Changes the active mode and persists to NVS.
 * If mode change callbacks are registered, they will be called.
 *
 * @param mode New mode to set
 * @return true if mode was changed, false if already in that mode
 */
bool controller_mode_set(controller_mode_t mode);

/**
 * @brief Check if Bluetooth mode is available
 *
 * Returns false if BLE HID support was not compiled in.
 *
 * @return true if Bluetooth mode can be enabled
 */
bool controller_mode_bluetooth_available(void);

/**
 * @brief Register callback for mode changes
 *
 * @param callback Function to call when mode changes
 * @param user_data Context passed to callback
 */
void controller_mode_register_callback(controller_mode_change_cb_t callback, void *user_data);

/**
 * @brief Get mode name as string
 * @param mode Mode to get name for
 * @return Human-readable mode name
 */
const char *controller_mode_name(controller_mode_t mode);

/**
 * @brief Special zone ID that represents Bluetooth mode
 *
 * When this zone_id is selected in the zone picker, switch to Bluetooth mode.
 */
#define ZONE_ID_BLUETOOTH "__bluetooth__"

/**
 * @brief Check if a zone_id represents Bluetooth mode
 * @param zone_id Zone ID to check
 * @return true if this is the special Bluetooth zone
 */
bool controller_mode_is_bluetooth_zone(const char *zone_id);

#ifdef __cplusplus
}
#endif

#endif /* CONTROLLER_MODE_H */
