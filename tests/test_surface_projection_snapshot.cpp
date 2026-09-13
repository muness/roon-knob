#include "../common/surface_projection_snapshot.h"

#include <cassert>
#include <cmath>
#include <cstring>

static_assert(sizeof(surface_projection_t) == 66264,
              "default host projection size drift");
static_assert(sizeof(surface_projection_snapshot_t) == 66288,
              "default host snapshot size drift");

namespace {

int sink_calls = 0;
bool sink_result = true;
surface_projection_input_t last_input = {};

bool capture_input(const surface_projection_input_t *input, void *) {
    ++sink_calls;
    last_input = *input;
    return sink_result;
}

void text(char *out, const char *value, size_t capacity) {
    std::strncpy(out, value, capacity - 1);
    out[capacity - 1] = '\0';
}

surface_projection_t vector_one(const char *projection_id) {
    surface_projection_t projection = {};
    text(projection.projection_id, projection_id,
         sizeof(projection.projection_id));
    projection.has_affect = true;
    projection.affect.valence = 0.25;
    projection.affect.arousal = 0.5;
    projection.affect.intensity = 0.75;
    projection.section_count = 1;
    surface_section_t &section = projection.sections[0];
    text(section.section_id, "section-alpha", sizeof(section.section_id));
    text(section.label, "Primary", sizeof(section.label));
    section.control_count = 7;

    surface_control_t &momentary = section.controls[0];
    text(momentary.control_id, "opaque-momentary", sizeof(momentary.control_id));
    momentary.kind = SURFACE_CONTROL_MOMENTARY;
    text(momentary.label, "Do", sizeof(momentary.label));
    text(momentary.content, "Ready", sizeof(momentary.content));
    text(momentary.action_ref, "opaque-ref-0", sizeof(momentary.action_ref));
    momentary.emphasis = 8;

    surface_control_t &toggle = section.controls[1];
    text(toggle.control_id, "opaque-toggle", sizeof(toggle.control_id));
    toggle.kind = SURFACE_CONTROL_TOGGLE;
    text(toggle.action_ref, "opaque-ref-1", sizeof(toggle.action_ref));

    surface_control_t &choice = section.controls[2];
    text(choice.control_id, "opaque-choice", sizeof(choice.control_id));
    choice.kind = SURFACE_CONTROL_SELECT_ONE;
    choice.option_count = 2;
    text(choice.options[0].option_id, "option-a", sizeof(choice.options[0].option_id));
    text(choice.options[0].label, "A", sizeof(choice.options[0].label));
    text(choice.options[0].action_ref, "opaque-ref-2a",
         sizeof(choice.options[0].action_ref));
    text(choice.options[1].option_id, "option-b", sizeof(choice.options[1].option_id));
    text(choice.options[1].label, "B", sizeof(choice.options[1].label));
    text(choice.options[1].action_ref, "opaque-ref-2b",
         sizeof(choice.options[1].action_ref));
    choice.has_text_value = true;
    text(choice.value_text, "option-a", sizeof(choice.value_text));

    surface_control_t &continuous = section.controls[3];
    text(continuous.control_id, "opaque-continuous", sizeof(continuous.control_id));
    continuous.kind = SURFACE_CONTROL_SELECT_CONTINUOUS;
    text(continuous.action_ref, "opaque-ref-3", sizeof(continuous.action_ref));
    continuous.has_range = true;
    continuous.range_min = -1.0;
    continuous.range_max = 1.0;
    continuous.range_step = 0.25;
    continuous.has_value = true;
    continuous.value = 0.5;

    surface_control_t &status = section.controls[4];
    text(status.control_id, "opaque-status", sizeof(status.control_id));
    status.kind = SURFACE_CONTROL_STATUS_LINE;
    text(status.content, "Informational", sizeof(status.content));

    surface_control_t &detail = section.controls[5];
    text(detail.control_id, "opaque-detail", sizeof(detail.control_id));
    detail.kind = SURFACE_CONTROL_DETAIL_REQUEST;
    text(detail.action_ref, "opaque-ref-5", sizeof(detail.action_ref));

    surface_control_t &voice = section.controls[6];
    text(voice.control_id, "opaque-voice", sizeof(voice.control_id));
    voice.kind = SURFACE_CONTROL_VOICE_INTENT;
    text(voice.action_ref, "opaque-ref-6", sizeof(voice.action_ref));
    return projection;
}

surface_projection_input_t input(uint32_t revision, const char *control_id,
                                 const char *action_ref) {
    surface_projection_input_t result = {};
    result.projection_revision = revision;
    text(result.control_id, control_id, sizeof(result.control_id));
    text(result.action_ref, action_ref, sizeof(result.action_ref));
    return result;
}

void test_contract_note() {
    assert(SURFACE_WIRE_V0_SNAPSHOT_VERSION == 1u);
    assert(std::strcmp(SURFACE_WIRE_V0_REFERENCE_COMMIT, "0e455fc1") == 0);
    /* Snapshot v1 intentionally has no media/resource member. */
}

void test_vectors_and_snapshot() {
    surface_projection_snapshot_t snapshot;
    surface_projection_snapshot_init(&snapshot, capture_input, nullptr);
    const surface_projection_t first = vector_one("opaque-source-a");
    assert(surface_projection_snapshot_publish(&snapshot, &first));
    assert(snapshot.revision == 1);

    surface_projection_t copied = {};
    assert(surface_projection_snapshot_copy(&snapshot, &copied));
    assert(std::strcmp(copied.projection_id, "opaque-source-a") == 0);
    assert(copied.sections[0].controls[2].option_count == 2);
    assert(copied.sections[0].controls[3].has_range);
    assert(copied.has_affect);

    const surface_projection_t second = vector_one("schema-vector-b");
    assert(surface_projection_snapshot_publish(&snapshot, &second));
    assert(snapshot.revision == 2);
    assert(surface_projection_snapshot_copy(&snapshot, &copied));
    assert(std::strcmp(copied.projection_id, "schema-vector-b") == 0);
}

void test_lifecycle_and_opaque_input() {
    surface_projection_snapshot_t snapshot;
    surface_projection_snapshot_init(&snapshot, capture_input, nullptr);
    const surface_projection_t projection = vector_one("opaque-source-a");
    assert(surface_projection_snapshot_publish(&snapshot, &projection));
    surface_projection_input_t action =
        input(snapshot.revision, "opaque-momentary", "opaque-ref-0");
    assert(!surface_projection_snapshot_can_emit(&snapshot));
    assert(!surface_projection_snapshot_emit(&snapshot, &action));

    surface_projection_snapshot_set_connection_ready(&snapshot, true);
    assert(!surface_projection_snapshot_emit(&snapshot, &action));
    surface_projection_snapshot_authorize_session(&snapshot);
    assert(!surface_projection_snapshot_emit(&snapshot, &action));
    assert(surface_projection_snapshot_publish(&snapshot, &projection));
    action = input(snapshot.revision, "opaque-momentary", "opaque-ref-0");
    sink_calls = 0;
    assert(surface_projection_snapshot_emit(&snapshot, &action));
    assert(sink_calls == 1);
    assert(std::strcmp(last_input.control_id, "opaque-momentary") == 0);
    assert(std::strcmp(last_input.action_ref, "opaque-ref-0") == 0);

    surface_projection_input_t numeric =
        input(snapshot.revision, "opaque-continuous", "opaque-ref-3");
    numeric.value.has_numeric_value = true;
    numeric.value.numeric_value = 0.75;
    assert(surface_projection_snapshot_emit(&snapshot, &numeric));
    numeric.value.numeric_value = 0.50000000001;
    numeric.projection_revision = snapshot.revision;
    assert(surface_projection_snapshot_emit(&snapshot, &numeric));

    surface_projection_input_t selected =
        input(snapshot.revision, "opaque-choice", "opaque-ref-2b");
    selected.value.has_text_value = true;
    text(selected.value.text_value, "option-b", sizeof(selected.value.text_value));
    assert(surface_projection_snapshot_emit(&snapshot, &selected));

    surface_projection_input_t voice =
        input(snapshot.revision, "opaque-voice", "opaque-ref-6");
    voice.value.has_text_value = true;
    text(voice.value.text_value, "opaque utterance",
         sizeof(voice.value.text_value));
    assert(surface_projection_snapshot_emit(&snapshot, &voice));

    sink_result = false;
    assert(!surface_projection_snapshot_emit(&snapshot, &action));
    assert(sink_calls > 1);
    assert(!surface_projection_snapshot_can_emit(&snapshot));
    sink_result = true;
    assert(!surface_projection_snapshot_emit(&snapshot, &action));
    assert(surface_projection_snapshot_publish(&snapshot, &projection));
    action = input(snapshot.revision, "opaque-momentary", "opaque-ref-0");
    assert(surface_projection_snapshot_emit(&snapshot, &action));

    surface_projection_snapshot_deauthorize_session(&snapshot);
    assert(!surface_projection_snapshot_emit(&snapshot, &action));
    surface_projection_snapshot_authorize_session(&snapshot);
    surface_projection_snapshot_set_connection_ready(&snapshot, true);
    assert(!surface_projection_snapshot_emit(&snapshot, &action));
    assert(surface_projection_snapshot_publish(&snapshot, &projection));
    action = input(snapshot.revision, "opaque-momentary", "opaque-ref-0");
    assert(surface_projection_snapshot_emit(&snapshot, &action));

    surface_projection_snapshot_set_connection_ready(&snapshot, false);
    assert(!surface_projection_snapshot_emit(&snapshot, &action));
    surface_projection_snapshot_set_connection_ready(&snapshot, true);
    surface_projection_snapshot_authorize_session(&snapshot);
    assert(!surface_projection_snapshot_emit(&snapshot, &action));
    assert(surface_projection_snapshot_publish(&snapshot, &projection));
    action = input(snapshot.revision, "opaque-momentary", "opaque-ref-0");
    assert(surface_projection_snapshot_emit(&snapshot, &action));
}

void test_rejection_and_stale_gates() {
    surface_projection_snapshot_t snapshot;
    surface_projection_snapshot_init(&snapshot, capture_input, nullptr);
    const surface_projection_t projection = vector_one("opaque-source-a");
    assert(surface_projection_snapshot_publish(&snapshot, &projection));
    surface_projection_snapshot_set_connection_ready(&snapshot, true);
    surface_projection_snapshot_authorize_session(&snapshot);
    assert(surface_projection_snapshot_publish(&snapshot, &projection));

    surface_projection_input_t wrong_ref =
        input(snapshot.revision, "opaque-momentary", "not-current");
    assert(!surface_projection_snapshot_emit(&snapshot, &wrong_ref));

    surface_projection_input_t wrong_revision =
        input(snapshot.revision - 1, "opaque-momentary", "opaque-ref-0");
    assert(!surface_projection_snapshot_emit(&snapshot, &wrong_revision));

    surface_projection_input_t out_of_range =
        input(snapshot.revision, "opaque-continuous", "opaque-ref-3");
    out_of_range.value.has_numeric_value = true;
    out_of_range.value.numeric_value = 2.0;
    assert(!surface_projection_snapshot_emit(&snapshot, &out_of_range));

    out_of_range.value.numeric_value = 0.6;
    assert(!surface_projection_snapshot_emit(&snapshot, &out_of_range));

    surface_projection_input_t wrong_kind =
        input(snapshot.revision, "opaque-toggle", "opaque-ref-1");
    wrong_kind.value.has_numeric_value = true;
    wrong_kind.value.numeric_value = 1.0;
    assert(!surface_projection_snapshot_emit(&snapshot, &wrong_kind));

    surface_projection_input_t wrong_option =
        input(snapshot.revision, "opaque-choice", "opaque-ref-2b");
    wrong_option.value.has_text_value = true;
    text(wrong_option.value.text_value, "option-a",
         sizeof(wrong_option.value.text_value));
    assert(!surface_projection_snapshot_emit(&snapshot, &wrong_option));

    surface_projection_input_t continuous_text =
        input(snapshot.revision, "opaque-continuous", "opaque-ref-3");
    continuous_text.value.has_text_value = true;
    text(continuous_text.value.text_value, "not-numeric",
         sizeof(continuous_text.value.text_value));
    assert(!surface_projection_snapshot_emit(&snapshot, &continuous_text));

    surface_projection_input_t unterminated =
        input(snapshot.revision, "opaque-choice", "opaque-ref-2a");
    unterminated.value.has_text_value = true;
    std::memset(unterminated.value.text_value, 'x',
                sizeof(unterminated.value.text_value));
    assert(!surface_projection_snapshot_emit(&snapshot, &unterminated));

    surface_projection_snapshot_mark_stale(&snapshot);
    assert(!surface_projection_snapshot_can_emit(&snapshot));
    assert(!surface_projection_snapshot_emit(&snapshot, &wrong_ref));

    surface_projection_t rejected = vector_one("rejected-candidate");
    rejected.sections[0].controls[0].emphasis = -1;
    assert(!surface_projection_snapshot_publish(&snapshot, &rejected));
    assert(!surface_projection_snapshot_can_emit(&snapshot));
    surface_projection_t retained = {};
    assert(surface_projection_snapshot_copy(&snapshot, &retained));
    assert(std::strcmp(retained.projection_id, "opaque-source-a") == 0);

    surface_projection_t duplicate_section = vector_one("duplicate-section");
    duplicate_section.section_count = 2;
    duplicate_section.sections[1] = duplicate_section.sections[0];
    assert(!surface_projection_snapshot_publish(&snapshot, &duplicate_section));

    surface_projection_t duplicate_control = vector_one("duplicate-control");
    duplicate_control.sections[0].controls[1].control_id[0] =
        duplicate_control.sections[0].controls[0].control_id[0];
    text(duplicate_control.sections[0].controls[1].control_id,
         duplicate_control.sections[0].controls[0].control_id,
         sizeof(duplicate_control.sections[0].controls[1].control_id));
    assert(!surface_projection_snapshot_publish(&snapshot, &duplicate_control));

    surface_projection_t duplicate_option = vector_one("duplicate-option");
    text(duplicate_option.sections[0].controls[2].options[1].option_id,
         duplicate_option.sections[0].controls[2].options[0].option_id,
         sizeof(duplicate_option.sections[0].controls[2].options[1].option_id));
    assert(!surface_projection_snapshot_publish(&snapshot, &duplicate_option));
}

void test_invalid_vectors() {
    surface_projection_snapshot_t snapshot;
    surface_projection_snapshot_init(&snapshot, capture_input, nullptr);
    surface_projection_t invalid = vector_one("invalid");
    invalid.sections[0].controls[0].action_ref[0] = '\0';
    assert(!surface_projection_snapshot_publish(&snapshot, &invalid));

    invalid = vector_one("invalid-range");
    invalid.sections[0].controls[3].range_step = 0.0;
    assert(!surface_projection_snapshot_publish(&snapshot, &invalid));

    invalid = vector_one("invalid-affect");
    invalid.affect.valence = 1.01;
    assert(!surface_projection_snapshot_publish(&snapshot, &invalid));

    invalid = vector_one("invalid-arousal");
    invalid.affect.arousal = -0.01;
    assert(!surface_projection_snapshot_publish(&snapshot, &invalid));

    invalid = vector_one("invalid-intensity");
    invalid.affect.intensity = 1.01;
    assert(!surface_projection_snapshot_publish(&snapshot, &invalid));

    invalid = vector_one("invalid-value-kind");
    invalid.sections[0].controls[3].has_text_value = true;
    text(invalid.sections[0].controls[3].value_text, "both",
         sizeof(invalid.sections[0].controls[3].value_text));
    assert(!surface_projection_snapshot_publish(&snapshot, &invalid));

    invalid = vector_one("invalid-choice-value");
    text(invalid.sections[0].controls[2].value_text, "missing",
         sizeof(invalid.sections[0].controls[2].value_text));
    assert(!surface_projection_snapshot_publish(&snapshot, &invalid));

    invalid = vector_one("invalid-sensitive-fallback");
    invalid.sections[0].controls[1].sensitive = true;
    assert(!surface_projection_snapshot_publish(&snapshot, &invalid));

    invalid = vector_one("valid-sensitive-fallback");
    invalid.sections[0].controls[1].sensitive = true;
    text(invalid.sections[0].controls[1].deterministic_fallback, "safe",
         sizeof(invalid.sections[0].controls[1].deterministic_fallback));
    assert(surface_projection_snapshot_publish(&snapshot, &invalid));

    invalid = vector_one("invalid-icon-termination");
    std::memset(invalid.sections[0].controls[0].icon, 'x',
                sizeof(invalid.sections[0].controls[0].icon));
    assert(!surface_projection_snapshot_publish(&snapshot, &invalid));

    invalid = vector_one("invalid-fallback-termination");
    std::memset(invalid.sections[0].controls[0].deterministic_fallback, 'x',
                sizeof(invalid.sections[0].controls[0].deterministic_fallback));
    assert(!surface_projection_snapshot_publish(&snapshot, &invalid));

    invalid = vector_one("invalid-emphasis");
    invalid.sections[0].controls[0].emphasis = -1;
    assert(!surface_projection_snapshot_publish(&snapshot, &invalid));
}

}  // namespace

int main() {
    test_contract_note();
    test_vectors_and_snapshot();
    test_lifecycle_and_opaque_input();
    test_rejection_and_stale_gates();
    test_invalid_vectors();
    return 0;
}
