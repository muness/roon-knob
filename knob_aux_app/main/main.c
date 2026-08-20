/*
 * Parking firmware for the otherwise-unused classic ESP32 on the exact
 * Waveshare ESP32-S3-Knob-Touch-LCD-1.8 board.
 *
 * The board hard-wires this chip's EN pin high. The primary ESP32-S3 cannot
 * reset or power-gate it, and the factory image starts Classic Bluetooth,
 * A2DP/AVRCP, encoder, audio, and UART services. HiPhi Dial does not consume
 * any of those services, so this companion image puts the audio DAC in its
 * mute/automatic-power-save path and parks the auxiliary SoC in Deep-sleep.
 * There is intentionally no wake source: reset or the bootloader wakes it,
 * then this image parks it again.
 */

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define PCM5100_XSMT_GPIO GPIO_NUM_32

static const char *TAG = "knob_aux_park";

void app_main(void) {
    const gpio_config_t mute_config = {
        .pin_bit_mask = 1ULL << PCM5100_XSMT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&mute_config));
    ESP_ERROR_CHECK(gpio_set_level(PCM5100_XSMT_GPIO, 0));

    /* XSMT is not externally biased on this board. Latch it low while the
     * digital GPIO domain is off so the PCM5100A cannot float back active. */
    ESP_ERROR_CHECK(gpio_hold_en(PCM5100_XSMT_GPIO));
    gpio_deep_sleep_hold_en();

    ESP_LOGW(TAG, "Auxiliary ESP32 parked; entering Deep-sleep");
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_deep_sleep_start();
}
