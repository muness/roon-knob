#include "rlcd_text.h"
#include "controller_text_ascii.h"

void rlcd_text_normalize(char *out, size_t length, const char *input) {
    (void)controller_text_ascii_normalize(out, length, input);
}
