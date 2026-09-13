#include "frame_power_policy.h"

frame_power_decision_t
frame_power_policy_decide(const frame_power_snapshot_t *snapshot) {
    if (!snapshot || !snapshot->enabled) {
        return FRAME_POWER_BLOCK_DISABLED;
    }
    if (snapshot->now_ms < snapshot->sleep_not_before_ms) {
        return FRAME_POWER_BLOCK_TIMEOUT;
    }
    if (!snapshot->bridge_connected) {
        return FRAME_POWER_BLOCK_BRIDGE;
    }
    if (!snapshot->zone_state_known) {
        return FRAME_POWER_BLOCK_ZONE_UNKNOWN;
    }
    if (snapshot->playing) {
        return FRAME_POWER_BLOCK_PLAYING;
    }
    if (snapshot->provisioning) {
        return FRAME_POWER_BLOCK_PROVISIONING;
    }
    if (snapshot->ui_pending) {
        return FRAME_POWER_BLOCK_UI_PENDING;
    }
    if (snapshot->task_pending) {
        return FRAME_POWER_BLOCK_TASK_PENDING;
    }
    if (snapshot->runtime_transition_pending) {
        return FRAME_POWER_BLOCK_RUNTIME_TRANSITION;
    }
    if (!snapshot->config_durable) {
        return FRAME_POWER_BLOCK_CONFIG_DURABILITY;
    }
    if (!snapshot->wake_button_released) {
        return FRAME_POWER_BLOCK_WAKE_BUTTON;
    }
    if (snapshot->power_source == FRAME_POWER_SOURCE_UNKNOWN) {
        return FRAME_POWER_BLOCK_SOURCE_UNKNOWN;
    }
    if (snapshot->power_source == FRAME_POWER_SOURCE_EXTERNAL) {
        return FRAME_POWER_BLOCK_EXTERNAL_POWER;
    }
    return FRAME_POWER_READY;
}
