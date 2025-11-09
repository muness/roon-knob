#include "platform/platform_http.h"

#include <esp_http_client.h>
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "platform_http";

static int http_perform(const char *url, const char *body, const char *content_type, char **out, size_t *out_len) {
    ESP_LOGI(TAG, "HTTP %s: %s", body ? "POST" : "GET", url);

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
    ESP_LOGI(TAG, "HTTP Status=%d, content_length=%d", status_code, content_length);

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
