#pragma once
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef struct esp_timer *esp_timer_handle_t;
typedef struct { void (*callback)(void *); void *arg; const char *name; } esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *h);
esp_err_t esp_timer_stop(esp_timer_handle_t h);
esp_err_t esp_timer_start_once(esp_timer_handle_t h, uint64_t us);
