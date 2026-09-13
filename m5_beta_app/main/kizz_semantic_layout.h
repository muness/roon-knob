#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool has_artwork;
    bool has_transport;
    bool has_volume;
    bool has_zone;
    bool content_available;
    bool metadata_overflow;
    bool voice_active;
    bool review_active;
    bool recovery_active;
    bool reduced_motion;
    bool touch_input;
    bool voice_input;
    bool encoder_input;
    bool button_input;
} kizz_semantic_context_t;

void kizz_semantic_set_context(const kizz_semantic_context_t *context);
bool kizz_semantic_admit_json(const char *contract_json, size_t contract_len,
                              char *evidence_json, size_t evidence_capacity);
bool kizz_semantic_apply(const char *contract_id);
bool kizz_semantic_apply_changed(void);
uint8_t kizz_semantic_active_family_token(void);
const char *kizz_semantic_family_name(uint8_t family_token);
const char *kizz_semantic_family_signature(uint8_t family_token);

#ifdef __cplusplus
}
#endif
