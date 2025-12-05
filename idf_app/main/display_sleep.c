#include "display_sleep.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "display_sleep";

// Hardware configuration
#define PIN_NUM_BK_LIGHT    ((gpio_num_t)47)
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_SPEED_MODE     LEDC_LOW_SPEED_MODE

// Display sleep configuration (from Kconfig)
#define DISPLAY_DIM_TIMEOUT_MS (CONFIG_RK_DISPLAY_DIM_TIMEOUT_SEC * 1000)
#define DISPLAY_SLEEP_TIMEOUT_MS (CONFIG_RK_DISPLAY_SLEEP_TIMEOUT_SEC * 1000)

// Backlight levels (0-255 for 8-bit PWM, from Kconfig)
#define BACKLIGHT_NORMAL CONFIG_RK_BACKLIGHT_NORMAL
#define BACKLIGHT_DIM CONFIG_RK_BACKLIGHT_DIM

// LVGL task priorities
#define LVGL_TASK_PRIORITY_NORMAL 2
#define LVGL_TASK_PRIORITY_LOW 1

// Mutex for thread safety
#define LOCK_DISPLAY_STATE() xSemaphoreTake(s_display_state_mutex, portMAX_DELAY)
#define UNLOCK_DISPLAY_STATE() xSemaphoreGive(s_display_state_mutex)

// Global state
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static TaskHandle_t s_lvgl_task_handle = NULL;
static esp_timer_handle_t s_dim_timer = NULL;
static esp_timer_handle_t s_sleep_timer = NULL;
static SemaphoreHandle_t s_display_state_mutex = NULL;
static bool s_display_is_on = true;
static bool s_display_is_dimmed = false;

// Set backlight brightness using LEDC PWM
void display_set_backlight(uint8_t brightness) {
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL, brightness));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL));
}

// Dim the display backlight
void display_dim(void) {
    LOCK_DISPLAY_STATE();
    if (s_display_is_on && !s_display_is_dimmed) {
        display_set_backlight(BACKLIGHT_DIM);
        s_display_is_dimmed = true;
        ESP_LOGI(TAG, "Display dimmed (brightness: %d%%)", (BACKLIGHT_DIM * 100) / 255);
    }
    UNLOCK_DISPLAY_STATE();
}

// Put display to sleep
void display_sleep(void) {
    LOCK_DISPLAY_STATE();
    if (s_display_is_on && s_panel_handle != NULL) {
        // Turn off backlight
        display_set_backlight(0);

        // Turn off display panel
        esp_lcd_panel_disp_on_off(s_panel_handle, false);

        // Lower LVGL task priority to save CPU cycles
        if (s_lvgl_task_handle != NULL) {
            vTaskPrioritySet(s_lvgl_task_handle, LVGL_TASK_PRIORITY_LOW);
            ESP_LOGI(TAG, "LVGL task priority lowered");
        }

        s_display_is_on = false;
        s_display_is_dimmed = false;
        ESP_LOGI(TAG, "Display sleeping");
    }
    UNLOCK_DISPLAY_STATE();
}

// Wake up display
void display_wake(void) {
    LOCK_DISPLAY_STATE();

    bool was_off = !s_display_is_on;
    bool was_dimmed = s_display_is_dimmed;

    if (!s_display_is_on && s_panel_handle != NULL) {
        // Turn on display panel first
        esp_lcd_panel_disp_on_off(s_panel_handle, true);

        // Small delay to let panel stabilize
        vTaskDelay(pdMS_TO_TICKS(10));

        // Restore LVGL task priority
        if (s_lvgl_task_handle != NULL) {
            vTaskPrioritySet(s_lvgl_task_handle, LVGL_TASK_PRIORITY_NORMAL);
            ESP_LOGI(TAG, "LVGL task priority restored");
        }

        // Turn on backlight to normal
        display_set_backlight(BACKLIGHT_NORMAL);

        s_display_is_on = true;
        s_display_is_dimmed = false;
        ESP_LOGI(TAG, "Display awake (brightness: %d%%)", (BACKLIGHT_NORMAL * 100) / 255);
    } else if (s_display_is_dimmed) {
        // Just restore brightness if only dimmed
        display_set_backlight(BACKLIGHT_NORMAL);
        s_display_is_dimmed = false;
        ESP_LOGI(TAG, "Display brightness restored");
    }

    UNLOCK_DISPLAY_STATE();

    // Reset timers outside of mutex to avoid deadlock
    if (was_off || was_dimmed) {
        if (s_dim_timer != NULL) {
            esp_timer_stop(s_dim_timer);
            esp_timer_start_once(s_dim_timer, DISPLAY_DIM_TIMEOUT_MS * 1000ULL);
        }
        if (s_sleep_timer != NULL) {
            esp_timer_stop(s_sleep_timer);
            esp_timer_start_once(s_sleep_timer, DISPLAY_SLEEP_TIMEOUT_MS * 1000ULL);
        }
    }
}

// Timer callback for dimming
static void dim_timer_callback(void *arg) {
    display_dim();
}

// Timer callback for sleep
static void sleep_timer_callback(void *arg) {
    display_sleep();
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

    // Create dim timer
    const esp_timer_create_args_t dim_timer_args = {
        .callback = &dim_timer_callback,
        .name = "display_dim"
    };
    ESP_ERROR_CHECK(esp_timer_create(&dim_timer_args, &s_dim_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(s_dim_timer, DISPLAY_DIM_TIMEOUT_MS * 1000ULL));

    // Create sleep timer
    const esp_timer_create_args_t sleep_timer_args = {
        .callback = &sleep_timer_callback,
        .name = "display_sleep"
    };
    ESP_ERROR_CHECK(esp_timer_create(&sleep_timer_args, &s_sleep_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(s_sleep_timer, DISPLAY_SLEEP_TIMEOUT_MS * 1000ULL));

    ESP_LOGI(TAG, "Display sleep initialized (dim: %ds, sleep: %ds)",
             DISPLAY_DIM_TIMEOUT_MS / 1000, DISPLAY_SLEEP_TIMEOUT_MS / 1000);
}

// Activity detected - reset timers and wake if needed
void display_activity_detected(void) {
    // Wake display if sleeping or dimmed
    if (!s_display_is_on || s_display_is_dimmed) {
        display_wake();
    } else {
        // Just reset the timers if already awake
        if (s_dim_timer != NULL) {
            esp_timer_stop(s_dim_timer);
            esp_timer_start_once(s_dim_timer, DISPLAY_DIM_TIMEOUT_MS * 1000ULL);
        }
        if (s_sleep_timer != NULL) {
            esp_timer_stop(s_sleep_timer);
            esp_timer_start_once(s_sleep_timer, DISPLAY_SLEEP_TIMEOUT_MS * 1000ULL);
        }
    }
}

// Check if display is sleeping
bool display_is_sleeping(void) {
    return !s_display_is_on;
}
