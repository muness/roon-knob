# ESP-IDF HTTP Client Reference

Key patterns from working ESP-IDF HTTP client example.

## Native Request Pattern (Most Reliable)

The "native request" approach using `open()` → `fetch_headers()` → `read()` → `close()` is more reliable than `perform()` with event handlers.

```c
esp_http_client_config_t config = {
    .url = "http://httpbin.org/get",
};
esp_http_client_handle_t client = esp_http_client_init(&config);

// Open connection
esp_err_t err = esp_http_client_open(client, 0);
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open: %s", esp_err_to_name(err));
    return;
}

// Fetch headers to get content length
int content_length = esp_http_client_fetch_headers(client);
if (content_length < 0) {
    ESP_LOGE(TAG, "HTTP client fetch headers failed");
}

// Read response
char *buffer = malloc(content_length + 1);
int data_read = esp_http_client_read_response(client, buffer, content_length);
if (data_read >= 0) {
    buffer[data_read] = '\0';
    ESP_LOGI(TAG, "Status=%d, len=%d",
             esp_http_client_get_status_code(client),
             content_length);
}

esp_http_client_close(client);
esp_http_client_cleanup(client);
```

## Config with Host/Path (Alternative)

```c
esp_http_client_config_t config = {
    .host = "httpbin.org",
    .path = "/get",
    .transport_type = HTTP_TRANSPORT_OVER_TCP,  // Explicit plain HTTP
};
```

## Required Kconfig Options

```
CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS=y
CONFIG_ESP_HTTP_CLIENT_ENABLE_BASIC_AUTH=y
```

**Note**: `CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS=y` is required even for plain HTTP because the HTTP client uses the same transport layer infrastructure.
