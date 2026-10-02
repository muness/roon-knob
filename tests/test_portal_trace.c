#include "portal_trace.h"
#include "fake_idf.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int64_t s_time;
static int s_locked;
static char s_log[16384];
static size_t s_log_len;
static bool s_reset_on_log;

void trace_test_enter(portMUX_TYPE *lock) {
    (void)lock;
    assert(!s_locked);
    s_locked = 1;
}

void trace_test_exit(portMUX_TYPE *lock) {
    (void)lock;
    assert(s_locked);
    s_locked = 0;
}

int64_t esp_timer_get_time(void) { return s_time; }

void trace_test_log(const char *tag, const char *format, ...) {
    (void)tag;
    assert(!s_locked); /* Serial logging must never run under a spinlock. */
    va_list args;
    va_start(args, format);
    int n = vsnprintf(s_log + s_log_len, sizeof(s_log) - s_log_len, format, args);
    va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(s_log) - s_log_len - 1);
    s_log_len += (size_t)n;
    s_log[s_log_len++] = '\n';
    s_log[s_log_len] = '\0';
    if (s_reset_on_log) {
        s_reset_on_log = false;
        portal_trace_reset();
        portal_trace('J', "new-session");
    }
}

esp_err_t httpd_req_get_hdr_value_str(struct httpd_req *req, const char *name,
                                     char *value, size_t size) {
    (void)req;
    assert(strcmp(name, "Host") == 0);
    snprintf(value, size, "192.168.4.1");
    return ESP_OK;
}

static void clear_log(void) {
    s_log_len = 0;
    s_log[0] = '\0';
}

int main(void) {
    portal_trace_reset();
    s_time = 12000;
    portal_trace('J', "phone");
    struct httpd_req req = {.uri = "/configure?password=private-fixture&ssid=home"};
    portal_trace_req(&req);
    req.uri = "/?token=short-secret";
    portal_trace_req(&req);
    req.uri = "/settings#fragment-secret";
    portal_trace_req(&req);
    portal_trace_dump("redaction");
    assert(strstr(s_log, "12 ms join"));
    assert(strstr(s_log, "192.168.4.1/configure"));
    assert(strstr(s_log, "192.168.4.1/settings"));
    assert(!strstr(s_log, "password"));
    assert(!strstr(s_log, "private-fixture"));
    assert(!strstr(s_log, "short-secret"));
    assert(!strstr(s_log, "fragment-secret"));

    clear_log();
    portal_trace_reset();
    for (int i = 0; i < 70; i++) portal_trace('D', "candidate-%02d", i);
    portal_trace_dump("capacity");
    assert(strstr(s_log, "64 events (full)"));
    assert(strstr(s_log, "candidate-63"));
    assert(!strstr(s_log, "candidate-64"));

    clear_log();
    s_reset_on_log = true; /* A concurrent station join after dump starts. */
    portal_trace_dump("interrupted");
    assert(!strstr(s_log, "candidate-"));
    assert(!strstr(s_log, "new-session"));
    clear_log();
    portal_trace_dump("next session");
    assert(strstr(s_log, "1 events"));
    assert(strstr(s_log, "new-session"));

    int descriptors[2];
    assert(pipe(descriptors) == 0);
    assert(portal_trace_open(NULL, descriptors[0]) == ESP_OK);
    portal_trace_close(NULL, descriptors[0]);
    errno = 0;
    assert(fcntl(descriptors[0], F_GETFD) == -1 && errno == EBADF);
    close(descriptors[1]);
    puts("portal trace redaction, bounded capacity, session reset, and owned close pass");
    return 0;
}
