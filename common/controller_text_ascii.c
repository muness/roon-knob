#include "controller_text_ascii.h"
#include "controller_utf8.h"

#include <stdbool.h>
#include <stdint.h>

static bool append_char(char *out, size_t capacity, size_t *used, char value) {
    if (*used + 1 >= capacity) return false;
    out[(*used)++] = value;
    return true;
}

static bool append_text(char *out, size_t capacity, size_t *used,
                        const char *text) {
    while (*text) {
        if (!append_char(out, capacity, used, *text++)) return false;
    }
    return true;
}

static const char *latin_ascii(uint32_t codepoint) {
    switch (codepoint) {
        case 0x00c0: case 0x00c1: case 0x00c2: case 0x00c3:
        case 0x00c4: case 0x00c5: return "A";
        case 0x00c6: return "AE";
        case 0x00c7: return "C";
        case 0x00c8: case 0x00c9: case 0x00ca: case 0x00cb: return "E";
        case 0x00cc: case 0x00cd: case 0x00ce: case 0x00cf: return "I";
        case 0x00d0: return "D";
        case 0x00d1: return "N";
        case 0x00d2: case 0x00d3: case 0x00d4: case 0x00d5:
        case 0x00d6: case 0x00d8: return "O";
        case 0x00d9: case 0x00da: case 0x00db: case 0x00dc: return "U";
        case 0x00dd: return "Y";
        case 0x00de: return "TH";
        case 0x00df: return "ss";
        case 0x00e0: case 0x00e1: case 0x00e2: case 0x00e3:
        case 0x00e4: case 0x00e5: return "a";
        case 0x00e6: return "ae";
        case 0x00e7: return "c";
        case 0x00e8: case 0x00e9: case 0x00ea: case 0x00eb: return "e";
        case 0x00ec: case 0x00ed: case 0x00ee: case 0x00ef: return "i";
        case 0x00f0: return "d";
        case 0x00f1: return "n";
        case 0x00f2: case 0x00f3: case 0x00f4: case 0x00f5:
        case 0x00f6: case 0x00f8: return "o";
        case 0x00f9: case 0x00fa: case 0x00fb: case 0x00fc: return "u";
        case 0x00fd: case 0x00ff: return "y";
        case 0x00fe: return "th";
        default: return NULL;
    }
}

static const char *symbol_ascii(uint32_t codepoint) {
    switch (codepoint) {
        case 0x00a0: return " ";
        case 0x00ab: case 0x00bb: return "\"";
        case 0x00b0: return "o";
        case 0x00b7: case 0x2022: case 0x2605: return "*";
        case 0x2010: case 0x2011: case 0x2012: case 0x2013:
        case 0x2014: case 0x2015: case 0x2212: return "-";
        case 0x2018: case 0x2019: case 0x201a: case 0x201b: return "'";
        case 0x201c: case 0x201d: case 0x201e: case 0x201f: return "\"";
        case 0x2026: return "...";
        case 0x20ac: return "EUR";
        case 0x2122: return "TM";
        default: return NULL;
    }
}

size_t controller_text_ascii_normalize(char *out, size_t capacity,
                                       const char *input) {
    if (!out || capacity == 0) return 0;
    size_t used = 0;
    const char *cursor = input ? input : "";

    while (*cursor && used + 1 < capacity) {
        uint32_t codepoint = controller_utf8_decode_next(&cursor);
        if ((codepoint >= 0x20u && codepoint <= 0x7eu) ||
            codepoint == '\n') {
            if (!append_char(out, capacity, &used, (char)codepoint)) break;
            continue;
        }
        /* Decomposed accents follow their base letter and should add no glyph. */
        if (codepoint >= 0x0300u && codepoint <= 0x036fu) continue;

        const char *replacement = latin_ascii(codepoint);
        if (!replacement) replacement = symbol_ascii(codepoint);
        if (!replacement) replacement = "?";
        if (!append_text(out, capacity, &used, replacement)) break;
    }
    out[used] = '\0';
    return used;
}
