#ifndef PORTAL_TRACE_FAKE_IDF_H
#define PORTAL_TRACE_FAKE_IDF_H

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

typedef int esp_err_t;
#define ESP_OK 0
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void trace_test_enter(portMUX_TYPE *lock);
void trace_test_exit(portMUX_TYPE *lock);
#define portENTER_CRITICAL(lock) trace_test_enter(lock)
#define portEXIT_CRITICAL(lock) trace_test_exit(lock)
int64_t esp_timer_get_time(void);
void trace_test_log(const char *tag, const char *format, ...);
#define ESP_LOGI(...) trace_test_log(__VA_ARGS__)
struct httpd_req { const char *uri; };
esp_err_t httpd_req_get_hdr_value_str(struct httpd_req *req, const char *name,
                                     char *value, size_t size);

#endif
