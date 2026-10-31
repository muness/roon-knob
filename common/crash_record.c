// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "crash_record.h"
#include <string.h>
static uint32_t checksum(const crash_record_t *r) {
    uint32_t hash = 2166136261u;
    const unsigned char *bytes = (const unsigned char *)r;
    for (size_t i = 0; i < offsetof(crash_record_t, checksum); ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}
void crash_record_seal(crash_record_t *r) { r->version = CRASH_RECORD_VERSION; r->checksum = checksum(r); }
bool crash_record_valid(const crash_record_t *r, size_t length) {
    return length == sizeof(*r) && r->version == CRASH_RECORD_VERSION &&
        r->depth <= CRASH_BACKTRACE_MAX && r->has_dump <= 1 &&
        memchr(r->task, 0, sizeof(r->task)) && memchr(r->capture_version, 0, sizeof(r->capture_version)) &&
        memchr(r->target, 0, sizeof(r->target)) && memchr(r->crashed_elf_sha, 0, sizeof(r->crashed_elf_sha)) &&
        r->checksum == checksum(r);
}
