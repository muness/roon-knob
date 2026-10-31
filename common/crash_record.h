// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CRASH_RECORD_VERSION 1u
#define CRASH_BACKTRACE_MAX 16
typedef struct {
    uint32_t version, reset_reason, pc;
    uint32_t backtrace[CRASH_BACKTRACE_MAX];
    uint8_t depth, has_dump, reserved[2];
    char task[17], capture_version[32], target[32], crashed_elf_sha[65];
    uint32_t checksum;
} crash_record_t;
void crash_record_seal(crash_record_t *record);
bool crash_record_valid(const crash_record_t *record, size_t length);
