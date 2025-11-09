#include "http_client.h"

#include <esp_http_client.h>
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "net_client";

struct http_buffer {
    char *data;
    size_t len;
};

static esp_err_t _event_handler(esp_http_client_event_handle_t evt) {
    struct http_buffer *buf = evt->data;
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!buf) {
                break;
            }
            buf->data = realloc(buf->data, buf->len + evt->data_len + 1);
            if (!buf->data) {
                return ESP_ERR_NO_MEM;
            }
            memcpy(buf->data + buf->len, evt->data, evt->data_len);
            buf->len += evt->data_len;
            buf->data[buf->len] = '\0';
            break;
        default:
            break;
    }
    return ESP_OK;
}

static int _http_request(const char *url, const char *body, const char *content_type, char **out, size_t *out_len) {
    esp_http_client_config_t config = {
        .url = url,
        .method = body ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .event_handler = _event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "failed to init http client");
        return -1;
    }

    struct http_buffer buf = {0};
    esp_http_client_set_user_data(client, &buf);
    esp_http_client_set_header(client, "Accept", "application/json");
    if (body) {
        esp_http_client_set_header(client, "Content-Type", content_type ?: "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    int ret = 0;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "http failed: %s", esp_err_to_name(err));
        ret = -1;
    } else {
        *out = buf.data;
        if (out_len) {
            *out_len = buf.len;
        }
    }
    if (ret != 0) {
        free(buf.data);
    }
    esp_http_client_cleanup(client);
    return ret;
}

int http_get(const char *url, char **out, size_t *out_len) {
    return _http_request(url, NULL, NULL, out, out_len);
}

int http_post_json(const char *url, const char *json, char **out, size_t *out_len) {
    return _http_request(url, json, "application/json", out, out_len);
}

void http_free(char *p) {
    free(p);
}
