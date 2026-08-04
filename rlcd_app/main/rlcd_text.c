#include "rlcd_text.h"

#include <stdbool.h>

static bool append(char *out, size_t length, size_t *used, char value) {
    if (*used + 1 >= length) return false;
    out[(*used)++] = value;
    return true;
}

static bool append_text(char *out, size_t length, size_t *used, const char *text) {
    while (*text) {
        if (!append(out, length, used, *text++)) return false;
    }
    return true;
}

static char latin1_ascii(unsigned char value) {
    switch (value) {
        case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc4: case 0xc5:
            return 'A';
        case 0xc6: return 'A';
        case 0xc7: return 'C';
        case 0xc8: case 0xc9: case 0xca: case 0xcb: return 'E';
        case 0xcc: case 0xcd: case 0xce: case 0xcf: return 'I';
        case 0xd0: return 'D';
        case 0xd1: return 'N';
        case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6: case 0xd8:
            return 'O';
        case 0xd9: case 0xda: case 0xdb: case 0xdc: return 'U';
        case 0xdd: return 'Y';
        case 0xde: return 'P';
        case 0xdf: return 's';
        case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5:
        case 0xe6: return 'a';
        case 0xe7: return 'c';
        case 0xe8: case 0xe9: case 0xea: case 0xeb: return 'e';
        case 0xec: case 0xed: case 0xee: case 0xef: return 'i';
        case 0xf0: return 'd';
        case 0xf1: return 'n';
        case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6: case 0xf8:
            return 'o';
        case 0xf9: case 0xfa: case 0xfb: case 0xfc: return 'u';
        case 0xfd: case 0xff: return 'y';
        case 0xfe: return 'p';
        default: return 0;
    }
}

void rlcd_text_normalize(char *out, size_t length, const char *input) {
    if (!out || length == 0) return;
    size_t used = 0;
    const unsigned char *cursor = (const unsigned char *)(input ? input : "");

    while (*cursor && used + 1 < length) {
        unsigned char first = *cursor++;
        if (first >= 0x20 && first <= 0x7e) {
            if (!append(out, length, &used, (char)first)) break;
            continue;
        }
        if (first == '\n') {
            if (!append(out, length, &used, '\n')) break;
            continue;
        }
        if (first == 0xc2 && *cursor) {
            unsigned char second = *cursor++;
            if (second == 0xa0) { if (!append(out, length, &used, ' ')) break; continue; }
            if (second == 0xb0) { if (!append(out, length, &used, 'o')) break; continue; }
            if (second == 0xb7) { if (!append(out, length, &used, '*')) break; continue; }
            if (second == 0xab || second == 0xbb) { if (!append(out, length, &used, '"')) break; continue; }
            if (!append(out, length, &used, '?')) break;
            continue;
        }
        if (first == 0xc3 && *cursor) {
            /* U+00C0..U+00FF encode as C3 80..BF. */
            char replacement = latin1_ascii((unsigned char)(*cursor++ | 0x40U));
            if (!append(out, length, &used, replacement ? replacement : '?')) break;
            continue;
        }
        if (first == 0xe2 && cursor[0] && cursor[1]) {
            unsigned char second = *cursor++;
            unsigned char third = *cursor++;
            if (second == 0x80 && (third >= 0x90 && third <= 0x95)) {
                if (!append(out, length, &used, '-')) break;
                continue;
            }
            if (second == 0x80 && (third == 0x98 || third == 0x99)) {
                if (!append(out, length, &used, '\'')) break;
                continue;
            }
            if (second == 0x80 && (third == 0x9c || third == 0x9d)) {
                if (!append(out, length, &used, '"')) break;
                continue;
            }
            if (second == 0x80 && (third == 0xa2 || third == 0xa6)) {
                if (!append_text(out, length, &used, third == 0xa2 ? "*" : "...")) break;
                continue;
            }
            if (second == 0x82 && third == 0xac) {
                if (!append_text(out, length, &used, "EUR")) break;
                continue;
            }
            if (second == 0x84 && third == 0xa2) {
                if (!append_text(out, length, &used, "TM")) break;
                continue;
            }
            if (!append(out, length, &used, '?')) break;
            continue;
        }
        /* Consume the remaining bytes of an otherwise unsupported UTF-8 codepoint. */
        unsigned remaining = first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : first >= 0xc0 ? 1 : 0;
        while (remaining-- && *cursor) ++cursor;
        if (!append(out, length, &used, '?')) break;
    }
    out[used] = '\0';
}
