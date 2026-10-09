#include "platform/platform_input.h"
#include "halo_input.h"

void platform_input_init(void) { halo_input_start(); }
void platform_input_process_events(void) { halo_input_drain(); }
void platform_input_shutdown(void) {}
controller_input_mailbox_stats_t platform_input_mailbox_stats(void) {
    controller_input_mailbox_stats_t stats = {0};
    return stats;
}
