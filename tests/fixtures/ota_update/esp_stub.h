#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_IMAGE_HEADER_MAGIC 0xe9
#define ESP_IMAGE_MAX_SEGMENTS 16
#define ESP_CHIP_ID_ESP32S3 9
#define ESP_IMAGE_FLASH_SIZE_8MB 3
#define ESP_IMAGE_FLASH_SIZE_16MB 4
#define ESP_APP_DESC_MAGIC_WORD 0xabcd5432U
/* Preserve the pinned ESP-IDF5.5.5 wire layout, including the size nibble. */
typedef struct __attribute__((packed)) {
    uint8_t magic, segment_count, spi_mode;
    uint8_t spi_speed:4, spi_size:4;
    uint32_t entry_addr;
    uint8_t wp_pin, spi_pin_drv[3];
    uint16_t chip_id;
    uint8_t min_chip_rev;
    uint16_t min_chip_rev_full, max_chip_rev_full;
    uint8_t reserved[4], hash_appended;
} esp_image_header_t;
typedef struct { uint32_t load_addr, data_len; } esp_image_segment_header_t;
typedef struct {
    uint32_t magic_word, secure_version, reserv1[2];
    char version[32], project_name[32], time[16], date[16], idf_ver[32];
    uint8_t app_elf_sha256[32], tail[80];
} esp_app_desc_t;
_Static_assert(sizeof(esp_image_header_t) == 24, "IDF image header layout");
_Static_assert(sizeof(esp_app_desc_t) == 256, "IDF descriptor layout");
_Static_assert(offsetof(esp_app_desc_t, project_name) == 48, "IDF project offset");
typedef void *TaskHandle_t;
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(int ticks);
int xTaskCreate(void (*task)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
const char *esp_err_to_name(esp_err_t error);
typedef struct { const char *url; int timeout_ms, buffer_size; } esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t, int);
int esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
typedef struct { const char *label; size_t size; } esp_partition_t;
typedef int esp_ota_handle_t;
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
esp_err_t esp_ota_begin(const esp_partition_t *, size_t, esp_ota_handle_t *);
esp_err_t esp_ota_write(esp_ota_handle_t, const void *, size_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
const esp_app_desc_t *esp_app_get_description(void);
void esp_restart(void);
