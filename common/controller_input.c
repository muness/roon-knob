#include "controller_input.h"

#include "controller_input_profile.h"
#include "platform/platform_task.h"

#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

static controller_action_handler_t s_action_handler;
/* Owned by the existing target UI/controller actor. */
static controller_interaction_context_t s_context =
    CONTROLLER_INTERACTION_CONTEXT_MEDIA;
static atomic_uint_least32_t s_control_accepted;
static atomic_uint_least32_t s_control_dropped;
static atomic_uint_least32_t s_control_overflow;

static bool context_is_valid(controller_interaction_context_t context) {
    return context >= CONTROLLER_INTERACTION_CONTEXT_MEDIA &&
           context <= CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY;
}

static bool command_is_valid(const controller_command_t *command) {
    if (!command || command->kind <= CONTROLLER_COMMAND_NONE ||
        command->kind > CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS) {
        return false;
    }
    if (command->kind == CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS) {
        return command->volume_steps != 0;
    }
    return command->volume_steps == 0;
}

static bool action_is_valid(const controller_action_t *action) {
    if (!action) {
        return false;
    }
    switch (action->kind) {
    case CONTROLLER_ACTION_COMMAND:
        return command_is_valid(&action->value.command);
    case CONTROLLER_ACTION_OPEN_ZONE_PICKER:
    case CONTROLLER_ACTION_CLOSE_ZONE_PICKER:
    case CONTROLLER_ACTION_SELECT_ZONE_PICKER:
    case CONTROLLER_ACTION_SHOW_SETTINGS:
        return action->value.picker_delta == 0;
    case CONTROLLER_ACTION_SCROLL_ZONE_PICKER:
        return action->value.picker_delta != 0;
    case CONTROLLER_ACTION_SYSTEM:
        return false;
    case CONTROLLER_ACTION_NONE:
    case CONTROLLER_ACTION_ADAPTIVE_REF:
    default:
        return false;
    }
}

/* System actions have no public semantic dispatch path. */
static bool locked_system_action_is_valid(
    const controller_input_descriptor_t *descriptor,
    const controller_input_binding_t *binding) {
    return descriptor && binding &&
           (descriptor->flags & CONTROLLER_INPUT_DESCRIPTOR_LOCKED_DEFAULT) !=
               0 &&
           (binding->flags & CONTROLLER_INPUT_BINDING_LOCKED_DEFAULT) != 0 &&
           binding->action.value.system > CONTROLLER_SYSTEM_ACTION_NONE &&
           binding->action.value.system <= CONTROLLER_SYSTEM_ACTION_RESTART;
}

static void atomic_increment_saturating(atomic_uint_least32_t *counter) {
    uint_least32_t value =
        atomic_load_explicit(counter, memory_order_relaxed);
    while (value < UINT32_MAX &&
           !atomic_compare_exchange_weak_explicit(
               counter, &value, value + 1, memory_order_relaxed,
               memory_order_relaxed)) {
    }
}

void controller_input_set_action_handler(controller_action_handler_t handler) {
    s_action_handler = handler;
}

bool controller_input_set_context(controller_interaction_context_t context) {
    if (!context_is_valid(context)) {
        return false;
    }
    s_context = context;
    return true;
}

controller_interaction_context_t controller_input_get_context(void) {
    return s_context;
}

static bool resolve_volume_ticks(int32_t ticks,
                                 controller_action_t *out_action) {
    if (ticks == 0 || !out_action) {
        return false;
    }

    int64_t magnitude = ticks;
    if (magnitude < 0) {
        magnitude = -magnitude;
    }

    int32_t steps;
    if (magnitude >= 3) {
        steps = 5;
    } else if (magnitude == 2) {
        steps = 3;
    } else {
        steps = 1;
    }
    if (ticks < 0) {
        steps = -steps;
    }

    *out_action = controller_action_command(
        controller_command_adjust_volume(steps));
    return true;
}

static bool binding_matches(const controller_input_binding_t *binding,
                            const controller_physical_event_t *event,
                            controller_interaction_context_t context) {
    return binding && event &&
           binding->source_id == event->source_id &&
           binding->control_id == event->control_id &&
           binding->event_kind == event->kind &&
           binding->gesture == event->gesture &&
           binding->context == context;
}

static bool descriptor_supports_event(
    const controller_input_descriptor_t *descriptor,
    const controller_physical_event_t *event) {
    if (!descriptor || !event ||
        descriptor->source_id != event->source_id ||
        descriptor->control_id != event->control_id ||
        descriptor->event_kind != event->kind ||
        (descriptor->flags & ~CONTROLLER_INPUT_DESCRIPTOR_FLAG_MASK) != 0 ||
        (descriptor->flags & CONTROLLER_INPUT_DESCRIPTOR_FLAG_MASK) ==
            CONTROLLER_INPUT_DESCRIPTOR_FLAG_MASK ||
        descriptor->allowed_gestures == 0 ||
        (descriptor->allowed_gestures &
         ~CONTROLLER_PHYSICAL_GESTURE_MASK_ALL) != 0 ||
        (descriptor->allowed_event_flags &
         ~CONTROLLER_PHYSICAL_EVENT_FLAG_MASK) != 0 ||
        event->gesture < CONTROLLER_PHYSICAL_GESTURE_NONE ||
        event->gesture > CONTROLLER_PHYSICAL_GESTURE_REPEAT ||
        (descriptor->allowed_gestures &
         CONTROLLER_PHYSICAL_GESTURE_MASK(event->gesture)) == 0 ||
        (event->flags & ~CONTROLLER_PHYSICAL_EVENT_FLAG_MASK) != 0 ||
        (event->flags & ~descriptor->allowed_event_flags) != 0) {
        return false;
    }

    switch (descriptor->value_policy) {
    case CONTROLLER_INPUT_VALUE_NONZERO_SIGNED_DELTA:
        return event->value != 0;
    case CONTROLLER_INPUT_VALUE_UNIT_POSITIVE:
        return event->value == 1;
    default:
        return false;
    }
}

static bool resolve_binding(const controller_input_descriptor_t *descriptor,
                            const controller_input_binding_t *binding,
                            const controller_physical_event_t *event,
                            controller_action_t *out_action) {
    if (!descriptor || !binding || !event || !out_action ||
        (binding->flags & ~CONTROLLER_INPUT_BINDING_FLAG_MASK) != 0) {
        return false;
    }

    switch (binding->transform) {
    case CONTROLLER_INPUT_TRANSFORM_FIXED:
        if (binding->action.kind == CONTROLLER_ACTION_SYSTEM) {
            if (!locked_system_action_is_valid(descriptor, binding)) {
                return false;
            }
        } else if (!action_is_valid(&binding->action)) {
            return false;
        }
        *out_action = binding->action;
        return true;

    case CONTROLLER_INPUT_TRANSFORM_ROTATION_ACCELERATED:
        if (binding->action.kind != CONTROLLER_ACTION_COMMAND ||
            binding->action.value.command.kind !=
                CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS) {
            return false;
        }
        return resolve_volume_ticks(event->value, out_action);

    case CONTROLLER_INPUT_TRANSFORM_ROTATION_DIRECTION:
        if (binding->action.kind !=
                CONTROLLER_ACTION_SCROLL_ZONE_PICKER ||
            event->value == 0) {
            return false;
        }
        *out_action =
            controller_action_picker_scroll(event->value > 0 ? 1 : -1);
        return true;

    default:
        return false;
    }
}

bool controller_input_resolve_physical(
    const controller_physical_event_t *event,
    controller_interaction_context_t context,
    controller_action_t *out_action) {
    if (!out_action) {
        return false;
    }
    *out_action = controller_action_none();
    if (!event || !context_is_valid(context) ||
        event->source_id == CONTROLLER_INPUT_SOURCE_NONE ||
        event->control_id == CONTROLLER_INPUT_CONTROL_NONE ||
        event->kind <= CONTROLLER_PHYSICAL_EVENT_NONE ||
        event->kind > CONTROLLER_PHYSICAL_EVENT_BUTTON) {
        return false;
    }

    size_t descriptor_count = 0;
    const controller_input_descriptor_t *descriptors =
        controller_input_profile_descriptors(&descriptor_count);
    if (!descriptors || descriptor_count == 0) {
        return false;
    }
    const controller_input_descriptor_t *descriptor = NULL;
    for (size_t i = 0; i < descriptor_count; ++i) {
        if (descriptors[i].source_id == event->source_id &&
            descriptors[i].control_id == event->control_id) {
            descriptor = &descriptors[i];
            break;
        }
    }
    if (!descriptor_supports_event(descriptor, event)) {
        return false;
    }

    size_t binding_count = 0;
    const controller_input_binding_t *bindings =
        controller_input_profile_bindings(&binding_count);
    if (!bindings && binding_count != 0) {
        return false;
    }
    for (size_t i = 0; i < binding_count; ++i) {
        if (binding_matches(&bindings[i], event, context)) {
            return resolve_binding(descriptor, &bindings[i], event,
                                   out_action);
        }
    }

    return false;
}

bool controller_input_resolve_control(
    controller_control_intent_t intent,
    controller_interaction_context_t context,
    controller_action_t *out_action) {
    if (!out_action) {
        return false;
    }
    *out_action = controller_action_none();
    if (!context_is_valid(context) ||
        intent <= CONTROLLER_CONTROL_INTENT_NONE ||
        intent > CONTROLLER_CONTROL_INTENT_PREVIOUS) {
        return false;
    }

    if (context == CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER) {
        switch (intent) {
        case CONTROLLER_CONTROL_INTENT_VOLUME_UP:
            *out_action = controller_action_picker_scroll(1);
            return true;
        case CONTROLLER_CONTROL_INTENT_VOLUME_DOWN:
            *out_action = controller_action_picker_scroll(-1);
            return true;
        case CONTROLLER_CONTROL_INTENT_ACTIVATE:
            *out_action = controller_action_simple(
                CONTROLLER_ACTION_SELECT_ZONE_PICKER);
            return true;
        case CONTROLLER_CONTROL_INTENT_NEXT:
        case CONTROLLER_CONTROL_INTENT_PREVIOUS:
        case CONTROLLER_CONTROL_INTENT_NONE:
        default:
            return false;
        }
    }

    /*
     * Preserve the legacy non-picker behavior for both media and
     * settings/recovery until a separately characterized UX change says
     * otherwise.
     */
    switch (intent) {
    case CONTROLLER_CONTROL_INTENT_VOLUME_UP:
        *out_action = controller_action_command(
            controller_command_adjust_volume(1));
        return true;
    case CONTROLLER_CONTROL_INTENT_VOLUME_DOWN:
        *out_action = controller_action_command(
            controller_command_adjust_volume(-1));
        return true;
    case CONTROLLER_CONTROL_INTENT_ACTIVATE:
        *out_action = controller_action_command(
            controller_command_make(CONTROLLER_COMMAND_TOGGLE_PLAYBACK));
        return true;
    case CONTROLLER_CONTROL_INTENT_NEXT:
        *out_action = controller_action_command(
            controller_command_make(CONTROLLER_COMMAND_NEXT_TRACK));
        return true;
    case CONTROLLER_CONTROL_INTENT_PREVIOUS:
        *out_action = controller_action_command(
            controller_command_make(CONTROLLER_COMMAND_PREVIOUS_TRACK));
        return true;
    case CONTROLLER_CONTROL_INTENT_NONE:
    default:
        return false;
    }
}

bool controller_input_dispatch_action(const controller_action_t *action) {
    return action_is_valid(action) && s_action_handler &&
           s_action_handler(action);
}

bool controller_input_dispatch_physical(
    const controller_physical_event_t *event) {
    controller_action_t action = controller_action_none();
    controller_interaction_context_t context = s_context;
    if (!controller_input_resolve_physical(event, context, &action)) {
        return false;
    }
    if (action.kind == CONTROLLER_ACTION_SYSTEM) {
        /* Privileged effects have a target-local locked ingress only. */
        return false;
    }
    return controller_input_dispatch_action(&action);
}

bool controller_input_dispatch_control(controller_control_intent_t intent) {
    controller_action_t action = controller_action_none();
    controller_interaction_context_t context = s_context;
    return controller_input_resolve_control(intent, context, &action) &&
           controller_input_dispatch_action(&action);
}

static void dispatch_posted_control(void *arg) {
    controller_control_intent_t intent =
        (controller_control_intent_t)(intptr_t)arg;
    (void)controller_input_dispatch_control(intent);
}

bool controller_input_post_control(controller_control_intent_t intent) {
    if (intent <= CONTROLLER_CONTROL_INTENT_NONE ||
        intent > CONTROLLER_CONTROL_INTENT_PREVIOUS) {
        return false;
    }
    bool accepted = platform_task_post_to_ui(
        dispatch_posted_control, (void *)(intptr_t)intent);
    if (accepted) {
        atomic_increment_saturating(&s_control_accepted);
    } else {
        atomic_increment_saturating(&s_control_dropped);
        atomic_increment_saturating(&s_control_overflow);
    }
    return accepted;
}

controller_input_mailbox_stats_t controller_input_control_mailbox_stats(void) {
    controller_input_mailbox_stats_t stats = {
        .accepted =
            atomic_load_explicit(&s_control_accepted, memory_order_relaxed),
        .coalesced = 0,
        .dropped =
            atomic_load_explicit(&s_control_dropped, memory_order_relaxed),
        .overflow =
            atomic_load_explicit(&s_control_overflow, memory_order_relaxed),
    };
    return stats;
}

void controller_input_reset_control_mailbox_stats(void) {
    atomic_store_explicit(&s_control_accepted, 0, memory_order_relaxed);
    atomic_store_explicit(&s_control_dropped, 0, memory_order_relaxed);
    atomic_store_explicit(&s_control_overflow, 0, memory_order_relaxed);
}
