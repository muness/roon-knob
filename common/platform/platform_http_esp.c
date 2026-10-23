// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "platform/platform_http.h"
#include "platform/platform_identity.h"

#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_app_desc.h>
#include <inttypes.h>
#include <limits.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "miniz.h"

/* Allocation/encoding policy only: response validation is identical on all targets. */
#ifndef PLATFORM_HTTP_JSON_INTERNAL_FALLBACK
#define PLATFORM_HTTP_JSON_INTERNAL_FALLBACK 0
#endif
#ifndef PLATFORM_HTTP_IMAGE_REQUEST_GZIP
#define PLATFORM_HTTP_IMAGE_REQUEST_GZIP 1
#endif
#define HTTP_IMAGE_MAX_BYTES (2U * 1024U * 1024U)

static const char *TAG = "platform_http";

static void *http_response_calloc(size_t count, size_t size) {
    void *ptr = heap_caps_calloc(count, size,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#if PLATFORM_HTTP_JSON_INTERNAL_FALLBACK
    if (!ptr) ptr = heap_caps_calloc(count, size, MALLOC_CAP_8BIT);
#endif
    return ptr;
}

static void *http_response_realloc(void *ptr, size_t size) {
    void *resized = heap_caps_realloc(ptr, size,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#if PLATFORM_HTTP_JSON_INTERNAL_FALLBACK
    if (!resized) resized = heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
#endif
    return resized;
}

static void get_knob_id(char *out, size_t len) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void platform_http_get_knob_id(char *out, size_t len) {
    get_knob_id(out, len);
}

static const char* get_knob_version(void) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    return app_desc->version;
}

static void request_headers(esp_http_client_handle_t client) {
    char knob_id[16];
    get_knob_id(knob_id, sizeof(knob_id));
    esp_http_client_set_header(client, "X-Knob-Id", knob_id);
    esp_http_client_set_header(client, "X-Knob-Version", get_knob_version());
    esp_http_client_set_header(client, "X-Device-Type", platform_device_slug());
    /* Let the bridge own TIME_WAIT for these one-shot requests. */
    esp_http_client_set_header(client, "Connection", "close");
}

typedef struct {
    bool encoding_seen;
    bool gzip;
    bool unsupported_encoding;
} response_headers_t;

static esp_err_t response_header_event(esp_http_client_event_t *event) {
    response_headers_t *headers = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key &&
        event->header_value && strcasecmp(event->header_key, "Content-Encoding") == 0) {
        /* get_header() reads request headers in ESP-IDF, not response headers.
         * Duplicate or compound encodings require a decoder chain we do not support. */
        if (headers->encoding_seen) headers->unsupported_encoding = true;
        headers->encoding_seen = true;
        const char *value = event->header_value;
        size_t length = strlen(value);
        while (length && (*value == ' ' || *value == '\t')) { value++; length--; }
        while (length && (value[length-1] == ' ' || value[length-1] == '\t')) length--;
        headers->gzip = length == 4 && strncasecmp(value, "gzip", length) == 0;
        if (length && !headers->gzip &&
            !(length == 8 && strncasecmp(value, "identity", length) == 0))
            headers->unsupported_encoding = true;
    }
    return ESP_OK;
}

static bool response_complete(esp_http_client_handle_t client,
                              int64_t declared, size_t received) {
    /* fetch_headers returns zero for chunked and close-delimited bodies too.
     * The SDK parser distinguishes a complete message from premature EOF. */
    return (declared <= 0 || (uint64_t)declared == received) &&
           esp_http_client_is_complete_data_received(client);
}

static int http_perform(const char *url, const char *body, size_t limit,
                        bool image, char **out, size_t *out_len, bool *gzip) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!url || !out || !limit || limit == SIZE_MAX) return -1;
    size_t body_len = body ? strlen(body) : 0;
    if (body_len > INT_MAX) return -1;
    response_headers_t headers = {0};
    esp_http_client_config_t config = {
        .event_handler = response_header_event, .user_data = &headers,
        .url = url,
        .method = body ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = image ? 5000 : 3000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -1;
    request_headers(client);
    esp_http_client_set_header(client, "Accept", image ? "*/*" : "application/json");
    esp_http_client_set_header(client, "Accept-Encoding",
                              image && PLATFORM_HTTP_IMAGE_REQUEST_GZIP ? "gzip" : "identity");
    if (body) esp_http_client_set_header(client, "Content-Type", "application/json");
    int result = -1;
    char *buffer = NULL;
    if (esp_http_client_open(client, (int)body_len) != ESP_OK) goto cleanup;
    for (size_t sent = 0; sent < body_len;) {
        int n = esp_http_client_write(client, body + sent, (int)(body_len - sent));
        if (n <= 0 || (size_t)n > body_len - sent) goto cleanup;
        sent += (size_t)n;
    }
    int64_t declared = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (declared < 0 || (uint64_t)declared > limit ||
        (image ? status != 200 : (status < 200 || status >= 300))) {
        ESP_LOGW(TAG, "Rejected HTTP response status=%d length=%" PRId64, status, declared);
        goto cleanup;
    }
    if (headers.unsupported_encoding || (!image && headers.gzip)) goto cleanup;
    if (gzip) *gzip = headers.gzip;

    size_t capacity = declared > 0 ? (size_t)declared + 1 : 4096;
    if (capacity > limit + 1) capacity = limit + 1;
    buffer = image ? calloc(1, capacity) : http_response_calloc(1, capacity);
    if (!buffer) goto cleanup;
    size_t received = 0;
    for (;;) {
        if (declared > 0 && received == (uint64_t)declared) break;
        if (received == limit) {
            char excess;
            if (esp_http_client_read(client, &excess, 1) != 0) goto cleanup;
            break;
        }
        if (received + 1 == capacity) {
            size_t next = capacity > (limit + 1) / 2 ? limit + 1 : capacity * 2;
            char *grown = image ? realloc(buffer, next) : http_response_realloc(buffer, next);
            if (!grown) goto cleanup;
            buffer = grown;
            capacity = next;
        }
        size_t available = capacity - received - 1;
        int count = (int)(available > 4096 ? 4096 : available);
        int n = esp_http_client_read(client, buffer + received, count);
        if (n < 0 || n > count) goto cleanup;
        if (!n) break;
        received += (size_t)n;
    }
    if (!response_complete(client, declared, received)) {
        ESP_LOGW(TAG, "Incomplete HTTP response: received=%zu declared=%" PRId64, received, declared);
        goto cleanup;
    }
    buffer[received] = '\0';
    *out = buffer;
    buffer = NULL;
    if (out_len) *out_len = received;
    result = 0;
cleanup:
    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

int platform_http_get(const char *url, char **out, size_t *out_len) {
    return platform_http_get_bounded(url, PLATFORM_HTTP_JSON_MAX_BYTES, out, out_len);
}
int platform_http_get_bounded(const char *url, size_t max_bytes, char **out, size_t *out_len) {
    return http_perform(url, NULL, max_bytes, false, out, out_len, NULL);
}
int platform_http_post_json(const char *url, const char *json, char **out, size_t *out_len) {
    return http_perform(url, json, PLATFORM_HTTP_JSON_MAX_BYTES, false, out, out_len, NULL);
}
void platform_http_free(char *p) { free(p); }

int platform_http_stream(const char *url, size_t max_bytes,
                         platform_http_stream_callback_t callback, void *ctx,
                         size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!url || !max_bytes || !callback) return -1;
    response_headers_t headers = {0};
    esp_http_client_config_t config = {
        .event_handler = response_header_event, .user_data = &headers,
        .url = url, .method = HTTP_METHOD_GET, .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -1;
    request_headers(client);
    esp_http_client_set_header(client, "Accept", "application/octet-stream");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    int result = -1;
    size_t received = 0;
    if (esp_http_client_open(client, 0) != ESP_OK) goto cleanup;
    int64_t declared = esp_http_client_fetch_headers(client);
    if (declared < 0 || (uint64_t)declared > max_bytes ||
        esp_http_client_get_status_code(client) != 200) goto cleanup;
    if (headers.unsupported_encoding || headers.gzip) goto cleanup;
    uint8_t chunk[1024];
    for (;;) {
        int n = esp_http_client_read(client, (char *)chunk, sizeof(chunk));
        if (n < 0 || n > (int)sizeof(chunk)) goto cleanup;
        if (!n) break;
        if ((size_t)n > max_bytes - received || callback(chunk, (size_t)n, ctx) != 0)
            goto cleanup;
        received += (size_t)n;
    }
    if (!response_complete(client, declared, received)) goto cleanup;
    if (out_len) *out_len = received;
    result = 0;
cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

static size_t decompress_gzip(char **data, size_t compressed_size) {
    if (!data || !*data || compressed_size < 18) return 0;

    uint8_t *gzip_data = (uint8_t *)*data;

    if (gzip_data[0] != 0x1f || gzip_data[1] != 0x8b || gzip_data[2] != 0x08) {
        ESP_LOGE(TAG, "Invalid gzip header");
        return 0;
    }

    uint8_t flags = gzip_data[3];
    if (flags & 0xe0) return 0;
    size_t header_size = 10;

    if (flags & 0x04) {
        if (header_size + 2 > compressed_size) return 0;
        uint16_t xlen = gzip_data[header_size] | (gzip_data[header_size + 1] << 8);
        header_size += 2 + xlen;
    }
    if (flags & 0x08) {
        while (header_size < compressed_size && gzip_data[header_size++] != 0);
    }
    if (flags & 0x10) {
        while (header_size < compressed_size && gzip_data[header_size++] != 0);
    }
    if (flags & 0x02) {
        if (header_size + 2 > compressed_size) return 0;
        uint16_t expected = gzip_data[header_size] | (gzip_data[header_size + 1] << 8);
        if ((uint16_t)mz_crc32(MZ_CRC32_INIT, gzip_data, header_size) != expected) return 0;
        header_size += 2;
    }

    if (header_size + 8 > compressed_size) {
        ESP_LOGE(TAG, "Gzip header exceeds data size");
        return 0;
    }

    uint32_t uncompressed_size = (uint32_t)gzip_data[compressed_size - 4] |
                                  ((uint32_t)gzip_data[compressed_size - 3] << 8) |
                                  ((uint32_t)gzip_data[compressed_size - 2] << 16) |
                                  ((uint32_t)gzip_data[compressed_size - 1] << 24);

    if (uncompressed_size == 0 || uncompressed_size > HTTP_IMAGE_MAX_BYTES) {
        ESP_LOGE(TAG, "Gzip uncompressed size too large: %" PRIu32, uncompressed_size);
        return 0;
    }
    char *decompressed = calloc(1, uncompressed_size);
    if (!decompressed) {
        ESP_LOGE(TAG, "Failed to allocate decompression buffer (%" PRIu32 " bytes)", uncompressed_size);
        return 0;
    }

    const uint8_t *deflate_data = gzip_data + header_size;
    size_t deflate_len = compressed_size - header_size - 8;

    size_t actual_size = tinfl_decompress_mem_to_mem(decompressed, uncompressed_size,
                                                       deflate_data, deflate_len, 0);

    if (actual_size == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        ESP_LOGE(TAG, "Gzip decompression failed");
        free(decompressed);
        return 0;
    }

    if (actual_size != uncompressed_size) {
        ESP_LOGW(TAG, "Gzip size mismatch: expected %" PRIu32 ", got %zu", uncompressed_size, actual_size);
        free(decompressed);
        return 0;
    }

    uint32_t expected_crc = (uint32_t)gzip_data[compressed_size - 8] |
                            ((uint32_t)gzip_data[compressed_size - 7] << 8) |
                            ((uint32_t)gzip_data[compressed_size - 6] << 16) |
                            ((uint32_t)gzip_data[compressed_size - 5] << 24);

    uint32_t actual_crc = mz_crc32(MZ_CRC32_INIT, (const uint8_t*)decompressed, uncompressed_size);

    if (actual_crc != expected_crc) {
        ESP_LOGE(TAG, "Gzip CRC32 mismatch");
        free(decompressed);
        return 0;
    }

    ESP_LOGI(TAG, "Gzip decompressed %zu -> %" PRIu32 " bytes", compressed_size, uncompressed_size);
    free(*data);
    *data = decompressed;
    return uncompressed_size;
}


int platform_http_get_image(const char *url, char **out, size_t *out_len) {
    if (out_len) *out_len = 0;
    size_t length = 0;
    bool gzip = false;
    if (http_perform(url, NULL, HTTP_IMAGE_MAX_BYTES, true, out, &length, &gzip) != 0)
        return -1;
    if (length == 0) goto fail;
    if (gzip || (length >= 2 && (uint8_t)(*out)[0] == 0x1f &&
                               (uint8_t)(*out)[1] == 0x8b)) {
        length = decompress_gzip(out, length);
        if (!length) goto fail;
    }
    if (out_len) *out_len = length;
    return 0;
fail:
    free(*out);
    *out = NULL;
    if (out_len) *out_len = 0;
    return -1;
}
