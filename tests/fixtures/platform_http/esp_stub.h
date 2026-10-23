#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zlib.h>
#define ESP_OK 0
#define ESP_MAC_WIFI_STA 0
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define HTTP_METHOD_GET 0
#define HTTP_METHOD_POST 1
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define MZ_CRC32_INIT 0
#define mz_crc32 crc32
#define TINFL_DECOMPRESS_MEM_TO_MEM_FAILED ((size_t)-1)
typedef int esp_err_t;
#define HTTP_EVENT_ON_HEADER 1
typedef struct { int event_id; const char *header_key; const char *header_value; void *user_data; } esp_http_client_event_t;
typedef struct {
    const char *url; int method; int timeout_ms;
    esp_err_t (*event_handler)(esp_http_client_event_t *event);
    void *user_data;
} esp_http_client_config_t;
typedef struct { const char *version; } esp_app_desc_t;
typedef void *esp_http_client_handle_t;
void *esp_http_client_init(const esp_http_client_config_t *config);
int esp_http_client_set_header(void *client, const char *name, const char *value);
int esp_http_client_open(void *client, int length);
int esp_http_client_write(void *client, const char *data, int length);
int64_t esp_http_client_fetch_headers(void *client);
int esp_http_client_get_status_code(void *client);
int esp_http_client_read(void *client, char *data, int length);
bool esp_http_client_is_complete_data_received(void *client);
int esp_http_client_close(void *client);
int esp_http_client_cleanup(void *client);
int esp_read_mac(uint8_t *mac, int type);
const esp_app_desc_t *esp_app_get_description(void);
void *heap_caps_calloc(size_t count, size_t size, unsigned caps);
void *heap_caps_realloc(void *ptr, size_t size, unsigned caps);
size_t tinfl_decompress_mem_to_mem(void *out, size_t out_len, const void *in, size_t in_len, int flags);
