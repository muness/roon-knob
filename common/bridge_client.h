#pragma once

#include "controller_command.h"
#include "controller_connection.h"
#include "platform/platform_http.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRIDGE_CLIENT_MAX_ZONES 64

/* Reads the already-published controller configuration snapshot. */
void bridge_client_start(void);
/* Wake the polling worker after a display sleep/power-save interval. */
void bridge_client_request_poll(void);
bool bridge_client_execute_command(const controller_command_t *command);
void bridge_client_set_network_ready(bool ready);
const char* bridge_client_get_artwork_url(char *url_buf, size_t buf_len, int width, int height);
const char* bridge_client_get_artwork_url_for_format(char *url_buf, size_t buf_len,
                                                     int width, int height,
                                                     int clip_radius,
                                                     const char *format);
const char* bridge_client_get_artwork_url_for_format_and_scale(
    char *url_buf, size_t buf_len, int width, int height, int clip_radius,
    const char *format, const char *scale, int crop_limit_percent);
int bridge_client_fetch_artwork(const char *image_key, int width, int height, const char *format,
                                char **out, size_t *out_len);
int bridge_client_stream_artwork(const char *image_key, int width, int height,
                                 const char *format, size_t max_bytes,
                                 platform_http_stream_callback_t callback,
                                 void *ctx, size_t *out_len);
bool bridge_client_is_ready_for_art_mode(void);

bool bridge_client_get_bridge_url(char *buf, size_t len);  // Get configured bridge URL
bool bridge_client_is_bridge_mdns(void);           // True if bridge was discovered via mDNS (persisted)

// Target-neutral zone access used by configuration surfaces that do not render
// the controller's on-device zone picker (for example the Frame web UI).
typedef struct {
    char id[64];
    char name[64];
} bridge_zone_t;

typedef void (*bridge_zone_list_visitor_t)(const bridge_zone_t *zones,
                                           int zone_count,
                                           const char *current_zone_id,
                                           void *ctx);

typedef struct {
    bool found;
    bool persisted;
    bool became_ready;
    char zone_name[64];
} bridge_zone_selection_result_t;

int bridge_client_get_zones(bridge_zone_t *out, int max);
bool bridge_client_get_current_zone_id(char *out, size_t len);
/*
 * Visits bridge-owned zone storage synchronously while the bridge state is
 * locked. The visitor must not retain pointers or call bridge_client APIs.
 * This is a transitional #194 compatibility constraint, not a general
 * presentation callback contract.
 */
bool bridge_client_visit_zones(bridge_zone_list_visitor_t visitor, void *ctx);
bridge_zone_selection_result_t bridge_client_select_zone_value(
    const char *zone_id);
bool bridge_client_set_zone(const char *zone_id);

void bridge_client_connection_snapshot(controller_connection_t *out);

void bridge_client_connection_status(char *summary, size_t summary_len, char *details, size_t details_len);

bool bridge_client_get_request_base(char *out, size_t len);
#ifdef __cplusplus
}
#endif
