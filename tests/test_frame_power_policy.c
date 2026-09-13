#include "frame_power_policy.h"

#include <assert.h>
#include <stdio.h>

static frame_power_snapshot_t ready_snapshot(void) {
    return (frame_power_snapshot_t){
        .enabled = true,
        .power_source = FRAME_POWER_SOURCE_BATTERY,
        .now_ms = 2000,
        .sleep_not_before_ms = 1000,
        .bridge_connected = true,
        .zone_state_known = true,
        .playing = false,
        .provisioning = false,
        .ui_pending = false,
        .task_pending = false,
        .runtime_transition_pending = false,
        .config_durable = true,
        .wake_button_released = true,
    };
}

int main(void) {
    frame_power_snapshot_t state = ready_snapshot();
    assert(frame_power_policy_decide(&state) == FRAME_POWER_READY);
    assert(frame_power_policy_decide(NULL) == FRAME_POWER_BLOCK_DISABLED);

    state.enabled = false;
    assert(frame_power_policy_decide(&state) == FRAME_POWER_BLOCK_DISABLED);
    state = ready_snapshot();
    state.power_source = FRAME_POWER_SOURCE_UNKNOWN;
    assert(frame_power_policy_decide(&state) ==
           FRAME_POWER_BLOCK_SOURCE_UNKNOWN);
    state = ready_snapshot();
    state.power_source = FRAME_POWER_SOURCE_EXTERNAL;
    assert(frame_power_policy_decide(&state) ==
           FRAME_POWER_BLOCK_EXTERNAL_POWER);
    state = ready_snapshot();
    state.now_ms = state.sleep_not_before_ms - 1;
    state.power_source = FRAME_POWER_SOURCE_UNKNOWN;
    assert(frame_power_policy_decide(&state) == FRAME_POWER_BLOCK_TIMEOUT);
    state = ready_snapshot();
    state.bridge_connected = false;
    assert(frame_power_policy_decide(&state) == FRAME_POWER_BLOCK_BRIDGE);
    state = ready_snapshot();
    state.zone_state_known = false;
    assert(frame_power_policy_decide(&state) ==
           FRAME_POWER_BLOCK_ZONE_UNKNOWN);
    state = ready_snapshot();
    state.playing = true;
    assert(frame_power_policy_decide(&state) == FRAME_POWER_BLOCK_PLAYING);
    state = ready_snapshot();
    state.provisioning = true;
    assert(frame_power_policy_decide(&state) ==
           FRAME_POWER_BLOCK_PROVISIONING);
    state = ready_snapshot();
    state.ui_pending = true;
    assert(frame_power_policy_decide(&state) == FRAME_POWER_BLOCK_UI_PENDING);
    state = ready_snapshot();
    state.task_pending = true;
    assert(frame_power_policy_decide(&state) ==
           FRAME_POWER_BLOCK_TASK_PENDING);
    state = ready_snapshot();
    state.runtime_transition_pending = true;
    assert(frame_power_policy_decide(&state) ==
           FRAME_POWER_BLOCK_RUNTIME_TRANSITION);
    state = ready_snapshot();
    state.config_durable = false;
    assert(frame_power_policy_decide(&state) ==
           FRAME_POWER_BLOCK_CONFIG_DURABILITY);
    state = ready_snapshot();
    state.wake_button_released = false;
    assert(frame_power_policy_decide(&state) ==
           FRAME_POWER_BLOCK_WAKE_BUTTON);

    puts("frame power policy: ok");
    return 0;
}
