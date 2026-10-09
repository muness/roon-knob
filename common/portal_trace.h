// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <esp_err.h>

/* httpd_req_t and httpd_handle_t are spelled out here (they are
 * `struct httpd_req` and `void *` in esp_http_server.h) so this header can be
 * included by code that is built without ESP-IDF's HTTP server. */
struct httpd_req;

#ifdef __cplusplus
extern "C" {
#endif

/* Setup-mode timeline for diagnosing slow captive-portal pop-ups.
 *
 * Records the first PORTAL_TRACE_MAX events after each station joins the
 * setup AP (join, DHCP lease, DNS queries, HTTP requests, socket open/close)
 * in ms since that join, and logs the whole sequence when the station leaves.
 * Events are stored in RAM rather than logged as they happen so that the
 * logging itself does not add latency to the paths being timed. */

/* Append an event.  kind: J join, L leave, I DHCP lease, D DNS, H HTTP,
 * O socket open, C socket close. */
void portal_trace(char kind, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Start a new timeline (called on each station join). */
void portal_trace_reset(void);

/* Log the recorded timeline with bounded stack use; stop if a new join resets it. */
void portal_trace_dump(const char *why);

/* Record "<Host><path>"; query values and fragments are never recorded. */
void portal_trace_req(struct httpd_req *req);

/* httpd open_fn/close_fn hooks.  close_fn owns closing the socket. */
esp_err_t portal_trace_open(void *hd, int sockfd);
void portal_trace_close(void *hd, int sockfd);

#ifdef __cplusplus
}
#endif
