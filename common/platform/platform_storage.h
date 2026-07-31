#pragma once

#include "rk_cfg.h"

#include <stdbool.h>

typedef enum {
    /* The backend could not read its device-local NVS namespace. */
    PLATFORM_STORAGE_READ_ERROR = 0,
    /* No saved compatibility blob exists yet. */
    PLATFORM_STORAGE_READ_EMPTY,
    /* A current V3 blob was decoded without repair. */
    PLATFORM_STORAGE_READ_VALID,
    /* A same-device V1/V2/V3 candidate was decoded and needs owner persist. */
    PLATFORM_STORAGE_READ_RECOVERED,
} platform_storage_read_state_t;

typedef struct {
    platform_storage_read_state_t state;
    bool needs_persist;
} platform_storage_read_result_t;

typedef enum {
    /* Validation, write, or commit failed before durability is possible. */
    PLATFORM_STORAGE_NOT_COMMITTED = 0,
    /* Commit completed and the full normalized V3 candidate read back exactly. */
    PLATFORM_STORAGE_COMMITTED_VERIFIED,
    /* Commit completed, but full read-back verification was unavailable or differed. */
    PLATFORM_STORAGE_COMMITTED_UNVERIFIED,
} platform_storage_write_result_t;

/*
 * Decode only: migration and normalization can mark a candidate for persistence,
 * but this function never writes. The configuration owner is the sole caller
 * that decides whether and how to persist the candidate.
 */
bool platform_storage_read(rk_cfg_t *out,
                           platform_storage_read_result_t *out_result);

/* Persist and fully verify a normalized V3 candidate. */
platform_storage_write_result_t platform_storage_write(const rk_cfg_t *in);

void platform_storage_defaults(rk_cfg_t *out);
