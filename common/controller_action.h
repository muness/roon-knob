#pragma once

#include "controller_command.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONTROLLER_SYSTEM_ACTION_NONE = 0,
    CONTROLLER_SYSTEM_ACTION_START_PROVISIONING,
    CONTROLLER_SYSTEM_ACTION_RESTART,
} controller_system_action_t;

typedef enum {
    CONTROLLER_ACTION_NONE = 0,
    CONTROLLER_ACTION_COMMAND,
    CONTROLLER_ACTION_OPEN_ZONE_PICKER,
    CONTROLLER_ACTION_CLOSE_ZONE_PICKER,
    CONTROLLER_ACTION_SCROLL_ZONE_PICKER,
    CONTROLLER_ACTION_SELECT_ZONE_PICKER,
    CONTROLLER_ACTION_SHOW_SETTINGS,
    CONTROLLER_ACTION_SYSTEM,
    /*
     * Reserved for #170/#194 Slice C. Slice A has no adaptive action table,
     * so the router must reject this kind.
     */
    CONTROLLER_ACTION_ADAPTIVE_REF,
    /*
     * A paired remote's key + gesture, for the bridge to resolve to a Library
     * shortcut on this controller's zone. Semantic input, not a physical
     * event: it enters the router like any other action and is honoured only
     * in the media context.
     */
    CONTROLLER_ACTION_SHORTCUT,
} controller_action_kind_t;

/* Matches controller_shortcut_gesture_t (controller_shortcut_key.h); kept as
 * plain integers so this header stays free of that dependency. */
#define CONTROLLER_SHORTCUT_ACTION_GESTURE_PRESS 1u
#define CONTROLLER_SHORTCUT_ACTION_GESTURE_LONG 2u
#define CONTROLLER_SHORTCUT_ACTION_GESTURE_DOUBLE 3u

typedef struct {
    uint16_t usage;   /* HID Consumer Control usage; never 0 */
    uint8_t gesture;  /* CONTROLLER_SHORTCUT_ACTION_GESTURE_* */
} controller_shortcut_action_t;

typedef struct {
    controller_action_kind_t kind;
    union {
        controller_command_t command;
        int32_t picker_delta;
        controller_system_action_t system;
        uint32_t adaptive_ref;
        controller_shortcut_action_t shortcut;
    } value;
} controller_action_t;

static inline controller_action_t controller_action_none(void) {
    controller_action_t action;
    action.kind = CONTROLLER_ACTION_NONE;
    action.value.command = controller_command_make(CONTROLLER_COMMAND_NONE);
    return action;
}

static inline controller_action_t controller_action_command(
    controller_command_t command) {
    controller_action_t action;
    action.kind = CONTROLLER_ACTION_COMMAND;
    action.value.command = command;
    return action;
}

static inline controller_action_t controller_action_simple(
    controller_action_kind_t kind) {
    controller_action_t action;
    action.kind = kind;
    action.value.picker_delta = 0;
    return action;
}

static inline controller_action_t controller_action_picker_scroll(int delta) {
    controller_action_t action;
    action.kind = CONTROLLER_ACTION_SCROLL_ZONE_PICKER;
    action.value.picker_delta = delta;
    return action;
}

static inline controller_action_t controller_action_shortcut(
    uint16_t usage, uint8_t gesture) {
    controller_action_t action;
    action.kind = CONTROLLER_ACTION_SHORTCUT;
    action.value.command = controller_command_make(CONTROLLER_COMMAND_NONE);
    action.value.shortcut.usage = usage;
    action.value.shortcut.gesture = gesture;
    return action;
}

static inline controller_action_t controller_action_system(
    controller_system_action_t system) {
    controller_action_t action;
    action.kind = CONTROLLER_ACTION_SYSTEM;
    action.value.system = system;
    return action;
}

#if defined(__cplusplus)
static_assert(sizeof(controller_action_t) <= 16,
              "controller action exceeds its ESP32 budget");
#else
_Static_assert(sizeof(controller_action_t) <= 16,
               "controller action exceeds its ESP32 budget");
#endif

#ifdef __cplusplus
}
#endif
