// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once
#include "crash_record.h"
// Called after NVS/config initialization. No flash writes in panic context.
void crash_report_capture_boot(void);
// available, none, corrupt, storage_error, unsupported
const char *crash_report_read(crash_record_t *record);
