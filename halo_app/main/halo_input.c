/* HALO V2 input. Pins established on the test unit by USB-only probing (halo-touch repo evidence):
 *   I2C SDA 48 / SCL 45: CST816 touch @0x15 (chip id 0xB6), INA226 @0x40/0x44, haptic @0x5A
 *   encoder contacts: GPIO6 and GPIO38 (direction not yet verified; flip HALO_ENC_INVERT if needed)
 * Encoder -> volume steps. Touch: tap -> play/pause; horizontal swipe -> next/previous track. */
#include "halo_input.h"
#include "controller_action.h"
#include "controller_command.h"
#include "controller_input.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdlib.h>

static const char *TAG = "halo_input";

#define HALO_I2C_SDA 48
#define HALO_I2C_SCL 45
#define HALO_TOUCH_ADDR 0x15
#define HALO_ENC_A 6
#define HALO_ENC_B 38
#define HALO_ENC_INVERT 0
#define HALO_ENC_TRANSITIONS_PER_DETENT 4

#define SWIPE_MIN_PX 60
#define TAP_MAX_PX 20
#define TAP_MAX_MS 400

static QueueHandle_t s_actions;
static i2c_master_dev_handle_t s_touch;

static void post(controller_action_t action) {
    if (s_actions && xQueueSend(s_actions, &action, 0) != pdTRUE) ESP_LOGW(TAG, "action queue full");
}

/* ---------------- encoder ---------------- */
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
        post(controller_action_command(controller_command_adjust_volume(detents)));
    }
}

/* ---------------- touch ---------------- */
static bool touch_init(void) {
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = HALO_I2C_SDA,
        .scl_io_num = HALO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    if (i2c_new_master_bus(&bus, &bus_handle) != ESP_OK) return false;
    i2c_device_config_t dev = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = HALO_TOUCH_ADDR, .scl_speed_hz = 100000};
    if (i2c_master_bus_add_device(bus_handle, &dev, &s_touch) != ESP_OK) return false;
    uint8_t reg = 0xA7, id[3] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_touch, &reg, 1, id, sizeof id, 50);
    ESP_LOGI(TAG, "CST816 id %02X %02X %02X (%s)", id[0], id[1], id[2], esp_err_to_name(err));
    uint8_t no_autosleep[2] = {0xFE, 0x01}; /* keep the controller awake so polling always answers */
    i2c_master_transmit(s_touch, no_autosleep, sizeof no_autosleep, 50);
    return err == ESP_OK;
}

static bool s_down;
static int s_x0, s_y0, s_x1, s_y1;
static TickType_t s_t0;

static void touch_poll(void) {
    if (!s_touch) return;
    uint8_t reg = 0x02, d[5];
    if (i2c_master_transmit_receive(s_touch, &reg, 1, d, sizeof d, 20) != ESP_OK) return;
    bool down = (d[0] & 0x0F) > 0;
    int x = ((d[1] & 0x0F) << 8) | d[2];
    int y = ((d[3] & 0x0F) << 8) | d[4];
    if (down) {
        if (!s_down) { s_x0 = x; s_y0 = y; s_t0 = xTaskGetTickCount(); }
        s_x1 = x; s_y1 = y;
        s_down = true;
        return;
    }
    if (!s_down) return;
    s_down = false;
    int dx = s_x1 - s_x0, dy = s_y1 - s_y0;
    uint32_t ms = pdTICKS_TO_MS(xTaskGetTickCount() - s_t0);
    if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > abs(dy)) {
        controller_command_kind_t kind = dx < 0 ? CONTROLLER_COMMAND_NEXT_TRACK : CONTROLLER_COMMAND_PREVIOUS_TRACK;
        ESP_LOGI(TAG, "swipe %s (dx %d)", dx < 0 ? "left -> next" : "right -> previous", dx);
        post(controller_action_command(controller_command_make(kind)));
    } else if (abs(dx) <= TAP_MAX_PX && abs(dy) <= TAP_MAX_PX && ms <= TAP_MAX_MS) {
        ESP_LOGI(TAG, "tap -> play/pause");
        post(controller_action_command(controller_command_make(CONTROLLER_COMMAND_TOGGLE_PLAYBACK)));
    }
}

/* ---------------- task ---------------- */
static void input_task(void *arg) {
    (void)arg;
    TickType_t last_touch = 0;
    for (;;) {
        encoder_poll();
        TickType_t now = xTaskGetTickCount();
        if (now - last_touch >= pdMS_TO_TICKS(20)) { touch_poll(); last_touch = now; }
        vTaskDelay(1);
    }
}

void halo_input_start(void) {
    s_actions = xQueueCreate(16, sizeof(controller_action_t));
    encoder_init();
    if (!touch_init()) ESP_LOGW(TAG, "CST816 not responding; touch gestures disabled");
    xTaskCreate(input_task, "halo_input", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "input started: encoder GPIO%d/%d, touch I2C SDA%d/SCL%d", HALO_ENC_A, HALO_ENC_B, HALO_I2C_SDA, HALO_I2C_SCL);
}

void halo_input_drain(void) {
    controller_action_t action;
    while (s_actions && xQueueReceive(s_actions, &action, 0) == pdTRUE) controller_input_dispatch_action(&action);
}
