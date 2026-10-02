#pragma once

#include <stdint.h>

// Decode one UTF-8 code point and advance *cursor. Invalid input consumes one
// byte and returns U+FFFD. A NUL terminator returns 0 without advancing.
uint32_t controller_utf8_decode_next(const char **cursor);
