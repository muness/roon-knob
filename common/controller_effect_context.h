// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Published by the controller actor. Background effects only read this epoch;
 * they never inspect renderer state or the actor's mutable interaction context. */
uint32_t controller_effect_context_generation(void);
#ifdef __cplusplus
}
#endif
