// platform_input.h implementation for M5Stack Tough.
//
// Tough has no physical rotational control and no software-visible physical
// buttons (see tough_capabilities.h / docs/esp/hw-reference/board-tough.md --
// power/reset are hardware-only per M5Stack docs, not GPIO-routable). The
// only input surface is the M5Unified touchscreen, and touch is consumed
// directly by touch_ui.cpp with M5GFX callbacks emitting controller actions
// directly -- exactly the same seam-bypass pattern Dial uses for its own
// touch input (see idf_app: touch never round-trips through
// controller_input_mailbox either; only the rotary encoder does).
//
// This file is therefore a legitimate, honest near-no-op: there is no
// physical-control event source on this target to poll, queue, or report
// statistics for.

#include "platform/platform_input.h"

void platform_input_init(void) {
    // No physical controls to configure.
}

void platform_input_process_events(void) {
    // No queued physical-control events exist on this target; touch input is
// consumed directly by M5Unified callbacks, not through this seam.
}

void platform_input_shutdown(void) {
    // Nothing was started.
}

controller_input_mailbox_stats_t platform_input_mailbox_stats(void) {
    // Zeroed: this target never posts into the physical-input mailbox.
    controller_input_mailbox_stats_t stats = {0};
    return stats;
}
