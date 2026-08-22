#include "surface_projection_snapshot.h"

#include <math.h>
#include <string.h>

static bool bounded_string(const char *value, size_t capacity, bool required) {
    if (!value || capacity == 0) return false;
    for (size_t index = 0; index < capacity; index++) {
        if (value[index] == '\0') return !required || index != 0;
    }
    return false;
}

static bool valid_kind(surface_control_kind_t kind) {
    return kind >= SURFACE_CONTROL_MOMENTARY &&
           kind <= SURFACE_CONTROL_VOICE_INTENT;
}

static bool requires_action(surface_control_kind_t kind) {
    return kind == SURFACE_CONTROL_MOMENTARY ||
           kind == SURFACE_CONTROL_TOGGLE ||
           kind == SURFACE_CONTROL_SELECT_CONTINUOUS ||
           kind == SURFACE_CONTROL_DETAIL_REQUEST ||
           kind == SURFACE_CONTROL_VOICE_INTENT;
}

static bool valid_affect(const surface_projection_t *projection) {
    return !projection->has_affect ||
           (projection->affect.valence >= -1.0 &&
            projection->affect.valence <= 1.0 &&
            projection->affect.arousal >= 0.0 &&
            projection->affect.arousal <= 1.0 &&
            projection->affect.intensity >= 0.0 &&
            projection->affect.intensity <= 1.0);
}

static bool valid_range(const surface_control_t *control) {
    if (!control->has_range) return true;
    if (!isfinite(control->range_min) || !isfinite(control->range_max) ||
        !isfinite(control->range_step) || control->range_step <= 0.0 ||
        control->range_max < control->range_min) {
        return false;
    }
    if (control->has_value &&
        (!isfinite(control->value) || control->value < control->range_min ||
         control->value > control->range_max)) {
        return false;
    }
    return true;
}

static bool valid_option(const surface_option_t *option) {
    return bounded_string(option->option_id, sizeof(option->option_id), true) &&
           bounded_string(option->label, sizeof(option->label), false) &&
           bounded_string(option->action_ref, sizeof(option->action_ref), true);
}

static bool valid_control(const surface_control_t *control) {
    if (!bounded_string(control->control_id, sizeof(control->control_id), true) ||
        !valid_kind(control->kind) ||
        !bounded_string(control->label, sizeof(control->label), false) ||
        !bounded_string(control->content, sizeof(control->content), false) ||
        !bounded_string(control->deterministic_fallback,
                        sizeof(control->deterministic_fallback), false) ||
        !bounded_string(control->icon, sizeof(control->icon), false) ||
        !bounded_string(control->action_ref, sizeof(control->action_ref),
                        requires_action(control->kind)) ||
        control->option_count > SURFACE_MAX_OPTIONS ||
        control->emphasis < 0 ||
        !valid_range(control)) {
        return false;
    }
    if (control->sensitive &&
        !bounded_string(control->deterministic_fallback,
                        sizeof(control->deterministic_fallback), true)) {
        return false;
    }
    if (control->has_value && control->has_text_value) return false;
    if (control->has_value && !isfinite(control->value)) return false;
    if (control->has_text_value &&
        !bounded_string(control->value_text, sizeof(control->value_text),
                        true)) {
        return false;
    }
    for (uint8_t index = 0; index < control->option_count; index++) {
        if (!valid_option(&control->options[index])) return false;
        for (uint8_t previous = 0; previous < index; previous++) {
            if (strcmp(control->options[previous].option_id,
                       control->options[index].option_id) == 0) {
                return false;
            }
        }
    }
    if (control->kind == SURFACE_CONTROL_SELECT_ONE &&
        control->option_count == 0) {
        return false;
    }
    if (control->has_text_value &&
        control->kind == SURFACE_CONTROL_SELECT_ONE) {
        bool option_found = false;
        for (uint8_t index = 0; index < control->option_count; index++) {
            if (strcmp(control->options[index].option_id,
                       control->value_text) == 0) {
                option_found = true;
                break;
            }
        }
        if (!option_found) return false;
    }
    return true;
}

static bool valid_projection(const surface_projection_t *projection) {
    if (!projection ||
        !bounded_string(projection->projection_id,
                        sizeof(projection->projection_id), true) ||
        projection->section_count > SURFACE_MAX_SECTIONS ||
        !valid_affect(projection)) {
        return false;
    }
    for (uint8_t section_index = 0;
         section_index < projection->section_count; section_index++) {
        const surface_section_t *section = &projection->sections[section_index];
        if (!bounded_string(section->section_id, sizeof(section->section_id),
                            true) ||
            !bounded_string(section->label, sizeof(section->label), false) ||
            section->control_count > SURFACE_MAX_CONTROLS) {
            return false;
        }
        for (uint8_t previous_section = 0;
             previous_section < section_index; previous_section++) {
            if (strcmp(projection->sections[previous_section].section_id,
                       section->section_id) == 0) {
                return false;
            }
        }
        for (uint8_t control_index = 0;
             control_index < section->control_count; control_index++) {
            if (!valid_control(&section->controls[control_index])) return false;
            for (uint8_t previous_section = 0;
                 previous_section <= section_index; previous_section++) {
                const surface_section_t *candidate =
                    &projection->sections[previous_section];
                const uint8_t limit = previous_section == section_index
                                          ? control_index
                                          : candidate->control_count;
                for (uint8_t previous_control = 0;
                     previous_control < limit; previous_control++) {
                    if (strcmp(candidate->controls[previous_control].control_id,
                               section->controls[control_index].control_id) ==
                        0) {
                        return false;
                    }
                }
            }
        }
    }
    return projection->section_count != 0;
}

static bool valid_input_value(const surface_projection_input_value_t *value) {
    if (value->has_numeric_value && value->has_text_value) return false;
    if (value->has_numeric_value && !isfinite(value->numeric_value)) return false;
    return !value->has_text_value ||
           bounded_string(value->text_value, sizeof(value->text_value), true);
}

static const surface_control_t *find_control(
    const surface_projection_t *projection,
    const surface_projection_input_t *input) {
    for (uint8_t section_index = 0;
         section_index < projection->section_count; section_index++) {
        const surface_section_t *section = &projection->sections[section_index];
        for (uint8_t control_index = 0;
             control_index < section->control_count; control_index++) {
            const surface_control_t *control = &section->controls[control_index];
            if (strcmp(control->control_id, input->control_id) == 0) {
                return control;
            }
        }
    }
    return NULL;
}

static bool action_matches(const surface_control_t *control,
                           const surface_projection_input_t *input) {
    if (strcmp(control->action_ref, input->action_ref) == 0 &&
        control->action_ref[0] != '\0') {
        return true;
    }
    for (uint8_t index = 0; index < control->option_count; index++) {
        if (strcmp(control->options[index].action_ref, input->action_ref) == 0)
            return true;
    }
    return false;
}

static bool input_matches(const surface_projection_snapshot_t *snapshot,
                          const surface_projection_input_t *input) {
    if (!snapshot || !input || input->projection_revision != snapshot->revision ||
        !bounded_string(input->control_id, sizeof(input->control_id), true) ||
        !bounded_string(input->action_ref, sizeof(input->action_ref), true) ||
        !valid_input_value(&input->value)) {
        return false;
    }
    const surface_control_t *control = find_control(&snapshot->projection, input);
    if (!control || !action_matches(control, input)) return false;
    switch (control->kind) {
    case SURFACE_CONTROL_SELECT_CONTINUOUS:
        if (!input->value.has_numeric_value || input->value.has_text_value ||
            !control->has_range ||
            input->value.numeric_value < control->range_min ||
            input->value.numeric_value > control->range_max) {
            return false;
        }
        {
            const double position =
                (input->value.numeric_value - control->range_min) /
                control->range_step;
            const double tolerance = 1e-9 * fmax(1.0, fabs(position));
            return fabs(position - round(position)) <= tolerance;
        }
    case SURFACE_CONTROL_SELECT_ONE: {
        if (!input->value.has_text_value || input->value.has_numeric_value)
            return false;
        for (uint8_t index = 0; index < control->option_count; index++) {
            const surface_option_t *option = &control->options[index];
            if (strcmp(option->option_id, input->value.text_value) == 0 &&
                strcmp(option->action_ref, input->action_ref) == 0) {
                return true;
            }
        }
        return false;
    }
    case SURFACE_CONTROL_VOICE_INTENT:
        return !input->value.has_numeric_value;
    case SURFACE_CONTROL_MOMENTARY:
    case SURFACE_CONTROL_TOGGLE:
    case SURFACE_CONTROL_STATUS_LINE:
    case SURFACE_CONTROL_DETAIL_REQUEST:
    default:
        return !input->value.has_numeric_value &&
               !input->value.has_text_value;
    }
}

void surface_projection_snapshot_init(
    surface_projection_snapshot_t *snapshot,
    surface_projection_input_sink_t input_sink,
    void *input_context) {
    if (!snapshot) return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->input_sink = input_sink;
    snapshot->input_context = input_context;
}

void surface_projection_snapshot_set_connection_ready(
    surface_projection_snapshot_t *snapshot,
    bool ready) {
    if (!snapshot) return;
    snapshot->connection_ready = ready;
    snapshot->projection_current = false;
}

void surface_projection_snapshot_authorize_session(
    surface_projection_snapshot_t *snapshot) {
    if (!snapshot) return;
    snapshot->session_authorized = true;
    snapshot->projection_current = false;
}

void surface_projection_snapshot_deauthorize_session(
    surface_projection_snapshot_t *snapshot) {
    if (!snapshot) return;
    snapshot->session_authorized = false;
    snapshot->projection_current = false;
}

bool surface_projection_snapshot_publish(
    surface_projection_snapshot_t *snapshot,
    const surface_projection_t *projection) {
    if (!snapshot || !valid_projection(projection)) {
        if (snapshot) snapshot->projection_current = false;
        return false;
    }
    memcpy(&snapshot->projection, projection, sizeof(snapshot->projection));
    snapshot->has_projection = true;
    snapshot->projection_current = snapshot->connection_ready &&
                                   snapshot->session_authorized;
    snapshot->revision = snapshot->revision == UINT32_MAX
                              ? 1
                              : snapshot->revision + 1;
    return true;
}

void surface_projection_snapshot_mark_stale(
    surface_projection_snapshot_t *snapshot) {
    if (snapshot) snapshot->projection_current = false;
}

bool surface_projection_snapshot_copy(
    const surface_projection_snapshot_t *snapshot,
    surface_projection_t *out) {
    if (!snapshot || !out || !snapshot->has_projection) return false;
    memcpy(out, &snapshot->projection, sizeof(*out));
    return true;
}

bool surface_projection_snapshot_can_emit(
    const surface_projection_snapshot_t *snapshot) {
    return snapshot && snapshot->has_projection && snapshot->projection_current &&
           snapshot->connection_ready && snapshot->session_authorized;
}

bool surface_projection_snapshot_emit(
    surface_projection_snapshot_t *snapshot,
    const surface_projection_input_t *input) {
    if (!surface_projection_snapshot_can_emit(snapshot) ||
        !input_matches(snapshot, input) || !snapshot->input_sink) {
        return false;
    }
    const bool sent = snapshot->input_sink(input, snapshot->input_context);
    if (!sent) snapshot->projection_current = false;
    return sent;
}
