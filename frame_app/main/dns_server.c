#include "dns_server.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <esp_log.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static const char *TAG = "dns_server";

#define DNS_PORT 53
#define DNS_MAX_LEN 512

// AP IP address (192.168.4.1 in network byte order)
#define AP_IP_ADDR htonl(0xC0A80401)

static int s_sock = -1;
static TaskHandle_t s_task = NULL;
static atomic_bool s_running = ATOMIC_VAR_INIT(false);
/* Do not reuse the global socket/task state until the stopped task has
 * acknowledged that it no longer references the prior lifecycle. */
static bool s_stopping = false;
static SemaphoreHandle_t s_stop_sem = NULL;

// Minimal DNS response - redirect everything to AP IP
static int build_dns_response(const uint8_t *query, int query_len, uint8_t *response) {
    if (query_len < 12) {
        return -1;  // Too short for DNS header
    }

    // Copy query to response
    memcpy(response, query, query_len);

    // Set response flags: QR=1 (response), AA=1 (authoritative), RCODE=0 (no error)
    response[2] = 0x84;  // QR=1, Opcode=0, AA=1, TC=0, RD=0
    response[3] = 0x00;  // RA=0, Z=0, RCODE=0

    // Set answer count to 1
    response[6] = 0x00;
    response[7] = 0x01;

    // Find end of question section (skip QNAME + QTYPE + QCLASS)
    int pos = 12;
    while (pos < query_len && query[pos] != 0) {
        pos += query[pos] + 1;  // Skip label
    }
    pos += 5;  // Skip null terminator + QTYPE (2) + QCLASS (2)

    if (pos > query_len) {
        return -1;  // Malformed query
    }

    // Add answer section
    int ans_start = pos;

    // Ensure answer record (16 bytes) fits within response buffer
    if (ans_start + 16 > DNS_MAX_LEN) {
        return -1;
    }

    // Name pointer to question
    response[ans_start] = 0xC0;
    response[ans_start + 1] = 0x0C;

    // Type A (1)
    response[ans_start + 2] = 0x00;
    response[ans_start + 3] = 0x01;

    // Class IN (1)
    response[ans_start + 4] = 0x00;
    response[ans_start + 5] = 0x01;

    // TTL (60 seconds)
    response[ans_start + 6] = 0x00;
    response[ans_start + 7] = 0x00;
    response[ans_start + 8] = 0x00;
    response[ans_start + 9] = 0x3C;

    // RDLENGTH (4 bytes for IPv4)
    response[ans_start + 10] = 0x00;
    response[ans_start + 11] = 0x04;

    // RDATA (192.168.4.1)
    response[ans_start + 12] = 192;
    response[ans_start + 13] = 168;
    response[ans_start + 14] = 4;
    response[ans_start + 15] = 1;

    return ans_start + 16;
}

static void dns_server_task(void *arg) {
    const int sock = (int)(intptr_t)arg;
    uint8_t rx_buf[DNS_MAX_LEN];
    uint8_t tx_buf[DNS_MAX_LEN];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    ESP_LOGI(TAG, "DNS server task started");

    while (atomic_load_explicit(&s_running, memory_order_acquire)) {
        int len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0,
                          (struct sockaddr *)&client_addr, &addr_len);
        if (len < 0) {
            if (atomic_load_explicit(&s_running, memory_order_acquire)) {
                ESP_LOGW(TAG, "recvfrom failed: %d", errno);
            }
            continue;
        }

        // Log the queried domain name for debugging
        if (len > 12) {
            char domain[128] = {0};
            int dpos = 0, qpos = 12;
            while (qpos < len && rx_buf[qpos] != 0 && dpos < 126) {
                int label_len = rx_buf[qpos++];
                for (int i = 0; i < label_len && qpos < len && dpos < 127; i++) {
                    domain[dpos++] = rx_buf[qpos++];
                }
                if (qpos < len && rx_buf[qpos] != 0) domain[dpos++] = '.';
            }
            ESP_LOGI(TAG, "DNS query: %s -> 192.168.4.1", domain);
        }

        int resp_len = build_dns_response(rx_buf, len, tx_buf);
        if (resp_len > 0) {
            sendto(sock, tx_buf, resp_len, 0,
                   (struct sockaddr *)&client_addr, addr_len);
        }
    }

    ESP_LOGI(TAG, "DNS server task stopped");
    if (s_stop_sem) xSemaphoreGive(s_stop_sem);
    vTaskDelete(NULL);
}

/* A timed-out stop remains isolated.  A later AP attempt may reap the task,
 * but cannot bind a replacement DNS socket until acknowledgement arrives. */
static bool reap_stopped_task(void) {
    if (!s_stopping) {
        return true;
    }
    if (!s_stop_sem || xSemaphoreTake(s_stop_sem, 0) != pdTRUE) {
        return false;
    }
    s_task = NULL;
    s_stopping = false;
    return true;
}

bool dns_server_start(void) {
    if (atomic_load_explicit(&s_running, memory_order_acquire)) {
        return s_sock >= 0 && s_task != NULL;
    }
    if (!reap_stopped_task()) {
        ESP_LOGW(TAG, "Previous DNS task is still stopping");
        return false;
    }

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", errno);
        return false;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(s_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: %d", errno);
        close(s_sock);
        s_sock = -1;
        return false;
    }

    if (!s_stop_sem) {
        s_stop_sem = xSemaphoreCreateBinary();
    }
    if (!s_stop_sem) {
        ESP_LOGE(TAG, "Failed to create DNS stop semaphore");
        close(s_sock);
        s_sock = -1;
        return false;
    }
    atomic_store_explicit(&s_running, true, memory_order_release);
    if (xTaskCreate(dns_server_task, "dns_server", 4096,
                    (void *)(intptr_t)s_sock, 5, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start DNS server task");
        atomic_store_explicit(&s_running, false, memory_order_release);
        close(s_sock);
        s_sock = -1;
        s_task = NULL;
        return false;
    }
    ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);
    return true;
}

void dns_server_stop(void) {
    if (!atomic_load_explicit(&s_running, memory_order_acquire) || s_stopping) {
        return;
    }

    atomic_store_explicit(&s_running, false, memory_order_release);
    s_stopping = true;

    if (s_sock >= 0) {
        shutdown(s_sock, SHUT_RDWR);
        close(s_sock);
        s_sock = -1;
    }

    if (!reap_stopped_task() && s_stop_sem &&
        xSemaphoreTake(s_stop_sem, pdMS_TO_TICKS(500)) == pdTRUE) {
        s_task = NULL;
        s_stopping = false;
    }

    ESP_LOGI(TAG, "%s", s_stopping ? "DNS server stop pending"
                                    : "DNS server stopped");
}

bool dns_server_is_running(void) {
    return atomic_load_explicit(&s_running, memory_order_acquire) &&
           s_sock >= 0 && s_task != NULL;
}
