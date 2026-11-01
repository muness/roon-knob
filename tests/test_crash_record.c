#include "crash_record.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
 crash_record_t r={0};strcpy(r.task,"ui_loop");strcpy(r.target,"hiphi-frame");r.depth=2;r.backtrace[0]=0x42000123;crash_record_seal(&r);
 assert(crash_record_valid(&r,sizeof(r)));assert(!crash_record_valid(&r,sizeof(r)-1));
 r.pc^=1;assert(!crash_record_valid(&r,sizeof(r)));r.pc^=1;
 r.depth=17;crash_record_seal(&r);assert(!crash_record_valid(&r,sizeof(r)));
 r.depth=1;memset(r.target,'x',sizeof(r.target));crash_record_seal(&r);assert(!crash_record_valid(&r,sizeof(r)));
 puts("crash_record_test: ok");
}
