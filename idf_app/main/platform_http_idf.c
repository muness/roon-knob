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

// Get unique device ID based on MAC address
static void get_knob_id(char *out, size_t len) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void platform_http_get_knob_id(char *out, size_t len) {
    get_knob_id(out, len);
}

// Get firmware version
static const char* get_knob_version(void) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    return app_desc->version;
}

static int http_perform(const char *url, const char *body, const char *content_type, char **out, size_t *out_len) {
    ESP_LOGD(TAG, "HTTP %s: %s", body ? "POST" : "GET", url);

    // Use native request pattern (more reliable than perform() with event handler)
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

    // Set headers
    esp_http_client_set_header(client, "Accept", "application/json");
    if (body) {
        esp_http_client_set_header(client, "Content-Type", content_type ? content_type : "application/json");
    }

    // Set knob identification headers
    char knob_id[16];
    get_knob_id(knob_id, sizeof(knob_id));
    esp_http_client_set_header(client, "X-Knob-Id", knob_id);
    esp_http_client_set_header(client, "X-Knob-Version", get_knob_version());

    // Open connection
    esp_err_t err = esp_http_client_open(client, body ? strlen(body) : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }

    // Write POST body if present
    if (body) {
        int wlen = esp_http_client_write(client, body, strlen(body));
        if (wlen < 0) {
            ESP_LOGE(TAG, "Write failed");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return -1;
        }
    }

    // Fetch headers to get content length
    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        ESP_LOGE(TAG, "HTTP fetch headers failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -1;
    }

    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGD(TAG, "HTTP Status=%d, content_length=%d", status_code, content_length);

    // Allocate buffer for response
    char *buffer = calloc(1, content_length + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -1;
    }

    // Read response
    int data_read = esp_http_client_read_response(client, buffer, content_length);
    if (data_read < 0) {
        ESP_LOGE(TAG, "Failed to read response");
        free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -1;
    }

    buffer[data_read] = '\0';
    *out = buffer;
    if (out_len) {
        *out_len = data_read;
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

    // Validate gzip magic bytes and compression method
    if (gzip_data[0] != 0x1f || gzip_data[1] != 0x8b || gzip_data[2] != 0x08) {
        ESP_LOGE(TAG, "Invalid gzip header (magic bytes or compression method)");
        return 0;
    }

    // Parse gzip header to find actual header size (RFC 1952)
    uint8_t flags = gzip_data[3];
    size_t header_size = 10;  // Minimum header size

    // Handle optional fields based on FLG byte
    if (flags & 0x04) {  // FEXTRA present
        if (header_size + 2 > compressed_size) return 0;
        uint16_t xlen = gzip_data[header_size] | (gzip_data[header_size + 1] << 8);
        header_size += 2 + xlen;
    }
    if (flags & 0x08) {  // FNAME present (null-terminated string)
        while (header_size < compressed_size && gzip_data[header_size++] != 0);
    }
    if (flags & 0x10) {  // FCOMMENT present (null-terminated string)
        while (header_size < compressed_size && gzip_data[header_size++] != 0);
    }
    if (flags & 0x02) {  // FHCRC present
        header_size += 2;
    }

    // Validate header size
    if (header_size + 8 > compressed_size) {
        ESP_LOGE(TAG, "Gzip header size (%zu) exceeds compressed size (%zu)", header_size, compressed_size);
        return 0;
    }

    // Extract uncompressed size from trailer (ISIZE field, last 4 bytes)
    uint32_t uncompressed_size = gzip_data[compressed_size - 4] |
                                  (gzip_data[compressed_size - 3] << 8) |
                                  (gzip_data[compressed_size - 2] << 16) |
                                  (gzip_data[compressed_size - 1] << 24);

    // Allocate decompression buffer
    char *decompressed = calloc(1, uncompressed_size);
    if (!decompressed) {
        ESP_LOGE(TAG, "Failed to allocate decompression buffer (%" PRIu32 " bytes)", uncompressed_size);
        return 0;
    }

    // Decompress deflate data (between header and 8-byte trailer)
    const uint8_t *deflate_data = gzip_data + header_size;
    size_t deflate_len = compressed_size - header_size - 8;

    size_t actual_size = tinfl_decompress_mem_to_mem(decompressed, uncompressed_size,
                                                       deflate_data, deflate_len, 0);

    // Check for decompression failure (returns (size_t)(-1) on error)
    if (actual_size == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        ESP_LOGE(TAG, "Gzip decompression failed (tinfl error)");
        free(decompressed);
        return 0;
    }

    // Validate decompressed size matches expected
    if (actual_size != uncompressed_size) {
        ESP_LOGW(TAG, "Gzip size mismatch: expected %" PRIu32 ", got %zu", uncompressed_size, actual_size);
        free(decompressed);
        return 0;
    }

    // Validate CRC32 from trailer (bytes -8 to -5)
    uint32_t expected_crc = gzip_data[compressed_size - 8] |
                            (gzip_data[compressed_size - 7] << 8) |
                            (gzip_data[compressed_size - 6] << 16) |
                            (gzip_data[compressed_size - 5] << 24);

    uint32_t actual_crc = mz_crc32(MZ_CRC32_INIT, (const uint8_t*)decompressed, uncompressed_size);

    if (actual_crc != expected_crc) {
        ESP_LOGE(TAG, "Gzip CRC32 mismatch: expected 0x%08" PRIx32 ", got 0x%08" PRIx32, expected_crc, actual_crc);
        free(decompressed);
        return 0;
    }

    ESP_LOGI(TAG, "Gzip decompressed %zu → %" PRIu32 " bytes (CRC32 valid)", compressed_size, uncompressed_size);
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
        ESP_LOGE(TAG, "HTTP request failed: status=%d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -1;
    }

    char *content_encoding = NULL;
    esp_http_client_get_header(client, "Content-Encoding", &content_encoding);
    bool is_gzipped = (content_encoding && strcmp(content_encoding, "gzip") == 0);

    // For chunked responses or unknown length, read in chunks
    // Allocate initial buffer, will realloc as needed
    size_t buffer_size = (content_length > 0) ? content_length : 65536;
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate initial buffer");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -1;
    }

    int total_read = 0;
    int read_attempts = 0;
    const int max_attempts = 1000;  // Prevent infinite loop

    while (read_attempts < max_attempts) {
        // Ensure we have space
        if (total_read + 4096 > buffer_size) {
            buffer_size *= 2;
            if (buffer_size > 1024 * 1024) {  // Safety: max 1MB
                ESP_LOGE(TAG, "Response too large (>1MB)");
                free(buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return -1;
            }
            char *new_buffer = realloc(buffer, buffer_size);
            if (!new_buffer) {
                ESP_LOGE(TAG, "Failed to realloc buffer to %zu", buffer_size);
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
            ESP_LOGE(TAG, "Failed to read chunk (attempt %d)", read_attempts);
            free(buffer);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return -1;
        }
        if (read_len == 0) {
            break;  // End of response
        }
        total_read += read_len;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total_read <= 0) {
        ESP_LOGE(TAG, "No data read from response");
        free(buffer);
        return -1;
    }

    // Check if data is gzipped by magic bytes (fallback if header not detected)
    bool looks_like_gzip = (total_read > 2 && (uint8_t)buffer[0] == 0x1f && (uint8_t)buffer[1] == 0x8b);
    if (!is_gzipped && looks_like_gzip) {
        is_gzipped = true;
    }

    size_t final_size = total_read;
    if (is_gzipped) {
        final_size = decompress_gzip(&buffer, total_read);
        if (final_size == 0) {
            ESP_LOGE(TAG, "Gzip decompression failed");
            free(buffer);
            return -1;
        }
    }

    *out = buffer;
    if (out_len) *out_len = final_size;
    return 0;
}
