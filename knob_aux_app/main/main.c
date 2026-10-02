/*
 * Parking firmware for the otherwise-unused classic ESP32 on the exact
 * Waveshare ESP32-S3-Knob-Touch-LCD-1.8 board.
 *
 * The board hard-wires this chip's EN pin high. The primary ESP32-S3 cannot
 * reset or power-gate it, and the factory image starts Classic Bluetooth,
 * A2DP/AVRCP, encoder, audio, and UART services. HiPhi Dial does not consume
 * any of those services, so this companion image puts the audio DAC in its
 * mute/automatic-power-save path and parks the auxiliary SoC in Deep-sleep at
 * the first possible point in app_main. There is intentionally no wake source,
 * connected-idle state, log-drain delay, or task loop: reset or the bootloader
 * wakes it, then this image parks it again.
 */

#include <driver/gpio.h>
#include <esp_sleep.h>

#define PCM5100_XSMT_GPIO GPIO_NUM_32

void app_main(void) {
    const gpio_config_t mute_config = {
        .pin_bit_mask = 1ULL << PCM5100_XSMT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&mute_config);
    (void)gpio_set_level(PCM5100_XSMT_GPIO, 0);

    /* XSMT is not externally biased on this board. Latch it low while the
     * digital GPIO domain is off so the PCM5100A cannot float back active. */
    (void)gpio_hold_en(PCM5100_XSMT_GPIO);
    gpio_deep_sleep_hold_en();

    /* This processor is not part of the product at runtime. Make permanent
     * Deep-sleep an invariant even if startup code later gains a wake source,
     * and retain no RTC memory for a wake stub that must never execute. */
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    (void)esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
    (void)esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
    (void)esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
    esp_deep_sleep_start();
}
