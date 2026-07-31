#include "controller_action_router.h"

#include "bridge_client.h"
#include "controller_input.h"
#include "controller_presentation.h"
#include "platform/platform_task.h"

#include <stdlib.h>
#include <string.h>

#define ZONE_ID_BACK "__back__"
#define ZONE_ID_SETTINGS "__settings__"

static const char *const s_back_name = "Back";
static const char *const s_back_id = ZONE_ID_BACK;
static const char *const s_settings_name = "Settings";
static const char *const s_settings_id = ZONE_ID_SETTINGS;

typedef void (*queued_text_setter_t)(const char *text);

typedef struct {
    queued_text_setter_t setter;
    char text[];
} queued_text_t;

static void apply_queued_text(void *arg) {
    queued_text_t *queued = arg;
    if (!queued) {
        return;
    }
    queued->setter(queued->text);
    free(queued);
}

static void post_text(queued_text_setter_t setter, const char *text) {
    if (!setter || !text) {
        return;
    }

    size_t text_len = strlen(text);
    queued_text_t *queued = malloc(sizeof(*queued) + text_len + 1);
    if (!queued) {
        return;
    }
    queued->setter = setter;
    memcpy(queued->text, text, text_len + 1);

    if (!platform_task_post_to_ui(apply_queued_text, queued)) {
        free(queued);
    }
}

typedef struct {
    bool shown;
} picker_open_context_t;

static void show_picker_for_zones(const bridge_zone_t *zones,
                                  int zone_count,
                                  const char *current_zone_id,
                                  void *ctx) {
    picker_open_context_t *open = ctx;
    const char *names[BRIDGE_CLIENT_MAX_ZONES + 2];
    const char *ids[BRIDGE_CLIENT_MAX_ZONES + 2];
    int count = 0;
    int selected = 1;

    names[count] = s_back_name;
    ids[count++] = s_back_id;

    if (!zones || zone_count < 0) {
        zone_count = 0;
    }
    if (zone_count > BRIDGE_CLIENT_MAX_ZONES) {
        zone_count = BRIDGE_CLIENT_MAX_ZONES;
    }

    for (int i = 0; i < zone_count; ++i) {
        names[count] = zones[i].name;
        ids[count] = zones[i].id;
        if (current_zone_id &&
            strcmp(zones[i].id, current_zone_id) == 0) {
            selected = count;
        }
        count++;
    }

    names[count] = s_settings_name;
    ids[count++] = s_settings_id;

    controller_presentation_show_zone_picker(names, ids, count, selected);
    if (open) {
        open->shown = true;
    }
}

static bool open_picker(void) {
    picker_open_context_t open = {0};
    bool visited = bridge_client_visit_zones(show_picker_for_zones, &open);
    if (!visited || !open.shown) {
        show_picker_for_zones(NULL, 0, NULL, &open);
    }
    return controller_input_set_context(
        CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER);
}

static bool hide_picker(controller_interaction_context_t next_context) {
    controller_presentation_hide_zone_picker();
    return controller_input_set_context(next_context);
}

static bool select_picker_entry(void) {
    char selected_id[sizeof(((bridge_zone_t *)0)->id)] = {0};
    controller_presentation_zone_picker_get_selected_id(
        selected_id, sizeof(selected_id));

    if (strcmp(selected_id, ZONE_ID_BACK) == 0) {
        return hide_picker(CONTROLLER_INTERACTION_CONTEXT_MEDIA);
    }

    if (strcmp(selected_id, ZONE_ID_SETTINGS) == 0) {
        controller_presentation_hide_zone_picker();
        controller_presentation_show_settings();
        return controller_input_set_context(
            CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY);
    }

    if (controller_presentation_zone_picker_is_current_selection()) {
        return hide_picker(CONTROLLER_INTERACTION_CONTEXT_MEDIA);
    }

    bridge_zone_selection_result_t result =
        bridge_client_select_zone_value(selected_id);
    if (result.found && result.became_operational) {
        controller_presentation_set_network_status(NULL);
    }
    (void)hide_picker(CONTROLLER_INTERACTION_CONTEXT_MEDIA);
    if (!result.found) {
        return false;
    }

    post_text(controller_presentation_set_zone_name, result.zone_name);
    post_text(controller_presentation_set_message, "Loading zone...");
    return true;
}

void controller_action_router_init(void) {
    (void)controller_input_set_context(
        CONTROLLER_INTERACTION_CONTEXT_MEDIA);
}

bool controller_action_router_handle(const controller_action_t *action) {
    if (!action) {
        return false;
    }

    switch (action->kind) {
    case CONTROLLER_ACTION_COMMAND:
        if (action->value.command.kind <= CONTROLLER_COMMAND_NONE ||
            action->value.command.kind >
                CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS ||
            (action->value.command.kind ==
                 CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS &&
             action->value.command.volume_steps == 0) ||
            (action->value.command.kind !=
                 CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS &&
             action->value.command.volume_steps != 0)) {
            return false;
        }
        return bridge_client_execute_command(&action->value.command);

    case CONTROLLER_ACTION_OPEN_ZONE_PICKER:
        return open_picker();

    case CONTROLLER_ACTION_CLOSE_ZONE_PICKER:
        if (controller_input_get_context() !=
            CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER) {
            return false;
        }
        return hide_picker(CONTROLLER_INTERACTION_CONTEXT_MEDIA);

    case CONTROLLER_ACTION_SCROLL_ZONE_PICKER:
        if (controller_input_get_context() !=
                CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER ||
            action->value.picker_delta == 0) {
            return false;
        }
        controller_presentation_zone_picker_scroll(
            action->value.picker_delta > 0 ? 1 : -1);
        return true;

    case CONTROLLER_ACTION_SELECT_ZONE_PICKER:
        if (controller_input_get_context() !=
            CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER) {
            return false;
        }
        return select_picker_entry();

    case CONTROLLER_ACTION_SHOW_SETTINGS:
        controller_presentation_show_settings();
        return controller_input_set_context(
            CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY);

    case CONTROLLER_ACTION_SYSTEM:
        return false;

    case CONTROLLER_ACTION_NONE:
    case CONTROLLER_ACTION_ADAPTIVE_REF:
    default:
        return false;
    }
}
