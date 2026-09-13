#pragma once

#include <stddef.h>

/* Normalize UTF-8 metadata for controller displays whose compact fonts only
 * contain printable ASCII. The output is always NUL-terminated when capacity
 * is non-zero. Unsupported code points become one '?', never one per UTF-8
 * byte. Returns the number of output bytes, excluding the terminator. */
size_t controller_text_ascii_normalize(char *out, size_t capacity,
                                       const char *input);
