#pragma once

#include <stddef.h>

/* The compact RLCD font only contains printable ASCII. Normalize music
 * metadata so unsupported UTF-8 never becomes an LVGL missing-glyph box. */
void rlcd_text_normalize(char *out, size_t out_length, const char *input);
