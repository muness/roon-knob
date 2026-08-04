#include "controller_input.h"
#include "controller_input_mailbox.h"
#include "controller_system_frame.h"
#include "platform/platform_task.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

static bool s_provisioning_ready;
static unsigned s_provisioning_calls;
static unsigned s_restart_calls;

bool platform_task_post_to_ui(platform_task_fn_t fn, void *arg) {
    (void)fn;
    (void)arg;
    return false;
}

/* Default target effects must not be used after the test port is installed. */
bool wifi_mgr_start_provisioning(void) {
    assert(false);
    return false;
}

void esp_restart(void) {
    assert(false);
}

static bool fake_start_provisioning(void) {
    s_provisioning_calls++;
    return s_provisioning_ready;
}

static void fake_restart(void) {
    s_restart_calls++;
}

static controller_physical_event_t frame_hold(uint16_t control_id) {
    controller_physical_event_t event = {
        .source_id = CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS,
        .control_id = control_id,
        .kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,
        .gesture = CONTROLLER_PHYSICAL_GESTURE_HOLD,
        .value = 1,
        .sequence = 1,
        .flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
    };
    return event;
}

static void test_pre_ready_provisioning_retries_then_succeeds(void) {
    const controller_system_frame_effects_t effects = {
        .start_provisioning = fake_start_provisioning,
        .restart = fake_restart,
    };
    controller_input_safety_latch_t latch;
    controller_physical_event_t boot =
        frame_hold(CONTROLLER_INPUT_CONTROL_FRAME_BOOT);

    s_provisioning_ready = false;
    s_provisioning_calls = 0;
    controller_input_safety_latch_reset(&latch);
    controller_system_frame_init();
    controller_system_frame_set_effects_for_test(&effects);
    assert(controller_input_set_context(
        CONTROLLER_INTERACTION_CONTEXT_MEDIA));
    assert(!controller_input_dispatch_physical(&boot));
    assert(s_provisioning_calls == 0);

    assert(controller_input_safety_latch_offer(&latch, 1));
    assert(controller_input_safety_latch_take(&latch, 1));
    assert(!controller_system_frame_dispatch_physical(&boot));
    controller_input_safety_latch_retry(&latch, 1);
    assert(latch.pending_bits == 1);
    assert(s_provisioning_calls == 1);

    s_provisioning_ready = true;
    assert(controller_input_safety_latch_take(&latch, 1));
    assert(controller_system_frame_dispatch_physical(&boot));
    assert(latch.pending_bits == 0);
    assert(s_provisioning_calls == 2);

    controller_system_frame_shutdown();
}

static void test_restart_uses_actual_locked_frame_profile(void) {
    const controller_system_frame_effects_t effects = {
        .start_provisioning = fake_start_provisioning,
        .restart = fake_restart,
    };
    controller_physical_event_t gp4 =
        frame_hold(CONTROLLER_INPUT_CONTROL_FRAME_GP4);

    s_restart_calls = 0;
    controller_system_frame_init();
    controller_system_frame_set_effects_for_test(&effects);
    assert(controller_input_set_context(
        CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY));
    assert(controller_system_frame_dispatch_physical(&gp4));
    assert(s_restart_calls == 1);

    controller_system_frame_shutdown();
}

int main(void) {
    test_pre_ready_provisioning_retries_then_succeeds();
    test_restart_uses_actual_locked_frame_profile();
    puts("controller system Frame contracts passed");
    return 0;
}
