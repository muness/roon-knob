/* HALO V2 encoder. Pins from the factory firmware: A = GPIO38, B = GPIO6.
 * Volume goes through the same physical-event path as the Dial (controller_input_profile_dial.c binds
 * DIAL_BUILTIN/DIAL_ROTATION to volume, or to zone-picker scroll while the picker is open). */
#include "halo_input.h"
#include "controller_input.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdlib.h>

static const char *TAG = "halo_input";

#define HALO_ENC_A 38
#define HALO_ENC_B 6
#define HALO_ENC_INVERT 0

static QueueHandle_t s_deltas;
static uint8_t s_enc_prev;
static volatile uint32_t s_enc_raw_transitions, s_enc_detents_total, s_enc_invalid;

static void encoder_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << HALO_ENC_A) | (1ULL << HALO_ENC_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    s_enc_prev = (uint8_t)((gpio_get_level(HALO_ENC_A) << 1) | gpio_get_level(HALO_ENC_B));
}

/* The HALO knob is NOT a quadrature encoder: it has two independent direction contacts (the factory
 * firmware's "BidiSwitchEncoder"). Both idle high (state AB = 11). Each clockwise detent pulses A low
 * once (11 -> 01 -> 11); each counter-clockwise detent pulses B low once (11 -> 10 -> 11). Measured on
 * the test unit 2026-10-09. A step is counted on a contact's falling edge out of the idle 11 state;
 * simultaneous/bounce transitions (e.g. 01 -> 10) are ignored. A quadrature decoder nets these pulses
 * to zero, which is why volume previously never moved. */
static void encoder_poll(void) {
    uint8_t cur = (uint8_t)((gpio_get_level(HALO_ENC_A) << 1) | gpio_get_level(HALO_ENC_B));
    if (cur == s_enc_prev) return;
    s_enc_raw_transitions++;
    int step = 0;
    if (s_enc_prev == 0x3 && cur == 0x1) step = +1;        /* A fell: clockwise */
    else if (s_enc_prev == 0x3 && cur == 0x2) step = -1;   /* B fell: counter-clockwise */
    else if (cur != 0x3 && s_enc_prev != 0x3) s_enc_invalid++;
    s_enc_prev = cur;
    if (!step) return;
    if (HALO_ENC_INVERT) step = -step;
    s_enc_detents_total++;
    ESP_LOGD(TAG, "encoder %+d", step);
    if (s_deltas && xQueueSend(s_deltas, &step, 0) != pdTRUE) ESP_LOGW(TAG, "encoder queue full");
}

static void input_task(void *arg) {
    (void)arg;
    TickType_t last_report = xTaskGetTickCount();
    uint32_t reported_raw = 0;
    for (;;) {
        encoder_poll();
        if (xTaskGetTickCount() - last_report >= pdMS_TO_TICKS(1000)) {
            last_report = xTaskGetTickCount();
            if (s_enc_raw_transitions != reported_raw) {
                reported_raw = s_enc_raw_transitions;
                ESP_LOGD(TAG, "encoder raw transitions=%lu invalid=%lu detents=%lu A=%d B=%d",
                         (unsigned long)s_enc_raw_transitions, (unsigned long)s_enc_invalid,
                         (unsigned long)s_enc_detents_total,
                         gpio_get_level(HALO_ENC_A), gpio_get_level(HALO_ENC_B));
            }
        }
        vTaskDelay(1);
    }
}

void halo_input_start(void) {
    s_deltas = xQueueCreate(16, sizeof(int));
    encoder_init();
    xTaskCreate(input_task, "halo_input", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "encoder started: A=GPIO%d B=GPIO%d", HALO_ENC_A, HALO_ENC_B);
}

void halo_input_drain(void) {
    int delta, total = 0;
    while (s_deltas && xQueueReceive(s_deltas, &delta, 0) == pdTRUE) total += delta;
    if (total == 0) return;
    controller_physical_event_t event = {
        .source_id = CONTROLLER_INPUT_SOURCE_DIAL_BUILTIN,
        .control_id = CONTROLLER_INPUT_CONTROL_DIAL_ROTATION,
        .kind = CONTROLLER_PHYSICAL_EVENT_ROTATION,
        .gesture = CONTROLLER_PHYSICAL_GESTURE_NONE,
        .value = total,
        .sequence = 0,
        .flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
    };
    (void)controller_input_dispatch_physical(&event);
}
