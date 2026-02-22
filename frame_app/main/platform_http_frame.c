// platform_http.h implementation for hiphi frame — copied from idf_app/platform_http_idf.c
// Identical functionality: HTTP GET/POST, image download with gzip support

#include "platform/platform_http.h"

#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_app_desc.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "miniz.h"

static const char *TAG = "platform_http";

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

static int http_perform(const char *url, const char *body, const char *content_type, char **out, size_t *out_len) {
    ESP_LOGD(TAG, "HTTP %s: %s", body ? "POST" : "GET", url);

    esp_http_client_config_t config = {
        .url = url,
        .method = body ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = 3000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return -1;
    }

    esp_http_client_set_header(client, "Accept", "application/json");
    if (body) {
        esp_http_client_set_header(client, "Content-Type", content_type ? content_type : "application/json");
    }

    char knob_id[16];
    get_knob_id(knob_id, sizeof(knob_id));
    esp_http_client_set_header(client, "X-Knob-Id", knob_id);
    esp_http_client_set_header(client, "X-Knob-Version", get_knob_version());

    esp_err_t err = esp_http_client_open(client, body ? strlen(body) : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }

    if (body) {
        int wlen = esp_http_client_write(client, body, strlen(body));
        if (wlen < 0) {
            ESP_LOGE(TAG, "Write failed");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return -1;
        }
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        ESP_LOGE(TAG, "HTTP fetch headers failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -1;
    }

    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGD(TAG, "HTTP Status=%d, content_length=%d", status_code, content_length);

    // Handle chunked transfer (content_length == 0) with dynamic buffering
    size_t buf_size = (content_length > 0) ? (size_t)(content_length + 1) : 4096;
    char *buffer = calloc(1, buf_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -1;
    }

    int total_read = 0;
    while (1) {
        if ((size_t)(total_read + 1024) > buf_size) {
            size_t new_size = buf_size * 2;
            if (new_size > 512 * 1024) {  // 512 KB cap for JSON responses
                ESP_LOGE(TAG, "Response too large (>512KB)");
                free(buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return -1;
            }
            char *new_buf = realloc(buffer, new_size);
            if (!new_buf) {
                ESP_LOGE(TAG, "Failed to grow response buffer");
                free(buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return -1;
            }
            buffer = new_buf;
            buf_size = new_size;
        }
        int rlen = esp_http_client_read(client, buffer + total_read,
                                         buf_size - total_read - 1);
        if (rlen < 0) {
            ESP_LOGE(TAG, "Failed to read response");
            free(buffer);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return -1;
        }
        if (rlen == 0) break;
        total_read += rlen;
    }

    buffer[total_read] = '\0';
    *out = buffer;
    if (out_len) {
        *out_len = total_read;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return 0;
}

int platform_http_get(const char *url, char **out, size_t *out_len) {
    return http_perform(url, NULL, NULL, out, out_len);
}

int platform_http_post_json(const char *url, const char *json, char **out, size_t *out_len) {
    return http_perform(url, json, "application/json", out, out_len);
}

void platform_http_free(char *p) {
    free(p);
}

static size_t decompress_gzip(char **data, size_t compressed_size) {
    if (!data || !*data || compressed_size < 18) return 0;

    uint8_t *gzip_data = (uint8_t *)*data;

    if (gzip_data[0] != 0x1f || gzip_data[1] != 0x8b || gzip_data[2] != 0x08) {
        ESP_LOGE(TAG, "Invalid gzip header");
        return 0;
    }

    uint8_t flags = gzip_data[3];
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

    if (uncompressed_size > 2 * 1024 * 1024) {  // 2 MB sanity cap
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
    esp_http_client_config_t config = {.url = url, .method = HTTP_METHOD_GET, .timeout_ms = 5000};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -1;

    esp_http_client_set_header(client, "Accept-Encoding", "gzip");
    char knob_id[16];
    get_knob_id(knob_id, sizeof(knob_id));
    esp_http_client_set_header(client, "X-Knob-Id", knob_id);
    esp_http_client_set_header(client, "X-Knob-Version", get_knob_version());

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return -1;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP image request failed: status=%d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -1;
    }

    char *content_encoding = NULL;
    esp_http_client_get_header(client, "Content-Encoding", &content_encoding);
    bool is_gzipped = (content_encoding && strcmp(content_encoding, "gzip") == 0);

    size_t buffer_size = (content_length > 0) ? content_length : 65536;
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -1;
    }

    int total_read = 0;
    int read_attempts = 0;

    while (read_attempts < 1000) {
        if (total_read + 4096 > (int)buffer_size) {
            buffer_size *= 2;
            if (buffer_size > 1024 * 1024) {
                free(buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return -1;
            }
            char *new_buffer = realloc(buffer, buffer_size);
            if (!new_buffer) {
                free(buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return -1;
            }
            buffer = new_buffer;
        }

        int read_len = esp_http_client_read(client, buffer + total_read, 4096);
        read_attempts++;
        if (read_len < 0) {
            ESP_LOGE(TAG, "Image read failed");
            free(buffer);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return -1;
        }
        if (read_len == 0) break;
        total_read += read_len;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total_read <= 0) {
        free(buffer);
        return -1;
    }

    bool looks_like_gzip = (total_read > 2 && (uint8_t)buffer[0] == 0x1f && (uint8_t)buffer[1] == 0x8b);
    if (!is_gzipped && looks_like_gzip) {
        is_gzipped = true;
    }

    size_t final_size = total_read;
    if (is_gzipped) {
        final_size = decompress_gzip(&buffer, total_read);
        if (final_size == 0) {
            free(buffer);
            return -1;
        }
    }

    *out = buffer;
    if (out_len) *out_len = final_size;
    return 0;
}
