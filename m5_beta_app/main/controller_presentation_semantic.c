#include "controller_presentation_semantic.h"

#include "touch_ui.h"

const char *controller_presentation_semantic_version(void) {
    return "controller-presentation-semantic-v1";
}

bool controller_presentation_semantic_admit(
    const char *contract_json, size_t contract_len,
    char *evidence_json, size_t evidence_capacity) {
    return touch_ui_semantic_admit(contract_json, contract_len,
                                   evidence_json, evidence_capacity);
}

bool controller_presentation_semantic_apply(const char *contract_id) {
    return touch_ui_semantic_apply(contract_id);
}
