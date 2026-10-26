// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

/* Device-specific settings the bridge may serve in the `config` object of GET /config/<knob_id>.
 * Every key is optional. A missing, null, wrong-typed or out-of-range value is "absent" and means
 * "leave the device's local value alone"; it is never clamped and never fails the config parse.
 *
 *   show_ip        bool          Frame only
 *   brightness     int 5..100    Dial and HALO only
 *   body_enabled   bool          StackChan only
 *   sound_enabled  bool          StackChan only
 *   sound_volume   int 0..2      StackChan only (0 Low, 1 Medium, 2 High)
 *
 * These are intentionally not part of rk_cfg_t (a versioned NVS blob); each target applies them
 * through its own existing preference store via platform_device_settings_apply(). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#define CONTROLLER_DEVICE_BRIGHTNESS_MIN 5
#define CONTROLLER_DEVICE_BRIGHTNESS_MAX 100
#define CONTROLLER_DEVICE_SOUND_VOLUME_MAX 2
#define CONTROLLER_FIRMWARE_VERSION_MAX 32 /* including the terminator */

enum {
    CDS_SHOW_IP = 1u << 0,
    CDS_BRIGHTNESS = 1u << 1,
    CDS_BODY_ENABLED = 1u << 2,
    CDS_SOUND_ENABLED = 1u << 3,
    CDS_SOUND_VOLUME = 1u << 4,
};

typedef struct {
    uint32_t present; /* CDS_* mask: only these fields carry a valid value */
    bool show_ip;
    uint8_t brightness;
    bool body_enabled;
    bool sound_enabled;
    uint8_t sound_volume;
} controller_device_settings_t;

/* Parse the optional keys from a cJSON `config` object. Always initialises *out. Returns whether
 * at least one valid value is present. */
bool controller_device_settings_parse(const cJSON *config, controller_device_settings_t *out);

/* Apply parsed settings through the platform hook. Returns true when they were delivered or there
 * was nothing to deliver, false when the target could not store or queue them. */
bool controller_device_settings_apply(const controller_device_settings_t *settings);

/* The bridge serves a config hash once: after the device accepts it, the same values are never
 * fetched again. So a change the target failed to apply is kept here and retried, instead of being
 * acknowledged and lost. submit() applies now and keeps the values pending on failure (replacing any
 * older pending set); retry_pending() is called from the bridge poll loop and gives up after
 * CONTROLLER_DEVICE_SETTINGS_MAX_RETRIES failed attempts so a persistent fault is not replayed
 * forever. Single-threaded: both run on the bridge worker. */
#define CONTROLLER_DEVICE_SETTINGS_MAX_RETRIES 5
void controller_device_settings_submit(const controller_device_settings_t *settings);
void controller_device_settings_retry_pending(void);

/* Platform hook, defined once per target. Called only with at least one present, valid field.
 * Returns true when every present field that applies to the device is now in effect (or already
 * was), false when a store or queue failed and the call should be retried. It must be idempotent (the config is re-fetched), must write nothing when a value is unchanged,
 * and must ignore fields that do not apply to the device. Targets with no such setting link
 * common/platform/platform_device_settings_none.c. It is deliberately not a weak symbol: a weak
 * default in a static archive can shadow the real definition. */
bool platform_device_settings_apply(const controller_device_settings_t *settings);

/* Parse `config.firmware_update`. Returns true only for
 * {"action":"install","from_version":"<string>"}; the version is copied to from_version (which may
 * be empty). Any other shape, or a version that does not fit, yields false and an empty string. */
bool controller_firmware_update_parse(const cJSON *config, char *from_version, size_t capacity);
