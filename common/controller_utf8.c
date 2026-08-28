#include "controller_utf8.h"

#include <stddef.h>

uint32_t controller_utf8_decode_next(const char **cursor) {
    if (!cursor || !*cursor) return 0;
    const unsigned char *s = (const unsigned char *)*cursor;
    const uint8_t lead = s[0];
    if (lead == 0) return 0;
    if (lead < 0x80) {
        *cursor += 1;
        return lead;
    }

    uint32_t codepoint = 0;
    size_t continuation_count = 0;
    uint32_t minimum = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
        codepoint = lead & 0x1Fu;
        continuation_count = 1;
        minimum = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        codepoint = lead & 0x0Fu;
        continuation_count = 2;
        minimum = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        codepoint = lead & 0x07u;
        continuation_count = 3;
        minimum = 0x10000;
    } else {
        *cursor += 1;
        return 0xFFFD;
    }

    for (size_t i = 1; i <= continuation_count; ++i) {
        if ((s[i] & 0xC0u) != 0x80u) {
            *cursor += 1;
            return 0xFFFD;
        }
        codepoint = (codepoint << 6) | (s[i] & 0x3Fu);
    }
    if (codepoint < minimum || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        *cursor += 1;
        return 0xFFFD;
    }

    *cursor += continuation_count + 1;
    return codepoint;
}
