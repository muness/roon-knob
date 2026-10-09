#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FRAME_POWER_SOURCE_UNKNOWN = 0,
    FRAME_POWER_SOURCE_BATTERY,
    FRAME_POWER_SOURCE_EXTERNAL,
} frame_power_source_t;

typedef enum {
    FRAME_POWER_READY = 0,
    FRAME_POWER_BLOCK_DISABLED,
    FRAME_POWER_BLOCK_SOURCE_UNKNOWN,
    FRAME_POWER_BLOCK_EXTERNAL_POWER,
    FRAME_POWER_BLOCK_TIMEOUT,
    FRAME_POWER_BLOCK_BRIDGE,
    FRAME_POWER_BLOCK_ZONE_UNKNOWN,
    FRAME_POWER_BLOCK_PLAYING,
    FRAME_POWER_BLOCK_PROVISIONING,
    FRAME_POWER_BLOCK_UI_PENDING,
    FRAME_POWER_BLOCK_TASK_PENDING,
    FRAME_POWER_BLOCK_RUNTIME_TRANSITION,
    FRAME_POWER_BLOCK_CONFIG_DURABILITY,
    FRAME_POWER_BLOCK_WAKE_BUTTON,
} frame_power_decision_t;

typedef struct {
    bool enabled;
    frame_power_source_t power_source;
    uint64_t now_ms;
    uint64_t sleep_not_before_ms;
    bool bridge_connected;
    bool zone_state_known;
    bool playing;
    bool provisioning;
    bool ui_pending;
    bool task_pending;
    bool runtime_transition_pending;
    bool config_durable;
    bool wake_button_released;
} frame_power_snapshot_t;

frame_power_decision_t
frame_power_policy_decide(const frame_power_snapshot_t *snapshot);
