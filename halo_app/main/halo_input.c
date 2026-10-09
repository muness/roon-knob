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
#define HALO_ENC_TRANSITIONS_PER_DETENT 4

static QueueHandle_t s_deltas;
static uint8_t s_enc_prev;
static int s_enc_accum;

static void encoder_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << HALO_ENC_A) | (1ULL << HALO_ENC_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    s_enc_prev = (uint8_t)((gpio_get_level(HALO_ENC_A) << 1) | gpio_get_level(HALO_ENC_B));
}

/* Gray-code transition table: +1 / -1 for valid single-step transitions, 0 for none/invalid. */
static const int8_t k_enc_table[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

static void encoder_poll(void) {
    uint8_t cur = (uint8_t)((gpio_get_level(HALO_ENC_A) << 1) | gpio_get_level(HALO_ENC_B));
    if (cur == s_enc_prev) return;
    s_enc_accum += k_enc_table[(s_enc_prev << 2) | cur];
    s_enc_prev = cur;
    if (abs(s_enc_accum) >= HALO_ENC_TRANSITIONS_PER_DETENT) {
        int detents = s_enc_accum / HALO_ENC_TRANSITIONS_PER_DETENT;
        s_enc_accum -= detents * HALO_ENC_TRANSITIONS_PER_DETENT;
        if (HALO_ENC_INVERT) detents = -detents;
        ESP_LOGD(TAG, "encoder %+d", detents);
        if (s_deltas && xQueueSend(s_deltas, &detents, 0) != pdTRUE) ESP_LOGW(TAG, "encoder queue full");
    }
}

static void input_task(void *arg) {
    (void)arg;
    for (;;) {
        encoder_poll();
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
