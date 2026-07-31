#pragma once

#include "controller_action.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONTROLLER_INPUT_SOURCE_NONE = 0,
    CONTROLLER_INPUT_SOURCE_DIAL_BUILTIN = 1,
    CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS = 2,
} controller_input_source_id_t;

typedef enum {
    CONTROLLER_INPUT_CONTROL_NONE = 0,
    CONTROLLER_INPUT_CONTROL_DIAL_ROTATION = 1,
    CONTROLLER_INPUT_CONTROL_FRAME_BOOT = 1,
    CONTROLLER_INPUT_CONTROL_FRAME_GP4 = 2,
    CONTROLLER_INPUT_CONTROL_FRAME_PWR = 3,
} controller_input_control_id_t;

typedef enum {
    CONTROLLER_PHYSICAL_EVENT_NONE = 0,
    CONTROLLER_PHYSICAL_EVENT_ROTATION,
    CONTROLLER_PHYSICAL_EVENT_BUTTON,
} controller_physical_event_kind_t;

typedef enum {
    CONTROLLER_PHYSICAL_GESTURE_NONE = 0,
    CONTROLLER_PHYSICAL_GESTURE_PRESS,
    CONTROLLER_PHYSICAL_GESTURE_RELEASE,
    CONTROLLER_PHYSICAL_GESTURE_TAP,
    CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP,
    CONTROLLER_PHYSICAL_GESTURE_HOLD,
    CONTROLLER_PHYSICAL_GESTURE_REPEAT,
} controller_physical_gesture_t;

typedef enum {
    CONTROLLER_PHYSICAL_EVENT_FLAG_NONE = 0,
    CONTROLLER_PHYSICAL_EVENT_FLAG_INITIAL = 1u << 0,
    CONTROLLER_PHYSICAL_EVENT_FLAG_REPEAT = 1u << 1,
    CONTROLLER_PHYSICAL_EVENT_FLAG_NEUTRAL = 1u << 2,
} controller_physical_event_flags_t;

#define CONTROLLER_PHYSICAL_GESTURE_MASK(gesture) (1u << (gesture))
#define CONTROLLER_PHYSICAL_GESTURE_MASK_ALL                                  \
    ((1u << (CONTROLLER_PHYSICAL_GESTURE_REPEAT + 1)) - 1u)
#define CONTROLLER_PHYSICAL_EVENT_FLAG_MASK                                   \
    (CONTROLLER_PHYSICAL_EVENT_FLAG_INITIAL |                                \
     CONTROLLER_PHYSICAL_EVENT_FLAG_REPEAT |                                 \
     CONTROLLER_PHYSICAL_EVENT_FLAG_NEUTRAL)

typedef struct {
    uint16_t source_id;
    uint16_t control_id;
    controller_physical_event_kind_t kind;
    controller_physical_gesture_t gesture;
    int32_t value;
    uint32_t sequence;
    uint16_t flags;
} controller_physical_event_t;

typedef enum {
    CONTROLLER_INPUT_DESCRIPTOR_NONE = 0,
    CONTROLLER_INPUT_DESCRIPTOR_REBINDABLE = 1u << 0,
    CONTROLLER_INPUT_DESCRIPTOR_LOCKED_DEFAULT = 1u << 1,
} controller_input_descriptor_flags_t;

#define CONTROLLER_INPUT_DESCRIPTOR_FLAG_MASK                                \
    (CONTROLLER_INPUT_DESCRIPTOR_REBINDABLE |                                \
     CONTROLLER_INPUT_DESCRIPTOR_LOCKED_DEFAULT)

typedef enum {
    CONTROLLER_INPUT_VALUE_NONZERO_SIGNED_DELTA = 0,
    CONTROLLER_INPUT_VALUE_UNIT_POSITIVE,
} controller_input_value_policy_t;

typedef struct {
    uint16_t source_id;
    uint16_t control_id;
    controller_physical_event_kind_t event_kind;
    uint32_t flags;
    uint32_t allowed_gestures;
    uint16_t allowed_event_flags;
    controller_input_value_policy_t value_policy;
    char label[24];
} controller_input_descriptor_t;

typedef enum {
    CONTROLLER_INTERACTION_CONTEXT_MEDIA = 0,
    CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER,
    CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY,
} controller_interaction_context_t;

typedef enum {
    CONTROLLER_INPUT_TRANSFORM_FIXED = 0,
    CONTROLLER_INPUT_TRANSFORM_ROTATION_ACCELERATED,
    CONTROLLER_INPUT_TRANSFORM_ROTATION_DIRECTION,
} controller_input_transform_t;

typedef enum {
    CONTROLLER_INPUT_BINDING_NONE = 0,
    CONTROLLER_INPUT_BINDING_LOCKED_DEFAULT = 1u << 0,
} controller_input_binding_flags_t;

#define CONTROLLER_INPUT_BINDING_FLAG_MASK                                   \
    CONTROLLER_INPUT_BINDING_LOCKED_DEFAULT

typedef struct {
    uint16_t source_id;
    uint16_t control_id;
    controller_physical_event_kind_t event_kind;
    controller_physical_gesture_t gesture;
    controller_interaction_context_t context;
    controller_input_transform_t transform;
    uint32_t flags;
    controller_action_t action;
} controller_input_binding_t;

typedef enum {
    CONTROLLER_CONTROL_INTENT_NONE = 0,
    CONTROLLER_CONTROL_INTENT_VOLUME_UP,
    CONTROLLER_CONTROL_INTENT_VOLUME_DOWN,
    CONTROLLER_CONTROL_INTENT_ACTIVATE,
    CONTROLLER_CONTROL_INTENT_NEXT,
    CONTROLLER_CONTROL_INTENT_PREVIOUS,
} controller_control_intent_t;

typedef struct {
    uint32_t accepted;
    uint32_t coalesced;
    uint32_t dropped;
    uint32_t overflow;
} controller_input_mailbox_stats_t;

typedef bool (*controller_action_handler_t)(const controller_action_t *action);

void controller_input_set_action_handler(controller_action_handler_t handler);

bool controller_input_set_context(controller_interaction_context_t context);
controller_interaction_context_t controller_input_get_context(void);

bool controller_input_resolve_physical(
    const controller_physical_event_t *event,
    controller_interaction_context_t context,
    controller_action_t *out_action);
bool controller_input_resolve_control(
    controller_control_intent_t intent,
    controller_interaction_context_t context,
    controller_action_t *out_action);

bool controller_input_dispatch_physical(
    const controller_physical_event_t *event);
bool controller_input_dispatch_control(controller_control_intent_t intent);
bool controller_input_dispatch_action(const controller_action_t *action);

/*
 * Queue semantic control input for resolution on the target UI/controller
 * actor. Safe for BLE/timer callbacks; no heap allocation is performed.
 */
bool controller_input_post_control(controller_control_intent_t intent);
controller_input_mailbox_stats_t controller_input_control_mailbox_stats(void);
void controller_input_reset_control_mailbox_stats(void);

#if defined(__cplusplus)
static_assert(sizeof(controller_physical_event_t) <= 24,
              "physical event exceeds its ESP32 budget");
static_assert(sizeof(controller_input_descriptor_t) <= 48,
              "input descriptor exceeds its ESP32 budget");
static_assert(sizeof(controller_input_binding_t) <= 40,
              "input binding exceeds its ESP32 budget");
#else
_Static_assert(sizeof(controller_physical_event_t) <= 24,
               "physical event exceeds its ESP32 budget");
_Static_assert(sizeof(controller_input_descriptor_t) <= 48,
               "input descriptor exceeds its ESP32 budget");
_Static_assert(sizeof(controller_input_binding_t) <= 40,
               "input binding exceeds its ESP32 budget");
#endif

#ifdef __cplusplus
}
#endif
