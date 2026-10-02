#include "portal_trace.h"

#include <esp_http_server.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <lwip/sockets.h>

static const char *TAG = "portal_trace";

#define PORTAL_TRACE_MAX 64
#define PORTAL_TRACE_TEXT 43

typedef struct {
    uint32_t ms;
    char kind;
    char text[PORTAL_TRACE_TEXT];
} trace_entry_t;

static trace_entry_t s_trace[PORTAL_TRACE_MAX];
static int s_trace_n;
static int64_t s_trace_t0;
static portMUX_TYPE s_trace_lock = portMUX_INITIALIZER_UNLOCKED;

static const char *kind_name(char kind) {
    switch (kind) {
    case 'J': return "join";
    case 'L': return "leave";
    case 'I': return "dhcp";
    case 'D': return "dns";
    case 'H': return "http";
    case 'O': return "open";
    case 'C': return "close";
    default: return "?";
    }
}

void portal_trace(char kind, const char *fmt, ...) {
    trace_entry_t e = {.kind = kind};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e.text, sizeof(e.text), fmt, ap);
    va_end(ap);

    /* Format outside the lock; the critical section is a struct copy. */
    portENTER_CRITICAL(&s_trace_lock);
    if (s_trace_n < PORTAL_TRACE_MAX) {
        e.ms = (uint32_t)((esp_timer_get_time() - s_trace_t0) / 1000);
        s_trace[s_trace_n++] = e;
    }
    portEXIT_CRITICAL(&s_trace_lock);
}

void portal_trace_reset(void) {
    portENTER_CRITICAL(&s_trace_lock);
    s_trace_n = 0;
    s_trace_t0 = esp_timer_get_time();
    portEXIT_CRITICAL(&s_trace_lock);
}

void portal_trace_dump(const char *why) {
    trace_entry_t snapshot[PORTAL_TRACE_MAX];
    int n;
    portENTER_CRITICAL(&s_trace_lock);
    n = s_trace_n;
    memcpy(snapshot, s_trace, sizeof(trace_entry_t) * (size_t)n);
    portEXIT_CRITICAL(&s_trace_lock);

    ESP_LOGI(TAG, "setup timeline (%s): %d events%s", why, n,
             n == PORTAL_TRACE_MAX ? " (full)" : "");
    for (int i = 0; i < n; i++) {
        ESP_LOGI(TAG, "%7lu ms %-5s %s", (unsigned long)snapshot[i].ms,
                 kind_name(snapshot[i].kind), snapshot[i].text);
    }
}

void portal_trace_req(struct httpd_req *req) {
    char host[24] = "";
    (void)httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
    portal_trace('H', "%.20s%.22s", host, req->uri);
}

esp_err_t portal_trace_open(void *hd, int sockfd) {
    (void)hd;
    portal_trace('O', "fd %d", sockfd);
    return ESP_OK;
}

void portal_trace_close(void *hd, int sockfd) {
    (void)hd;
    portal_trace('C', "fd %d", sockfd);
    /* Setting close_fn replaces httpd's own close(), so we must do it. */
    close(sockfd);
}
