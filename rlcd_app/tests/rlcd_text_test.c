#include "rlcd_text.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char output[128];
    rlcd_text_normalize(output, sizeof(output), "Beyonc\xc3\xa9 \xe2\x80\x94 R\xc3\xb3n\xe2\x80\x99s \xe2\x80\x9cSet\xe2\x80\x9d");
    assert(strcmp(output, "Beyonce - Ron's \"Set\"") == 0);
    rlcd_text_normalize(output, sizeof(output), "A\xc2\xa0" "B \xe2\x80\xa2" " C \xf0\x9f\x8e\xb5");
    assert(strcmp(output, "A B * C ?") == 0);
    rlcd_text_normalize(output, sizeof(output),
                        "SMOKE HOUR \xe2\x98\x85 WILLIE NELSON - "
                        "Beyonc\xc3\xa9 - COWBOY CARTER");
    assert(strcmp(output,
                  "SMOKE HOUR * WILLIE NELSON - Beyonce - COWBOY CARTER") == 0);
    rlcd_text_normalize(output, sizeof(output), "Cafe\xcc\x81");
    assert(strcmp(output, "Cafe") == 0);
    puts("rlcd_text_test: ok");
    return 0;
}
