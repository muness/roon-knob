#include "platform/platform_input.h"

void platform_input_init(void) {}
void platform_input_process_events(void) {}
void platform_input_shutdown(void) {}
controller_input_mailbox_stats_t platform_input_mailbox_stats(void) {
    controller_input_mailbox_stats_t stats = {0};
    return stats;
}
