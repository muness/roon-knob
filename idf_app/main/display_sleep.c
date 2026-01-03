#include "display_sleep.h"
#include "captive_portal.h"
#include "platform/platform_display.h"
#include "roon_client.h"
#include "wifi_manager.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ui.h"

static const char *TAG = "display_sleep";

// Hardware configuration
#define PIN_NUM_BK_LIGHT    ((gpio_num_t)47)
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_SPEED_MODE     LEDC_LOW_SPEED_MODE

// Default timeout configuration (use charging defaults for cold start - more generous during setup)
#define DEFAULT_ART_MODE_TIMEOUT_MS (RK_DEFAULT_ART_MODE_CHARGING_TIMEOUT_SEC * 1000)
#define DEFAULT_DIM_TIMEOUT_MS (RK_DEFAULT_DIM_CHARGING_TIMEOUT_SEC * 1000)
#define DEFAULT_SLEEP_TIMEOUT_MS (RK_DEFAULT_SLEEP_CHARGING_TIMEOUT_SEC * 1000)

// Backlight levels (0-255 for 8-bit PWM, from Kconfig)
#define BACKLIGHT_NORMAL CONFIG_RK_BACKLIGHT_NORMAL
#define BACKLIGHT_DIM CONFIG_RK_BACKLIGHT_DIM

// LVGL task priorities
#define LVGL_TASK_PRIORITY_NORMAL 2
#define LVGL_TASK_PRIORITY_LOW 1

// Touch suppression after wake to prevent accidental widget activation
#define TOUCH_SUPPRESS_AFTER_WAKE_MS 250

// Mutex for thread safety
#define LOCK_DISPLAY_STATE() xSemaphoreTake(s_display_state_mutex, portMAX_DELAY)
#define UNLOCK_DISPLAY_STATE() xSemaphoreGive(s_display_state_mutex)

// Global state
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static TaskHandle_t s_lvgl_task_handle = NULL;
static esp_timer_handle_t s_art_mode_timer = NULL;
static esp_timer_handle_t s_dim_timer = NULL;
static esp_timer_handle_t s_sleep_timer = NULL;
static SemaphoreHandle_t s_display_state_mutex = NULL;
static display_state_t s_display_state = DISPLAY_STATE_NORMAL;
static int64_t s_touch_suppress_until_ms = 0;  // Suppress widget touches after wake

// Current timeout values (in ms, 0 = disabled)
static uint32_t s_art_mode_timeout_ms = DEFAULT_ART_MODE_TIMEOUT_MS;
static uint32_t s_dim_timeout_ms = DEFAULT_DIM_TIMEOUT_MS;
static uint32_t s_sleep_timeout_ms = DEFAULT_SLEEP_TIMEOUT_MS;

// Power management settings
static bool s_wifi_power_save_enabled = false;
static bool s_cpu_freq_scaling_enabled = false;

#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_pm_cpu_lock = NULL;
static bool s_pm_initialized = false;

// Initialize power management (call once during init)
static void pm_init(void) {
    if (s_pm_initialized) return;

    // Configure PM: allow scaling between 80MHz (min) and 240MHz (max)
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,  // Don't auto-sleep, we control display separately
    };

    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure PM: %s", esp_err_to_name(err));
        return;
    }

    // Create a CPU frequency lock - when held, keeps CPU at max freq
    err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "display", &s_pm_cpu_lock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create PM lock: %s", esp_err_to_name(err));
        return;
    }

    // Start with lock held (display is awake at boot)
    esp_pm_lock_acquire(s_pm_cpu_lock);

    s_pm_initialized = true;
    ESP_LOGI(TAG, "Power management initialized (80-240MHz scaling)");
}
#endif

// Set backlight brightness using LEDC PWM
void display_set_backlight(uint8_t brightness) {
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL, brightness));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL));
}

// Get current display state
display_state_t display_get_state(void) {
    return s_display_state;
}

// Enter art mode - hide controls, keep full brightness
void display_art_mode(void) {
    bool entered_art_mode = false;
    uint32_t dim_timeout = 0;

    LOCK_DISPLAY_STATE();
    if (s_display_state == DISPLAY_STATE_NORMAL) {
        s_display_state = DISPLAY_STATE_ART_MODE;
        ui_set_controls_visible(false);
        entered_art_mode = true;
        dim_timeout = s_dim_timeout_ms;
        ESP_LOGI(TAG, "Display entering art mode");
    }
    UNLOCK_DISPLAY_STATE();

    // Timer operations outside mutex to avoid deadlock (consistent with display_wake)
    if (entered_art_mode) {
        if (s_art_mode_timer != NULL) {
            esp_timer_stop(s_art_mode_timer);
        }
        // Start dim timer from art mode (if dim is enabled)
        if (s_dim_timer != NULL && dim_timeout > 0) {
            esp_timer_stop(s_dim_timer);
            esp_timer_start_once(s_dim_timer, dim_timeout * 1000ULL);
        }
    }
}

// Dim the display backlight
void display_dim(void) {
    bool entered_dim = false;
    uint32_t sleep_timeout = 0;

    LOCK_DISPLAY_STATE();
    if (s_display_state == DISPLAY_STATE_NORMAL || s_display_state == DISPLAY_STATE_ART_MODE) {
        display_set_backlight(BACKLIGHT_DIM);
        ui_set_controls_visible(false);
        s_display_state = DISPLAY_STATE_DIM;
        entered_dim = true;
        sleep_timeout = s_sleep_timeout_ms;
        ESP_LOGI(TAG, "Display dimmed (brightness: %d%%)", (BACKLIGHT_DIM * 100) / 255);
    }
    UNLOCK_DISPLAY_STATE();

    // Timer operations outside mutex to avoid deadlock
    if (entered_dim) {
        // Stop art mode and dim timers (we're past those stages)
        if (s_art_mode_timer != NULL) {
            esp_timer_stop(s_art_mode_timer);
        }
        if (s_dim_timer != NULL) {
            esp_timer_stop(s_dim_timer);
        }
        // Start sleep timer (if sleep is enabled)
        if (s_sleep_timer != NULL && sleep_timeout > 0) {
            esp_timer_stop(s_sleep_timer);
            esp_timer_start_once(s_sleep_timer, sleep_timeout * 1000ULL);
        }
    }
}

// Put display to sleep
void display_sleep(void) {
    LOCK_DISPLAY_STATE();
    if (s_display_state != DISPLAY_STATE_SLEEP && s_panel_handle != NULL) {
        // Turn off backlight
        display_set_backlight(0);

        // Turn off display panel
        esp_lcd_panel_disp_on_off(s_panel_handle, false);

        // Lower LVGL task priority to save CPU cycles
        if (s_lvgl_task_handle != NULL) {
            vTaskPrioritySet(s_lvgl_task_handle, LVGL_TASK_PRIORITY_LOW);
            ESP_LOGI(TAG, "LVGL task priority lowered");
        }

        // Enable WiFi power save if configured
        if (s_wifi_power_save_enabled) {
            wifi_mgr_set_power_save(true);
        }

        // Release CPU frequency lock to allow scaling down
#if CONFIG_PM_ENABLE
        if (s_cpu_freq_scaling_enabled && s_pm_initialized && s_pm_cpu_lock) {
            esp_pm_lock_release(s_pm_cpu_lock);
            ESP_LOGI(TAG, "CPU frequency lock released (scaling enabled)");
        }
#endif

        s_display_state = DISPLAY_STATE_SLEEP;
        ESP_LOGI(TAG, "Display sleeping");
    }
    UNLOCK_DISPLAY_STATE();
}

// Wake up display to normal state
void display_wake(void) {
    display_state_t prev_state;
    uint32_t art_timeout = 0;
    uint32_t dim_timeout = 0;
    uint32_t sleep_timeout = 0;

    LOCK_DISPLAY_STATE();

    prev_state = s_display_state;
    art_timeout = s_art_mode_timeout_ms;
    dim_timeout = s_dim_timeout_ms;
    sleep_timeout = s_sleep_timeout_ms;

    if (s_display_state == DISPLAY_STATE_SLEEP && s_panel_handle != NULL) {
        // Acquire CPU frequency lock first (need full performance)
#if CONFIG_PM_ENABLE
        if (s_cpu_freq_scaling_enabled && s_pm_initialized && s_pm_cpu_lock) {
            esp_pm_lock_acquire(s_pm_cpu_lock);
            ESP_LOGI(TAG, "CPU frequency lock acquired (max freq)");
        }
#endif

        // Disable WiFi power save (need full performance for responsive polling)
        if (s_wifi_power_save_enabled) {
            wifi_mgr_set_power_save(false);
        }

        // Turn on display panel first
        esp_lcd_panel_disp_on_off(s_panel_handle, true);

        // Small delay to let panel stabilize
        vTaskDelay(pdMS_TO_TICKS(10));

        // Restore LVGL task priority
        if (s_lvgl_task_handle != NULL) {
            vTaskPrioritySet(s_lvgl_task_handle, LVGL_TASK_PRIORITY_NORMAL);
            ESP_LOGI(TAG, "LVGL task priority restored");
        }
    }

    if (s_display_state != DISPLAY_STATE_NORMAL) {
        // Restore full brightness
        display_set_backlight(BACKLIGHT_NORMAL);
        // Show controls
        ui_set_controls_visible(true);
        s_display_state = DISPLAY_STATE_NORMAL;
        // Suppress widget touches after wake to prevent accidental activation
        s_touch_suppress_until_ms = esp_timer_get_time() / 1000 + TOUCH_SUPPRESS_AFTER_WAKE_MS;
        ESP_LOGI(TAG, "Display awake (brightness: %d%%)", (BACKLIGHT_NORMAL * 100) / 255);
    }

    UNLOCK_DISPLAY_STATE();

    // Reset timers outside of mutex to avoid deadlock
    // Sequential chain: start only the first enabled timer, each transition starts the next
    if (prev_state != DISPLAY_STATE_NORMAL) {
        // Stop all timers first
        if (s_art_mode_timer != NULL) esp_timer_stop(s_art_mode_timer);
        if (s_dim_timer != NULL) esp_timer_stop(s_dim_timer);
        if (s_sleep_timer != NULL) esp_timer_stop(s_sleep_timer);

        // Start the first enabled timer in the chain
        if (art_timeout > 0 && s_art_mode_timer != NULL) {
            esp_timer_start_once(s_art_mode_timer, art_timeout * 1000ULL);
        } else if (dim_timeout > 0 && s_dim_timer != NULL) {
            esp_timer_start_once(s_dim_timer, dim_timeout * 1000ULL);
        } else if (sleep_timeout > 0 && s_sleep_timer != NULL) {
            esp_timer_start_once(s_sleep_timer, sleep_timeout * 1000ULL);
        }
    }
}

// Pending state changes (set by timer callbacks, processed in UI loop)
static volatile bool s_pending_art_mode = false;
static volatile bool s_pending_dim = false;
static volatile bool s_pending_sleep = false;

// Timer callback for art mode
static void art_mode_timer_callback(void *arg) {
    s_pending_art_mode = true;  // Defer to UI loop
}

// Timer callback for dimming
static void dim_timer_callback(void *arg) {
    s_pending_dim = true;  // Defer to UI loop
}

// Timer callback for sleep
static void sleep_timer_callback(void *arg) {
    s_pending_sleep = true;  // Defer to UI loop
}

// Process pending display state changes (call from UI loop)
void display_process_pending(void) {
    // Don't enter art mode during setup (captive portal active or bridge unreachable)
    // But allow dim/sleep to work regardless of bridge state
    if (captive_portal_is_running() || !roon_client_is_ready_for_art_mode()) {
        s_pending_art_mode = false;  // Only block art mode entry, not dim/sleep
    }

    if (s_pending_art_mode) {
        s_pending_art_mode = false;
        // Only transition to art mode from normal state
        if (s_display_state == DISPLAY_STATE_NORMAL) {
            display_art_mode();
        }
    }
    if (s_pending_dim) {
        s_pending_dim = false;
        display_dim();
    }
    if (s_pending_sleep) {
        s_pending_sleep = false;
        display_sleep();
    }
}

// Initialize sleep timer
void display_sleep_init(esp_lcd_panel_handle_t panel_handle, TaskHandle_t lvgl_task_handle) {
    ESP_LOGI(TAG, "Initializing display sleep management");

    // Create mutex for thread safety
    s_display_state_mutex = xSemaphoreCreateMutex();
    if (s_display_state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create display state mutex");
        return;
    }

    s_panel_handle = panel_handle;
    s_lvgl_task_handle = lvgl_task_handle;

    // Create art mode timer
    const esp_timer_create_args_t art_mode_timer_args = {
        .callback = &art_mode_timer_callback,
        .name = "display_art_mode"
    };
    ESP_ERROR_CHECK(esp_timer_create(&art_mode_timer_args, &s_art_mode_timer));

    // Create dim timer
    const esp_timer_create_args_t dim_timer_args = {
        .callback = &dim_timer_callback,
        .name = "display_dim"
    };
    ESP_ERROR_CHECK(esp_timer_create(&dim_timer_args, &s_dim_timer));

    // Create sleep timer
    const esp_timer_create_args_t sleep_timer_args = {
        .callback = &sleep_timer_callback,
        .name = "display_sleep"
    };
    ESP_ERROR_CHECK(esp_timer_create(&sleep_timer_args, &s_sleep_timer));

    // Start the first enabled timer in the chain
    if (s_art_mode_timeout_ms > 0) {
        ESP_ERROR_CHECK(esp_timer_start_once(s_art_mode_timer, s_art_mode_timeout_ms * 1000ULL));
    } else if (s_dim_timeout_ms > 0) {
        ESP_ERROR_CHECK(esp_timer_start_once(s_dim_timer, s_dim_timeout_ms * 1000ULL));
    } else if (s_sleep_timeout_ms > 0) {
        ESP_ERROR_CHECK(esp_timer_start_once(s_sleep_timer, s_sleep_timeout_ms * 1000ULL));
    }

    ESP_LOGI(TAG, "Display sleep initialized (art: %lums, dim: %lums, sleep: %lums)",
             s_art_mode_timeout_ms, s_dim_timeout_ms, s_sleep_timeout_ms);
}

// Activity detected - reset timers and wake if needed
void display_activity_detected(void) {
    display_state_t current_state = display_get_state();
    uint32_t art_timeout = s_art_mode_timeout_ms;
    uint32_t dim_timeout = s_dim_timeout_ms;
    uint32_t sleep_timeout = s_sleep_timeout_ms;

    // Wake display if in art mode, dimmed, or sleeping (tap shows controls at normal brightness)
    if (current_state == DISPLAY_STATE_ART_MODE || current_state == DISPLAY_STATE_DIM || current_state == DISPLAY_STATE_SLEEP) {
        display_wake();
        return;  // display_wake already starts the timer chain
    }

    // Reset timer chain for NORMAL state (only remaining case after wake check)
    // Stop all timers first
    if (s_art_mode_timer != NULL) esp_timer_stop(s_art_mode_timer);
    if (s_dim_timer != NULL) esp_timer_stop(s_dim_timer);
    if (s_sleep_timer != NULL) esp_timer_stop(s_sleep_timer);

    // From NORMAL state, start at beginning of timer chain
    if (art_timeout > 0 && s_art_mode_timer != NULL) {
        esp_timer_start_once(s_art_mode_timer, art_timeout * 1000ULL);
    } else if (dim_timeout > 0 && s_dim_timer != NULL) {
        esp_timer_start_once(s_dim_timer, dim_timeout * 1000ULL);
    } else if (sleep_timeout > 0 && s_sleep_timer != NULL) {
        esp_timer_start_once(s_sleep_timer, sleep_timeout * 1000ULL);
    }
}

// Check if display is sleeping
bool display_is_sleeping(void) {
    return s_display_state == DISPLAY_STATE_SLEEP;
}

// Check if widget touches should be suppressed (within 250ms after wake)
bool display_is_touch_suppressed(void) {
    int64_t now_ms = esp_timer_get_time() / 1000;
    return now_ms < s_touch_suppress_until_ms;
}

// Update timeout values from config
void display_update_timeouts(const rk_cfg_t *cfg, bool is_charging) {
    uint32_t new_art_timeout_ms;
    uint32_t new_dim_timeout_ms;
    uint32_t new_sleep_timeout_ms;

    if (cfg != NULL) {
        // Get timeouts from config based on charging state (convert seconds to ms)
        new_art_timeout_ms = rk_cfg_get_art_mode_timeout(cfg, is_charging) * 1000;
        new_dim_timeout_ms = rk_cfg_get_dim_timeout(cfg, is_charging) * 1000;
        new_sleep_timeout_ms = rk_cfg_get_sleep_timeout(cfg, is_charging) * 1000;
    } else {
        // Use defaults from Kconfig
        new_art_timeout_ms = DEFAULT_ART_MODE_TIMEOUT_MS;
        new_dim_timeout_ms = DEFAULT_DIM_TIMEOUT_MS;
        new_sleep_timeout_ms = DEFAULT_SLEEP_TIMEOUT_MS;
    }

    // Check if any values changed
    if (new_art_timeout_ms == s_art_mode_timeout_ms &&
        new_dim_timeout_ms == s_dim_timeout_ms &&
        new_sleep_timeout_ms == s_sleep_timeout_ms) {
        return;  // No change
    }

    ESP_LOGI(TAG, "Updating display timeouts (art: %lums, dim: %lums, sleep: %lums)",
             new_art_timeout_ms, new_dim_timeout_ms, new_sleep_timeout_ms);

    // Update stored values
    s_art_mode_timeout_ms = new_art_timeout_ms;
    s_dim_timeout_ms = new_dim_timeout_ms;
    s_sleep_timeout_ms = new_sleep_timeout_ms;

    // Restart timer chain based on current state
    display_state_t current_state = display_get_state();

    // Stop all timers
    if (s_art_mode_timer != NULL) esp_timer_stop(s_art_mode_timer);
    if (s_dim_timer != NULL) esp_timer_stop(s_dim_timer);
    if (s_sleep_timer != NULL) esp_timer_stop(s_sleep_timer);

    // Restart appropriate timer for current state
    if (current_state == DISPLAY_STATE_NORMAL) {
        if (s_art_mode_timeout_ms > 0 && s_art_mode_timer != NULL) {
            esp_timer_start_once(s_art_mode_timer, s_art_mode_timeout_ms * 1000ULL);
        } else if (s_dim_timeout_ms > 0 && s_dim_timer != NULL) {
            esp_timer_start_once(s_dim_timer, s_dim_timeout_ms * 1000ULL);
        } else if (s_sleep_timeout_ms > 0 && s_sleep_timer != NULL) {
            esp_timer_start_once(s_sleep_timer, s_sleep_timeout_ms * 1000ULL);
        }
    } else if (current_state == DISPLAY_STATE_ART_MODE) {
        if (s_dim_timeout_ms > 0 && s_dim_timer != NULL) {
            esp_timer_start_once(s_dim_timer, s_dim_timeout_ms * 1000ULL);
        } else if (s_sleep_timeout_ms > 0 && s_sleep_timer != NULL) {
            esp_timer_start_once(s_sleep_timer, s_sleep_timeout_ms * 1000ULL);
        }
    } else if (current_state == DISPLAY_STATE_DIM) {
        if (s_sleep_timeout_ms > 0 && s_sleep_timer != NULL) {
            esp_timer_start_once(s_sleep_timer, s_sleep_timeout_ms * 1000ULL);
        }
    }
    // DISPLAY_STATE_SLEEP: no timers to start
}

// Update power management settings from config
void display_update_power_settings(const rk_cfg_t *cfg) {
    if (!cfg) return;

    bool wifi_changed = (s_wifi_power_save_enabled != (cfg->wifi_power_save_enabled != 0));
    bool cpu_changed = (s_cpu_freq_scaling_enabled != (cfg->cpu_freq_scaling_enabled != 0));

    s_wifi_power_save_enabled = cfg->wifi_power_save_enabled != 0;
    s_cpu_freq_scaling_enabled = cfg->cpu_freq_scaling_enabled != 0;

    if (wifi_changed || cpu_changed) {
        ESP_LOGI(TAG, "Power settings updated: wifi_ps=%d, cpu_scaling=%d",
                 s_wifi_power_save_enabled, s_cpu_freq_scaling_enabled);
    }

    // If currently sleeping and wifi power save setting changed, apply immediately
    if (wifi_changed && s_display_state == DISPLAY_STATE_SLEEP) {
        wifi_mgr_set_power_save(s_wifi_power_save_enabled);
    }

#if CONFIG_PM_ENABLE
    // Initialize PM when CPU scaling is first enabled
    if (cpu_changed && s_cpu_freq_scaling_enabled) {
        pm_init();
        // If currently sleeping, release the lock to enable scaling
        if (s_display_state == DISPLAY_STATE_SLEEP && s_pm_initialized && s_pm_cpu_lock) {
            esp_pm_lock_release(s_pm_cpu_lock);
            ESP_LOGI(TAG, "CPU scaling enabled while sleeping - lock released");
        }
    }
#endif
}
