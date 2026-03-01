#include "bridge_client.h"

#include "os_mutex.h"
#include "platform/platform_display.h"
#include "platform/platform_http.h"
#include "platform/platform_log.h"
#include "platform/platform_mdns.h"
#include "platform/platform_storage.h"
#include "platform/platform_task.h"
#include "platform/platform_time.h"
#include "ui.h"

#ifdef ESP_PLATFORM
#include "display_sleep.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#endif

#include "manifest_parse.h"
#include "manifest_ui.h"

#include <cJSON.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for config handling
static bool fetch_knob_config(void) __attribute__((unused));
static void apply_knob_config(const rk_cfg_t *cfg);
static void check_charging_state_change(void);

#define MAX_LINE 128
#define MAX_ZONE_NAME 64
#define MAX_ZONES 64
#define POLL_DELAY_AWAKE_CHARGING_MS                                           \
  2000 // 2 seconds when charging and display on
#define POLL_DELAY_AWAKE_BATTERY_MS 5000 // 5 seconds on battery to save power
#define POLL_DELAY_SLEEPING_MS 30000     // 30 seconds when display is sleeping
#define POLL_DELAY_SLEEPING_STOPPED_MS                                         \
  60000 // 60 seconds when sleeping AND zone stopped
#define POLL_DELAY_BRIDGE_ERROR_MS 10000 // 10 seconds when bridge unreachable

// Special zone picker options (not actual zones)
#define ZONE_ID_BACK "__back__"
#define ZONE_ID_SETTINGS "__settings__"


struct zone_entry {
  char id[MAX_ZONE_NAME];
  char name[MAX_ZONE_NAME];
};

// Device operational state for safe volume control
typedef enum {
  DEVICE_STATE_BOOT,        // Hardware ready, no network
  DEVICE_STATE_CONNECTING,  // WiFi attempting
  DEVICE_STATE_CONNECTED,   // Network ready, zones unknown
  DEVICE_STATE_OPERATIONAL, // Zones loaded, fully ready
  DEVICE_STATE_RECONNECTING // Was operational, lost connection
} device_state_t;

static const char *device_state_name(device_state_t state) {
  switch (state) {
  case DEVICE_STATE_BOOT:
    return "BOOT";
  case DEVICE_STATE_CONNECTING:
    return "CONNECTING";
  case DEVICE_STATE_CONNECTED:
    return "CONNECTED";
  case DEVICE_STATE_OPERATIONAL:
    return "OPERATIONAL";
  case DEVICE_STATE_RECONNECTING:
    return "RECONNECTING";
  default:
    return "UNKNOWN";
  }
}

struct bridge_state {
  rk_cfg_t cfg;
  struct zone_entry zones[MAX_ZONES];
  int zone_count;
  char zone_label[MAX_ZONE_NAME];
  bool zone_resolved;
  bool net_connected;
};

static struct bridge_state s_state;
static os_mutex_t s_state_lock = OS_MUTEX_INITIALIZER;
static bool s_running;
static bool s_trigger_poll;
static bool s_last_net_ok;
static bool s_network_ready;
static device_state_t s_device_state = DEVICE_STATE_BOOT; // Initial state
static bool s_force_artwork_refresh; // Force artwork reload on zone change
static float s_last_known_volume =
    0.0f; // Cached volume for optimistic UI updates
static float s_last_known_volume_min = -80.0f; // Cached volume min for clamping
static float s_last_known_volume_max = 0.0f;   // Cached volume max for clamping
static float s_last_known_volume_step = 1.0f;  // Cached volume step
static interactions_t s_cached_interactions;    // Cached input-to-action mappings
static bool s_has_interactions = false;         // True when manifest has interactions

// Cached per-screen encoders (indexed parallel to manifest screens)
#define MAX_CACHED_SCREENS MANIFEST_MAX_SCREENS
typedef struct {
    char screen_id[MANIFEST_MAX_ID];
    bool has_encoder;
    manifest_encoder_t encoder;
} cached_screen_encoder_t;
static cached_screen_encoder_t s_cached_encoders[MAX_CACHED_SCREENS];
static int s_cached_encoder_count = 0;

// Cached per-screen elements for v2 button tap dispatch
#define MAX_CACHED_ELEMENTS MAX_ELEMENTS
typedef struct {
    char screen_id[MANIFEST_MAX_ID];
    manifest_element_t elements[MAX_CACHED_ELEMENTS];
    int element_count;
} cached_screen_elements_t;
static cached_screen_elements_t s_cached_elements[MAX_CACHED_SCREENS];
static int s_cached_element_count = 0;
static bool s_bridge_verified =
    false; // True after bridge found AND responded successfully
static uint32_t s_last_mdns_check_ms = 0; // Timestamp of last mDNS check
static bool s_last_charging_state =
    true; // Track charging state for config reapply
static bool s_last_is_playing =
    false; // Track play state for extended sleep polling
#define MDNS_RECHECK_INTERVAL_MS                                               \
  (3600 * 1000) // Re-check mDNS every hour if bridge stops responding

// Bridge connection retry tracking (mirrors WiFi retry pattern)
#define BRIDGE_FAIL_THRESHOLD                                                  \
  5 // Show recovery info after this many consecutive failures
#define MDNS_FAIL_THRESHOLD                                                    \
  10 // Show recovery info after this many mDNS failures (~30s)
static int s_bridge_fail_count = 0;
static int s_mdns_fail_count = 0;
static char s_device_ip[16] = {0}; // Device IP for recovery messages

static void lock_state(void) { os_mutex_lock(&s_state_lock); }

static void unlock_state(void) { os_mutex_unlock(&s_state_lock); }

static bool refresh_zone_label(bool prefer_zone_id);
static void parse_zones_from_response(const char *resp);
static const char *extract_json_string(const char *start, const char *key,
                                       char *out, size_t len);
static bool send_control_json(const char *json);
static void wait_for_poll_interval(void);
static void bridge_poll_thread(void *arg);
static bool host_is_valid(const char *url);
static void maybe_update_bridge_base(void);
static void strip_trailing_slashes(char *url);
static void reset_bridge_fail_count(void);
static void increment_bridge_fail_count(void);


static bool host_is_valid(const char *url) {
  // Accept any URL with a non-empty hostname (IP or mDNS name like
  // rooExtend.localdomain)
  if (!url || !url[0])
    return false;
  const char *host = url;
  const char *scheme = strstr(url, "://");
  if (scheme)
    host = scheme + 3;
  const char *end = host;
  while (*end && *end != ':' && *end != '/')
    ++end;
  return (end > host);
}


static void ui_battery_cb(void *arg) {
  (void)arg;
  ui_update_battery();
}

static void post_ui_battery_update(void) {
  platform_task_post_to_ui(ui_battery_cb, NULL);
}




static void wait_for_poll_interval(void) {
  // Use longer delay when display is sleeping, on battery, or bridge
  // unreachable
  uint32_t delay_ms;
  if (s_bridge_fail_count >= BRIDGE_FAIL_THRESHOLD) {
    delay_ms = POLL_DELAY_BRIDGE_ERROR_MS; // Slow down when bridge unreachable
  } else if (platform_display_is_sleeping()) {
    // When sleeping AND zone not playing, use extended poll interval from
    // config
    lock_state();
    uint16_t sleep_poll_stopped = s_state.cfg.sleep_poll_stopped_sec;
    unlock_state();
    if (!s_last_is_playing && sleep_poll_stopped > 0) {
      delay_ms = sleep_poll_stopped * 1000; // Config is in seconds
    } else {
      delay_ms = POLL_DELAY_SLEEPING_MS; // Default 30s when playing
    }
  } else if (platform_battery_is_charging()) {
    delay_ms = POLL_DELAY_AWAKE_CHARGING_MS; // Fast polling when plugged in
  } else {
    delay_ms = POLL_DELAY_AWAKE_BATTERY_MS; // Slower on battery to save power
  }
  uint64_t start = platform_millis();
  while (s_running) {
    if (s_trigger_poll) {
      s_trigger_poll = false;
      break;
    }
    if (platform_millis() - start >= delay_ms) {
      break;
    }
    platform_sleep_ms(50);
  }
}

// Fallback bridge URL when mDNS discovery fails and no bridge is stored
#ifndef CONFIG_RK_DEFAULT_BRIDGE_BASE
#define CONFIG_RK_DEFAULT_BRIDGE_BASE "http://127.0.0.1:8088"
#endif

// Strip trailing slashes from URL to prevent double-slash issues
static void strip_trailing_slashes(char *url) {
  if (!url)
    return;
  size_t len = strlen(url);
  while (len > 0 && url[len - 1] == '/') {
    url[--len] = '\0';
  }
}

/// Try to discover the bridge via UDP broadcast.
/// Sends a poll packet to 255.255.255.255:8089 and extracts the source IP
/// from the response. Returns true if bridge was discovered and saved.
static bool udp_broadcast_discover(void) {
#ifdef ESP_PLATFORM
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0)
    return false;

  // Enable broadcast and set short timeout
  int one = 1;
  setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
  struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // Build discovery poll with empty zone_id and SHA
  udp_fast_request_t req;
  memset(&req, 0, sizeof(req));
  req.magic = UDP_FAST_MAGIC;

  struct sockaddr_in dest;
  memset(&dest, 0, sizeof(dest));
  dest.sin_family = AF_INET;
  dest.sin_port = htons(8088 + UDP_FAST_PORT_OFFSET);
  dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);

  ssize_t sent = sendto(sock, &req, sizeof(req), 0, (struct sockaddr *)&dest,
                        sizeof(dest));
  if (sent != sizeof(req)) {
    close(sock);
    return false;
  }

  // Wait for response. Source address is the bridge.
  udp_fast_response_t resp;
  struct sockaddr_in from;
  socklen_t from_len = sizeof(from);
  ssize_t recvd = recvfrom(sock, &resp, sizeof(resp), 0,
                           (struct sockaddr *)&from, &from_len);
  close(sock);

  if (recvd != sizeof(resp))
    return false;
  if (resp.magic != UDP_FAST_MAGIC || resp.version != 1)
    return false;

  // Extract bridge IP from response source address
  char ip[16];
  uint32_t addr = ntohl(from.sin_addr.s_addr);
  snprintf(ip, sizeof(ip), "%d.%d.%d.%d", (int)((addr >> 24) & 0xFF),
           (int)((addr >> 16) & 0xFF), (int)((addr >> 8) & 0xFF),
           (int)(addr & 0xFF));
  LOGI("UDP broadcast discovered bridge at %s", ip);

  lock_state();
  snprintf(s_state.cfg.bridge_base, sizeof(s_state.cfg.bridge_base),
           "http://%s:8088", ip);
  s_state.cfg.bridge_from_mdns = 1;
  platform_storage_save(&s_state.cfg);
  unlock_state();
  manifest_ui_set_message("Bridge: Found");
  return true;
#else
  return false;
#endif
}

static void maybe_update_bridge_base(void) {
  // Only use mDNS when no bridge URL is configured.
  // This respects user-set URLs (via web config) and allows Clear to trigger
  // fresh discovery.
  lock_state();
  bool need_discovery = (s_state.cfg.bridge_base[0] == '\0');
  unlock_state();

  if (!need_discovery) {
    return; // Bridge URL already configured - don't overwrite with mDNS
  }

  // Try UDP broadcast first — works even when mDNS is broken.
  // Sends a poll to 255.255.255.255:8089; bridge responds normally.
  if (udp_broadcast_discover()) {
    s_mdns_fail_count = 0;
    return;
  }

  // Fall back to mDNS (skip if not initialized yet)
  if (!platform_mdns_is_ready()) {
    return;
  }

  // Bridge is empty - try mDNS discovery
  char discovered[sizeof(s_state.cfg.bridge_base)];
  bool mdns_ok =
      platform_mdns_discover_base_url(discovered, sizeof(discovered));

  if (mdns_ok && host_is_valid(discovered)) {
    // mDNS found a bridge - save it
    s_mdns_fail_count = 0;
    lock_state();
    LOGI("mDNS discovered bridge: %s", discovered);
    strncpy(s_state.cfg.bridge_base, discovered,
            sizeof(s_state.cfg.bridge_base) - 1);
    s_state.cfg.bridge_base[sizeof(s_state.cfg.bridge_base) - 1] = '\0';
    strip_trailing_slashes(s_state.cfg.bridge_base);
    s_state.cfg.bridge_from_mdns = 1; // Persist mDNS source
    platform_storage_save(&s_state.cfg);
    unlock_state();
    manifest_ui_set_message("Bridge: Found");
    return;
  }

  // mDNS failed - try compile-time default fallback
  if (CONFIG_RK_DEFAULT_BRIDGE_BASE[0] != '\0') {
    LOGI("mDNS discovery failed, using fallback: %s",
         CONFIG_RK_DEFAULT_BRIDGE_BASE);
    lock_state();
    strncpy(s_state.cfg.bridge_base, CONFIG_RK_DEFAULT_BRIDGE_BASE,
            sizeof(s_state.cfg.bridge_base) - 1);
    s_state.cfg.bridge_base[sizeof(s_state.cfg.bridge_base) - 1] = '\0';
    strip_trailing_slashes(s_state.cfg.bridge_base);
    // Don't save the fallback - let mDNS retry on next poll
    unlock_state();
  } else {
    // No fallback configured - increment mDNS failure counter
    if (s_mdns_fail_count < MDNS_FAIL_THRESHOLD) {
      s_mdns_fail_count++;
    }
    LOGW("mDNS discovery failed (%d/%d) - use Settings to configure bridge",
         s_mdns_fail_count, MDNS_FAIL_THRESHOLD);
  }
}


// ── Manifest fetch ──────────────────────────────────────────────────────────

static char s_manifest_sha[9] = {0}; // Cached SHA for 304 support
static int s_udp_sock = -1; // Persistent UDP socket for fast-path polling

/// Parse host and port from bridge_base URL (e.g. "http://192.168.50.225:8088")
/// Returns true on success, writing host (null-terminated) and port.
static bool parse_bridge_host_port(const char *bridge_base, char *host_out,
                                   size_t host_len, uint16_t *port_out) {
  if (!bridge_base || !bridge_base[0])
    return false;
  const char *hp = bridge_base;
  const char *scheme = strstr(bridge_base, "://");
  if (scheme)
    hp = scheme + 3;
  // Find end of host (colon or slash or NUL)
  const char *colon = NULL;
  const char *end = hp;
  while (*end && *end != '/' && *end != '\0') {
    if (*end == ':' && !colon)
      colon = end;
    end++;
  }
  if (colon) {
    size_t hlen = (size_t)(colon - hp);
    if (hlen >= host_len)
      hlen = host_len - 1;
    memcpy(host_out, hp, hlen);
    host_out[hlen] = '\0';
    *port_out = (uint16_t)atoi(colon + 1);
  } else {
    size_t hlen = (size_t)(end - hp);
    if (hlen >= host_len)
      hlen = host_len - 1;
    memcpy(host_out, hp, hlen);
    host_out[hlen] = '\0';
    *port_out = 8088; // default
  }
  return host_out[0] != '\0';
}

/// Try UDP fast-path poll. Returns true if we got a valid response and
/// populated *resp_out. On failure, caller should fall back to HTTP.
static bool udp_poll_fast_state(udp_fast_response_t *resp_out) {
  if (!resp_out)
    return false;

  // Get bridge_base and zone_id under lock
  char bridge_base[128];
  char zone_id[64];
  lock_state();
  strncpy(bridge_base, s_state.cfg.bridge_base, sizeof(bridge_base) - 1);
  bridge_base[sizeof(bridge_base) - 1] = '\0';
  strncpy(zone_id, s_state.cfg.zone_id, sizeof(zone_id) - 1);
  zone_id[sizeof(zone_id) - 1] = '\0';
  unlock_state();

  if (bridge_base[0] == '\0' || zone_id[0] == '\0')
    return false;

  // Parse host and port from bridge_base
  char host[64];
  uint16_t bridge_port = 8088;
  if (!parse_bridge_host_port(bridge_base, host, sizeof(host), &bridge_port))
    return false;
  uint16_t udp_port = bridge_port + UDP_FAST_PORT_OFFSET;

  // Create socket once, reuse across polls
  if (s_udp_sock < 0) {
    s_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_udp_sock < 0) {
      LOGW("UDP: socket creation failed");
      return false;
    }
    // Set 500ms receive timeout
    struct timeval tv = {.tv_sec = 0, .tv_usec = 500000};
    setsockopt(s_udp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  }

  // Resolve host to IP
  struct sockaddr_in dest;
  memset(&dest, 0, sizeof(dest));
  dest.sin_family = AF_INET;
  dest.sin_port = htons(udp_port);

  // Try direct IP parse first, then DNS
  if (inet_aton(host, &dest.sin_addr) == 0) {
    struct hostent *he = gethostbyname(host);
    if (!he) {
      LOGW("UDP: DNS resolve failed for %s", host);
      return false;
    }
    memcpy(&dest.sin_addr, he->h_addr_list[0], sizeof(dest.sin_addr));
  }

  // Build request
  udp_fast_request_t req;
  memset(&req, 0, sizeof(req));
  req.magic = UDP_FAST_MAGIC; // LE native on ESP32
  strncpy((char *)req.sha, s_manifest_sha, sizeof(req.sha) - 1);
  strncpy(req.zone_id, zone_id, sizeof(req.zone_id) - 1);

  // Send
  ssize_t sent = sendto(s_udp_sock, &req, sizeof(req), 0,
                        (struct sockaddr *)&dest, sizeof(dest));
  if (sent != sizeof(req)) {
    LOGW("UDP: sendto failed (%d)", (int)sent);
    return false;
  }

  // Receive
  udp_fast_response_t resp;
  ssize_t recvd = recvfrom(s_udp_sock, &resp, sizeof(resp), 0, NULL, NULL);
  if (recvd != sizeof(resp)) {
    // Timeout or wrong size — not an error, bridge may not support UDP yet
    return false;
  }

  // Validate
  if (resp.magic != UDP_FAST_MAGIC || resp.version != 1) {
    LOGW("UDP: bad magic=0x%04X or version=%d", resp.magic, resp.version);
    return false;
  }

  *resp_out = resp;
  return true;
}

/// Send a volume command via UDP (non-blocking fire-and-forget).
/// Returns true on success, false if UDP unavailable (caller should fall back
/// to HTTP).
static bool udp_send_volume(float volume) {
  if (s_udp_sock < 0)
    return false;

  char bridge_base[128];
  char zone_id[64];
  lock_state();
  strncpy(bridge_base, s_state.cfg.bridge_base, sizeof(bridge_base) - 1);
  bridge_base[sizeof(bridge_base) - 1] = '\0';
  strncpy(zone_id, s_state.cfg.zone_id, sizeof(zone_id) - 1);
  zone_id[sizeof(zone_id) - 1] = '\0';
  unlock_state();

  if (bridge_base[0] == '\0' || zone_id[0] == '\0')
    return false;

  char host[64];
  uint16_t bridge_port = 8088;
  if (!parse_bridge_host_port(bridge_base, host, sizeof(host), &bridge_port))
    return false;
  uint16_t udp_port = bridge_port + UDP_FAST_PORT_OFFSET;

  struct sockaddr_in dest;
  memset(&dest, 0, sizeof(dest));
  dest.sin_family = AF_INET;
  dest.sin_port = htons(udp_port);

  if (inet_aton(host, &dest.sin_addr) == 0) {
    struct hostent *he = gethostbyname(host);
    if (!he)
      return false;
    memcpy(&dest.sin_addr, he->h_addr_list[0], sizeof(dest.sin_addr));
  }

  udp_command_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.magic = UDP_FAST_MAGIC;
  cmd.cmd = UDP_CMD_VOLUME_SET;
  strncpy(cmd.zone_id, zone_id, sizeof(cmd.zone_id) - 1);
  cmd.value = volume;

  ssize_t sent = sendto(s_udp_sock, &cmd, sizeof(cmd), 0,
                        (struct sockaddr *)&dest, sizeof(dest));
  return (sent == sizeof(cmd));
}

/// Fetch manifest from bridge. Returns heap-allocated manifest_t on success.
/// Caller must free the returned pointer.
static manifest_t *fetch_manifest(void) {
  lock_state();
  char bridge_base[sizeof(s_state.cfg.bridge_base)];
  char zone_id[sizeof(s_state.cfg.zone_id)];
  strncpy(bridge_base, s_state.cfg.bridge_base, sizeof(bridge_base) - 1);
  strncpy(zone_id, s_state.cfg.zone_id, sizeof(zone_id) - 1);
  unlock_state();

  if (bridge_base[0] == '\0' || zone_id[0] == '\0')
    return NULL;

  char url[384];
  // Send SHA for fast-path: bridge returns only fast state when screens
  // unchanged
  if (s_manifest_sha[0]) {
    snprintf(url, sizeof(url), "%s/knob/manifest?zone_id=%s&sha=%s",
             bridge_base, zone_id, s_manifest_sha);
  } else {
    snprintf(url, sizeof(url), "%s/knob/manifest?zone_id=%s", bridge_base,
             zone_id);
  }
  char *resp = NULL;
  size_t resp_len = 0;
  int ret = platform_http_get(url, &resp, &resp_len);
  if (ret != 0 || !resp || resp_len == 0) {
    platform_http_free(resp);
    return NULL;
  }

  if (ret != 0 || !resp || resp_len == 0) {
    platform_http_free(resp);
    return NULL;
  }

  manifest_t *m = malloc(sizeof(manifest_t));
  if (!m) {
    platform_http_free(resp);
    return NULL;
  }

  if (!manifest_parse(resp, resp_len, m)) {
    free(m);
    platform_http_free(resp);
    return NULL;
  }

  // Cache SHA for next request
  strncpy(s_manifest_sha, m->sha, sizeof(s_manifest_sha) - 1);

  platform_http_free(resp);
  return m;
}

/// UI callback for manifest update (runs on UI thread).
static void ui_manifest_cb(void *arg) {
  manifest_t *m = arg;
  if (!m)
    return;

  // Cache volume for optimistic UI
  s_last_known_volume = m->fast.volume;
  s_last_known_volume_min = m->fast.volume_min;
  s_last_known_volume_max = m->fast.volume_max;
  s_last_known_volume_step = m->fast.volume_step;

  // Cache interactions for config-driven input dispatch
  s_has_interactions = m->has_interactions;
  if (m->has_interactions) {
    memcpy(&s_cached_interactions, &m->interactions,
           sizeof(s_cached_interactions));
  }

  // Cache per-screen encoder config for v2 command-pattern dispatch
  s_cached_encoder_count = 0;
  for (int i = 0; i < m->screen_count && i < MAX_CACHED_SCREENS; i++) {
    cached_screen_encoder_t *ce = &s_cached_encoders[s_cached_encoder_count];
    strncpy(ce->screen_id, m->screens[i].id, sizeof(ce->screen_id) - 1);
    ce->screen_id[sizeof(ce->screen_id) - 1] = '\0';
    ce->has_encoder = m->screens[i].has_encoder;
    if (m->screens[i].has_encoder) {
      memcpy(&ce->encoder, &m->screens[i].encoder, sizeof(ce->encoder));
    }
    s_cached_encoder_count++;
  }

  // Cache per-screen elements for v2 button tap dispatch
  s_cached_element_count = 0;
  for (int i = 0; i < m->screen_count && i < MAX_CACHED_SCREENS; i++) {
    cached_screen_elements_t *ce = &s_cached_elements[s_cached_element_count];
    strncpy(ce->screen_id, m->screens[i].id, sizeof(ce->screen_id) - 1);
    ce->screen_id[sizeof(ce->screen_id) - 1] = '\0';
    ce->element_count = m->screens[i].element_count;
    if (m->screens[i].element_count > 0) {
      memcpy(ce->elements, m->screens[i].elements, sizeof(ce->elements));
    }
    s_cached_element_count++;
  }

  manifest_ui_update(m);

  // Fetch artwork whenever screens are updated (SHA changed)
  for (int i = 0; i < m->screen_count; i++) {
    if (m->screens[i].type == SCREEN_TYPE_MEDIA) {
      const char *url = m->screens[i].data.media.image_url;
      if (url[0]) {
        manifest_ui_set_artwork(url);
      }
      break;
    }
  }
  free(m);
}
static void post_manifest_update(manifest_t *m) {
  platform_task_post_to_ui(ui_manifest_cb, m);
}
static bool refresh_zone_label(bool prefer_zone_id) {
  LOGI("refresh_zone_label: Called (prefer_zone_id=%s)",
       prefer_zone_id ? "true" : "false");
  lock_state();
  char bridge_base[sizeof(s_state.cfg.bridge_base)];
  strncpy(bridge_base, s_state.cfg.bridge_base, sizeof(bridge_base) - 1);
  unlock_state();
  if (bridge_base[0] == '\0') {
    LOGI("refresh_zone_label: bridge_base is empty, returning false");
    return false;
  }

  // Get knob ID for zone filtering
  char knob_id[16];
  platform_http_get_knob_id(knob_id, sizeof(knob_id));

  char url[256];
  snprintf(url, sizeof(url), "%s/zones?knob_id=%s", bridge_base, knob_id);
  LOGI("refresh_zone_label: Requesting %s", url);

  char *resp = NULL;
  size_t resp_len = 0;
  bool success = false;

  if (platform_http_get(url, &resp, &resp_len) != 0 || !resp) {
    LOGI("refresh_zone_label: HTTP request failed");
    platform_http_free(resp);
    return false;
  }

  LOGI("refresh_zone_label: Received %zu bytes", resp_len);
  parse_zones_from_response(resp);

  char zone_label_copy[MAX_ZONE_NAME] = {0};
  lock_state();
  LOGI("refresh_zone_label: Parsed %d zones", s_state.zone_count);
  if (s_state.zone_count > 0) {
    bool found = false;
    bool should_sync = false;
    for (int i = 0; i < s_state.zone_count; ++i) {
      struct zone_entry *entry = &s_state.zones[i];
      if (prefer_zone_id && s_state.cfg.zone_id[0] &&
          strcmp(entry->id, s_state.cfg.zone_id) == 0) {
        strncpy(s_state.zone_label, entry->name,
                sizeof(s_state.zone_label) - 1);
        s_state.zone_label[sizeof(s_state.zone_label) - 1] = '\0';
        strncpy(zone_label_copy, s_state.zone_label,
                sizeof(zone_label_copy) - 1);
        found = true;
        should_sync = true;
        break;
      }
      if (!s_state.cfg.zone_id[0]) {
        strncpy(s_state.cfg.zone_id, entry->id,
                sizeof(s_state.cfg.zone_id) - 1);
        s_state.cfg.zone_id[sizeof(s_state.cfg.zone_id) - 1] = '\0';
        strncpy(s_state.zone_label, entry->name,
                sizeof(s_state.zone_label) - 1);
        s_state.zone_label[sizeof(s_state.zone_label) - 1] = '\0';
        strncpy(zone_label_copy, s_state.zone_label,
                sizeof(zone_label_copy) - 1);
        found = true;
        should_sync = true;
        break;
      }
    }
    if (!found && s_state.zone_count > 0) {
      struct zone_entry *entry = &s_state.zones[0];
      strncpy(s_state.cfg.zone_id, entry->id, sizeof(s_state.cfg.zone_id) - 1);
      s_state.cfg.zone_id[sizeof(s_state.cfg.zone_id) - 1] = '\0';
      strncpy(s_state.zone_label, entry->name, sizeof(s_state.zone_label) - 1);
      s_state.zone_label[sizeof(s_state.zone_label) - 1] = '\0';
      strncpy(zone_label_copy, s_state.zone_label, sizeof(zone_label_copy) - 1);
      should_sync = true;
    }
    s_state.zone_resolved = true;
    if (s_device_state != DEVICE_STATE_OPERATIONAL) {
      LOGI("Device state: %s -> OPERATIONAL (zones loaded)",
           device_state_name(s_device_state));
      s_device_state = DEVICE_STATE_OPERATIONAL;
      manifest_ui_set_network_status(NULL); // Clear status banner when ready
    }
    success = should_sync && zone_label_copy[0] != '\0';
  }
  unlock_state();

  platform_http_free(resp);
  if (success) {
    LOGI("refresh_zone_label: Selected zone '%s', posting to UI",
         zone_label_copy);
    platform_storage_save(&s_state.cfg);
    manifest_ui_set_zone_name(zone_label_copy);
  } else {
    LOGI("refresh_zone_label: No zone selected (success=false)");
  }
  return success;
}

static void parse_zones_from_response(const char *resp) {
  if (!resp) {
    return;
  }
  lock_state();
  s_state.zone_count = 0;
  const char *cursor = resp;
  while (s_state.zone_count < MAX_ZONES &&
         (cursor = strstr(cursor, "\"zone_id\""))) {
    char id[MAX_ZONE_NAME] = {0};
    char name[MAX_ZONE_NAME] = {0};
    const char *next =
        extract_json_string(cursor, "\"zone_id\"", id, sizeof(id));
    if (!next) {
      break;
    }
    const char *after_name =
        extract_json_string(next, "\"zone_name\"", name, sizeof(name));
    if (!after_name) {
      cursor = next;
      continue;
    }
    strncpy(s_state.zones[s_state.zone_count].id, id,
            sizeof(s_state.zones[0].id) - 1);
    strncpy(s_state.zones[s_state.zone_count].name, name,
            sizeof(s_state.zones[0].name) - 1);
    s_state.zones[s_state.zone_count].id[sizeof(s_state.zones[0].id) - 1] =
        '\0';
    s_state.zones[s_state.zone_count].name[sizeof(s_state.zones[0].name) - 1] =
        '\0';
    s_state.zone_count++;
    cursor = after_name;
  }
  unlock_state();
}

static const char *extract_json_string(const char *start, const char *key,
                                       char *out, size_t len) {
  const char *key_pos = strstr(start, key);
  if (!key_pos) {
    return NULL;
  }
  const char *colon = strchr(key_pos, ':');
  if (!colon) {
    return NULL;
  }
  const char *quote_start = strchr(colon, '"');
  if (!quote_start) {
    return NULL;
  }
  quote_start++;
  const char *quote_end = strchr(quote_start, '"');
  if (!quote_end) {
    return NULL;
  }
  size_t copy_len = quote_end - quote_start;
  if (copy_len >= len) {
    copy_len = len - 1;
  }
  memcpy(out, quote_start, copy_len);
  out[copy_len] = '\0';
  return quote_end + 1;
}

static bool send_control_json(const char *json) {
  if (!json) {
    return false;
  }
  lock_state();
  char bridge_base[sizeof(s_state.cfg.bridge_base)];
  char zone_id[sizeof(s_state.cfg.zone_id)];
  strncpy(bridge_base, s_state.cfg.bridge_base, sizeof(bridge_base) - 1);
  strncpy(zone_id, s_state.cfg.zone_id, sizeof(zone_id) - 1);
  unlock_state();
  if (bridge_base[0] == '\0' || zone_id[0] == '\0') {
    return false;
  }
  char url[256];
  snprintf(url, sizeof(url), "%s/control", bridge_base);
  char *resp = NULL;
  size_t resp_len = 0;
  int ret = platform_http_post_json(url, json, &resp, &resp_len);
  if (ret != 0) {
    platform_http_free(resp);
    return false;
  }
  if (resp && strstr(resp, "\"error\"")) {
    platform_http_free(resp);
    return false;
  }
  platform_http_free(resp);
  return true;
}

static void bridge_poll_thread(void *arg) {
  (void)arg;
  LOGI("Bridge poll thread started");
  while (s_running) {
    // Skip HTTP requests if network is not ready yet (or in BLE mode)
    // In BLE mode, s_network_ready is false, so we just sleep without logging
    if (!s_network_ready) {
      wait_for_poll_interval();
      continue;
    }

    // Only run mDNS discovery if:
    // 1. We haven't verified a working bridge yet, OR
    // 2. It's been over an hour since last check (in case bridge IP changed)
    uint32_t now_ms = (uint32_t)platform_millis();
    bool should_check_mdns =
        !s_bridge_verified ||
        (now_ms - s_last_mdns_check_ms > MDNS_RECHECK_INTERVAL_MS);
    if (should_check_mdns) {
      maybe_update_bridge_base();
      s_last_mdns_check_ms = now_ms;
    }

    // If bridge URL is still empty after mDNS/UDP discovery attempt,
    // show a helpful status screen immediately rather than letting the
    // failed HTTP/UDP calls produce a blank screen.
    lock_state();
    bool bridge_base_empty = (s_state.cfg.bridge_base[0] == '\0');
    unlock_state();
    if (bridge_base_empty && s_device_state == DEVICE_STATE_CONNECTED) {
      char status_msg[128];
      manifest_ui_set_zone_name(""); // Clear zone name to avoid overlay
      if (s_mdns_fail_count >= MDNS_FAIL_THRESHOLD) {
        // Many attempts failed — show "Bridge Not Found" with config URL
        if (s_device_ip[0]) {
          snprintf(status_msg, sizeof(status_msg),
                   "Bridge Not Found\nConfigure at http://%s", s_device_ip);
        } else {
          snprintf(status_msg, sizeof(status_msg),
                   "Bridge Not Found\nUse zone menu > Settings");
        }
      } else {
        // Still searching — show progress with device IP for manual config
        if (s_device_ip[0]) {
          snprintf(status_msg, sizeof(status_msg),
                   "Searching for bridge...\nKnob IP: %s", s_device_ip);
        } else {
          snprintf(status_msg, sizeof(status_msg),
                   "Searching for bridge...\nAttempt %d of %d",
                   s_mdns_fail_count + 1, MDNS_FAIL_THRESHOLD);
        }
      }
      manifest_ui_set_network_status(status_msg);
      wait_for_poll_interval();
      continue;
    }

    // Show status immediately before any HTTP timeouts
    if (!s_last_net_ok && !s_bridge_verified) {
      lock_state();
      char bridge_url[64];
      strncpy(bridge_url, s_state.cfg.bridge_base, sizeof(bridge_url) - 1);
      bridge_url[sizeof(bridge_url) - 1] = '\0';
      unlock_state();
      if (bridge_url[0]) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Connecting...\n%.50s", bridge_url);
        manifest_ui_set_network_status(msg);
      }
    }

    if (!s_state.zone_resolved) {
      refresh_zone_label(true);
    }
    // ── UDP fast-path: try lightweight poll first ──
    udp_fast_response_t udp_resp;
    bool udp_ok = udp_poll_fast_state(&udp_resp);
    bool sha_changed = false;
    manifest_t *manifest = NULL;
    bool ok = false;

    if (udp_ok) {
      // Check if SHA changed — need full HTTP manifest for screens
      char udp_sha[MANIFEST_SHA_LEN];
      memset(udp_sha, 0, sizeof(udp_sha));
      memcpy(udp_sha, udp_resp.sha,
             sizeof(udp_resp.sha) < sizeof(udp_sha) ? sizeof(udp_resp.sha)
                                                    : sizeof(udp_sha) - 1);
      sha_changed = (strcmp(udp_sha, s_manifest_sha) != 0);

      if (sha_changed) {
        // SHA changed — fetch full manifest via HTTP for screen updates
        manifest = fetch_manifest();
        ok = (manifest != NULL);
      } else {
        // SHA same — build fast-only manifest from UDP response
        manifest = malloc(sizeof(manifest_t));
        if (manifest) {
          memset(manifest, 0, sizeof(manifest_t));
          manifest->version = 1;
          strncpy(manifest->sha, s_manifest_sha, sizeof(manifest->sha) - 1);
          manifest->screen_count = 0; // No screen updates
          manifest->fast.is_playing = (udp_resp.flags & UDP_FLAG_PLAYING) != 0;
          manifest->fast.transport.play =
              (udp_resp.flags & UDP_FLAG_PLAY_OK) != 0;
          manifest->fast.transport.pause =
              (udp_resp.flags & UDP_FLAG_PAUSE_OK) != 0;
          manifest->fast.transport.next =
              (udp_resp.flags & UDP_FLAG_NEXT_OK) != 0;
          manifest->fast.transport.prev =
              (udp_resp.flags & UDP_FLAG_PREV_OK) != 0;
          manifest->fast.volume = udp_resp.volume;
          manifest->fast.volume_min = udp_resp.volume_min;
          manifest->fast.volume_max = udp_resp.volume_max;
          manifest->fast.volume_step = udp_resp.volume_step;
          manifest->fast.seek_position = udp_resp.seek_position;
          manifest->fast.length = (int)udp_resp.length;
          ok = true;
        }
      }
    }

    // Log transport method (every 30th poll to avoid spam)
    static int s_udp_poll_count = 0;
    if (udp_ok) {
      s_udp_poll_count++;
      if (s_udp_poll_count == 1 || s_udp_poll_count % 30 == 0) {
        LOGI("UDP fast-path OK (poll #%d, sha_changed=%d, vol=%.0f)",
             s_udp_poll_count, sha_changed, udp_resp.volume);
      }
    } else {
      s_udp_poll_count = 0;
    }

    if (!ok) {
      // UDP failed or unavailable — fall back to HTTP
      LOGI("UDP unavailable, falling back to HTTP");
      manifest = fetch_manifest();
      ok = (manifest != NULL);
    }

    manifest_ui_set_status(ok);

    if (ok) {
      s_last_is_playing = manifest->fast.is_playing;
    }
    // Note: config_sha and zones_sha are not in the manifest response.
    // TODO: Add these to manifest fast state, or keep a parallel /now_playing
    // call.
    check_charging_state_change();

    if (ok) {
      post_manifest_update(manifest); // Ownership transfers to UI thread
      if (!s_last_net_ok) {
        // Just connected - clear status, restore zone name, mark verified
        reset_bridge_fail_count();
        manifest_ui_set_message("Bridge: Connected");
        manifest_ui_set_network_status(NULL);
        s_bridge_verified = true;
        // Restore zone name (was cleared during error display)
        lock_state();
        char zone_name_copy[MAX_ZONE_NAME];
        strncpy(zone_name_copy, s_state.zone_label, sizeof(zone_name_copy) - 1);
        zone_name_copy[sizeof(zone_name_copy) - 1] = '\0';
        unlock_state();
        if (zone_name_copy[0]) {
          manifest_ui_set_zone_name(zone_name_copy);
        }
      }
    } else if (!ok && s_last_net_ok) {
      // Just lost connection to bridge - start retry tracking
      // line1=main content (bottom), line2=header (top)
      increment_bridge_fail_count();
      s_bridge_verified = false;
      char line1_msg[64];
      char status_msg[96];
      snprintf(line1_msg, sizeof(line1_msg), "Attempt %d of %d...",
               s_bridge_fail_count, BRIDGE_FAIL_THRESHOLD);
      manifest_ui_set_zone_name(""); // Clear zone name to avoid overlay
      snprintf(status_msg, sizeof(status_msg),
               "Testing Bridge\nAttempt %d of %d...", s_bridge_fail_count,
               BRIDGE_FAIL_THRESHOLD);
      manifest_ui_set_network_status(status_msg);
    } else if (!ok && !s_last_net_ok) {
      // Still trying to connect - check if we have a bridge URL
      lock_state();
      bool has_bridge = (s_state.cfg.bridge_base[0] != '\0');
      unlock_state();

      if (!has_bridge) {
        // No bridge URL - searching via mDNS
        // Show retry progress or recovery info based on failure count
        char line1_msg[64];
        char line2_msg[64];
        char status_msg[96];
        manifest_ui_set_zone_name(""); // Clear zone name to avoid overlay

        if (s_mdns_fail_count >= MDNS_FAIL_THRESHOLD) {
          // mDNS search exhausted - show recovery info
          if (s_device_ip[0]) {
            snprintf(line1_msg, sizeof(line1_msg), "http://%s", s_device_ip);
            snprintf(line2_msg, sizeof(line2_msg), "Set Bridge URL at:");
            snprintf(status_msg, sizeof(status_msg),
                     "mDNS failed. Set Bridge at http://%s", s_device_ip);
          } else {
            snprintf(line1_msg, sizeof(line1_msg), "Use zone menu > Settings");
            snprintf(line2_msg, sizeof(line2_msg), "Bridge Not Found");
            snprintf(status_msg, sizeof(status_msg),
                     "mDNS failed. Configure Bridge in Settings.");
          }
          manifest_ui_set_network_status(status_msg);
        } else {
          // Still searching - show progress
          snprintf(line1_msg, sizeof(line1_msg), "Attempt %d of %d...",
                   s_mdns_fail_count + 1, MDNS_FAIL_THRESHOLD);
          snprintf(status_msg, sizeof(status_msg),
                   "Searching for Bridge\nAttempt %d of %d...",
                   s_mdns_fail_count + 1, MDNS_FAIL_THRESHOLD);
          manifest_ui_set_network_status(status_msg);
        }
      } else {
        // Bridge URL configured but not responding - show retry progress
        increment_bridge_fail_count();
        char line1_msg[64];
        char status_msg[96];

        if (s_bridge_fail_count >= BRIDGE_FAIL_THRESHOLD) {
          // Max retries reached - show recovery info with device IP
          // line1=main content (bottom), line2=header (top)
          char line2_msg[64];
          if (s_device_ip[0]) {
            snprintf(line1_msg, sizeof(line1_msg), "http://%s", s_device_ip);
            snprintf(line2_msg, sizeof(line2_msg), "Update Bridge at:");
            snprintf(status_msg, sizeof(status_msg),
                     "Bridge unreachable\nUpdate at http://%s", s_device_ip);
          } else {
            snprintf(line1_msg, sizeof(line1_msg), "Use zone menu > Settings");
            snprintf(line2_msg, sizeof(line2_msg), "Bridge Unreachable");
            snprintf(status_msg, sizeof(status_msg),
                     "Bridge unreachable. Check Settings.");
          }
          manifest_ui_set_zone_name(""); // Clear zone name to avoid overlay
          manifest_ui_set_network_status(status_msg);
        } else {
          // Still retrying - show progress on main display
          // line1=main content (bottom), line2=header (top)
          snprintf(line1_msg, sizeof(line1_msg), "Attempt %d of %d...",
                   s_bridge_fail_count, BRIDGE_FAIL_THRESHOLD);
          manifest_ui_set_zone_name(""); // Clear zone name to avoid overlay
          snprintf(status_msg, sizeof(status_msg),
                   "Testing Bridge\nAttempt %d of %d...",
                   s_bridge_fail_count, BRIDGE_FAIL_THRESHOLD);
          manifest_ui_set_network_status(status_msg);
        }
      }
    }
    s_last_net_ok = ok;
    wait_for_poll_interval();
  }
}

void bridge_client_start(const rk_cfg_t *cfg) {
  if (!cfg) {
    return;
  }
  platform_task_init();
  lock_state();
  s_state.cfg = *cfg;
  strncpy(s_state.zone_label,
          cfg->zone_id[0] ? cfg->zone_id : "Tap here to select zone",
          sizeof(s_state.zone_label) - 1);
  s_state.zone_label[sizeof(s_state.zone_label) - 1] = '\0';
  unlock_state();

  // Always apply config on startup (uses defaults if no saved config)
  // This ensures rotation is applied even on fresh devices
  LOGI("Applying config on startup: rot=%d/%d sha='%s'", cfg->rotation_charging,
       cfg->rotation_not_charging,
       cfg->config_sha[0] ? cfg->config_sha : "(none)");
  apply_knob_config(cfg);

  s_running = true;
  platform_task_start(bridge_poll_thread, NULL);
}

/// Look up the encoder for the currently visible screen.
/// Returns NULL if the current screen has no encoder config.
static const manifest_encoder_t *get_current_encoder(void) {
    const char *screen_id = manifest_ui_current_screen_id();
    if (!screen_id)
        return NULL;
    for (int i = 0; i < s_cached_encoder_count; i++) {
        if (strcmp(s_cached_encoders[i].screen_id, screen_id) == 0) {
            return s_cached_encoders[i].has_encoder
                       ? &s_cached_encoders[i].encoder
                       : NULL;
        }
    }
    return NULL;
}

/// Look up an element by its index in the currently visible screen's element list.
/// Returns NULL if the current screen has no such element.
static const manifest_element_t *get_element_for_button(int element_idx) {
    if (element_idx < 0)
        return NULL;
    const char *screen_id = manifest_ui_current_screen_id();
    if (!screen_id)
        return NULL;
    for (int i = 0; i < s_cached_element_count; i++) {
        if (strcmp(s_cached_elements[i].screen_id, screen_id) == 0) {
            if (element_idx < s_cached_elements[i].element_count)
                return &s_cached_elements[i].elements[element_idx];
            return NULL;
        }
    }
    return NULL;
}

/// Dispatch a manifest action. Handles volume fast-path and generic JSON.
static void dispatch_action(const manifest_action_t *act) {
    if (!act || !act->action[0])
        return;

    // Buffer must hold: zone_id (64) + action (32) + params_json (128) + JSON template overhead (~37) + null = ~262 bytes minimum.
    char body[384];
    if (strcmp(act->action, "volume_up") == 0) {
        lock_state();
        float predicted_up = s_last_known_volume + s_last_known_volume_step;
        if (predicted_up > s_last_known_volume_max)
            predicted_up = s_last_known_volume_max;
        s_last_known_volume = predicted_up;
        snprintf(body, sizeof(body),
                 "{\"zone_id\":\"%s\",\"action\":\"vol_abs\",\"value\":%.10g}",
                 s_state.cfg.zone_id, predicted_up);
        unlock_state();
        manifest_ui_show_volume_change(predicted_up, s_last_known_volume_step);
        if (!udp_send_volume(predicted_up)) {
            if (!send_control_json(body))
                manifest_ui_set_message("Volume change failed");
        }
    } else if (strcmp(act->action, "volume_down") == 0) {
        lock_state();
        float predicted_down = s_last_known_volume - s_last_known_volume_step;
        if (predicted_down < s_last_known_volume_min)
            predicted_down = s_last_known_volume_min;
        s_last_known_volume = predicted_down;
        snprintf(body, sizeof(body),
                 "{\"zone_id\":\"%s\",\"action\":\"vol_abs\",\"value\":%.10g}",
                 s_state.cfg.zone_id, predicted_down);
        unlock_state();
        manifest_ui_show_volume_change(predicted_down, s_last_known_volume_step);
        if (!udp_send_volume(predicted_down)) {
            if (!send_control_json(body))
                manifest_ui_set_message("Volume change failed");
        }
    } else if (strcmp(act->action, "show_zone_picker") == 0) {
        manifest_ui_show_zone_picker();
    } else {
        // Generic action: send JSON to bridge
        lock_state();
        if (act->has_params && act->params_json[0]) {
            snprintf(body, sizeof(body),
                     "{\"zone_id\":\"%s\",\"action\":\"%s\",\"params\":%s}",
                     s_state.cfg.zone_id, act->action, act->params_json);
        } else {
            snprintf(body, sizeof(body),
                     "{\"zone_id\":\"%s\",\"action\":\"%s\"}",
                     s_state.cfg.zone_id, act->action);
        }
        unlock_state();
        if (!send_control_json(body))
            manifest_ui_set_message("Action failed");
    }
}

void bridge_client_handle_input(ui_input_event_t event) {
  if (manifest_ui_is_zone_picker_visible()) {
    if (event == UI_INPUT_VOL_UP) {
      manifest_ui_zone_picker_scroll(1);
      return;
    }
    if (event == UI_INPUT_VOL_DOWN) {
      manifest_ui_zone_picker_scroll(-1);
      return;
    }
    if (event == UI_INPUT_PLAY_PAUSE) {
      // Get the selected zone ID directly from the picker
      char selected_id[MAX_ZONE_NAME] = {0};
      manifest_ui_zone_picker_get_selected_id(selected_id, sizeof(selected_id));
      LOGI("Zone picker: selected zone id '%s'", selected_id);

      // Check for Back (always a no-op, just closes picker)
      if (strcmp(selected_id, ZONE_ID_BACK) == 0) {
        LOGI("Zone picker: Back selected (no-op)");
        manifest_ui_hide_zone_picker();
        return;
      }

      // Check for Settings
      if (strcmp(selected_id, ZONE_ID_SETTINGS) == 0) {
        LOGI("Zone picker: Settings selected");
        manifest_ui_hide_zone_picker();
        ui_show_settings();
        return;
      }

      // Check if user selected the same zone they started with (no-op)
      if (manifest_ui_zone_picker_is_current_selection()) {
        LOGI("Zone picker: Same zone selected (no-op)");
        manifest_ui_hide_zone_picker();
        return;
      }

      // Zone selection
      char label_copy[MAX_ZONE_NAME] = {0};
      bool updated = false;
      lock_state();
      // Find the zone by ID to get its name
      for (int i = 0; i < s_state.zone_count; ++i) {
        struct zone_entry *entry = &s_state.zones[i];
        if (strcmp(entry->id, selected_id) == 0) {
          LOGI("Zone picker: switching to zone '%s' (id=%s)", entry->name,
               entry->id);
          strncpy(s_state.cfg.zone_id, entry->id,
                  sizeof(s_state.cfg.zone_id) - 1);
          s_state.cfg.zone_id[sizeof(s_state.cfg.zone_id) - 1] = '\0';
          strncpy(s_state.zone_label, entry->name,
                  sizeof(s_state.zone_label) - 1);
          s_state.zone_label[sizeof(s_state.zone_label) - 1] = '\0';
          strncpy(label_copy, s_state.zone_label, sizeof(label_copy) - 1);
          s_state.zone_resolved = true;
          if (s_device_state != DEVICE_STATE_OPERATIONAL) {
            LOGI("Device state: %s -> OPERATIONAL (zone selected)",
                 device_state_name(s_device_state));
            s_device_state = DEVICE_STATE_OPERATIONAL;
            manifest_ui_set_network_status(NULL); // Clear status banner when ready
          }
          s_trigger_poll = true;
          s_force_artwork_refresh = true; // Force artwork reload for new zone
          updated = true;
          break;
        }
      }
      if (!updated) {
        LOGW("Zone picker: zone id '%s' not found in zone list", selected_id);
      }
      unlock_state();
      // Hide picker FIRST to ensure it closes before any async operations
      manifest_ui_hide_zone_picker();
      if (updated) {
        platform_storage_save(&s_state.cfg);
        manifest_ui_set_zone_name(label_copy);
        manifest_ui_set_message("Loading zone...");
      }
      return;
    }
    if (event == UI_INPUT_MENU) {
      manifest_ui_hide_zone_picker();
      return;
    }
    return;
  }

  // v2 per-screen encoder dispatch — try this before hardcoded defaults
  {
    const manifest_encoder_t *enc = get_current_encoder();
    if (enc) {
      const manifest_action_t *act = NULL;
      switch (event) {
      case UI_INPUT_VOL_UP:
        act = &enc->cw;
        break;
      case UI_INPUT_VOL_DOWN:
        act = &enc->ccw;
        break;
      case UI_INPUT_PLAY_PAUSE:
        act = enc->has_press ? &enc->press : NULL;
        break;
      case UI_INPUT_MENU:
        act = enc->has_long_press ? &enc->long_press : NULL;
        break;
      default:
        break;
      }
      if (act && act->action[0]) {
        dispatch_action(act);
        return; // Handled via per-screen encoder — skip fallbacks
      }
    }
  }

  // v2 element-based button tap dispatch — if current screen has elements,
  // use the element's on_tap action instead of hardcoded action strings.
  {
    int elem_idx = manifest_ui_get_button_element_idx(event);
    if (elem_idx >= 0) {
      const manifest_element_t *elem = get_element_for_button(elem_idx);
      if (elem && elem->has_on_tap) {
        dispatch_action(&elem->on_tap);
        return; // Handled via v2 element on_tap — skip fallbacks
      }
    }
  }

  if (event == UI_INPUT_MENU) {
    manifest_ui_show_zone_picker();
    return;
  }

  // Config-driven input dispatch — if manifest has interactions, use them
  if (s_has_interactions) {
    // Map ui_input_event_t enum to string name
    const char *input_name = NULL;
    switch (event) {
    case UI_INPUT_VOL_UP:
      input_name = "encoder_cw";
      break;
    case UI_INPUT_VOL_DOWN:
      input_name = "encoder_ccw";
      break;
    case UI_INPUT_PLAY_PAUSE:
      input_name = "encoder_press";
      break;
    case UI_INPUT_MENU:
      input_name = "encoder_long_press";
      break;
    case UI_INPUT_PREV_TRACK:
      input_name = "button_prev";
      break;
    case UI_INPUT_NEXT_TRACK:
      input_name = "button_next";
      break;
    case UI_INPUT_MUTE:
      input_name = "button_mute";
      break;
    default:
      break;
    }

    if (input_name) {
      const char *action =
          manifest_lookup_interaction(&s_cached_interactions, input_name);
      if (action) {
        char body[256];
        if (strcmp(action, "volume_up") == 0) {
          // Volume up via UDP fast-path (same as hardcoded VOL_UP)
          lock_state();
          float predicted_up =
              s_last_known_volume + s_last_known_volume_step;
          if (predicted_up > s_last_known_volume_max) {
            predicted_up = s_last_known_volume_max;
          }
          s_last_known_volume = predicted_up;
          snprintf(
              body, sizeof(body),
              "{\"zone_id\":\"%s\",\"action\":\"vol_abs\",\"value\":%.10g}",
              s_state.cfg.zone_id, predicted_up);
          unlock_state();
          manifest_ui_show_volume_change(predicted_up,
                                         s_last_known_volume_step);
          if (!udp_send_volume(predicted_up)) {
            if (!send_control_json(body)) {
              manifest_ui_set_message("Volume change failed");
            }
          }
        } else if (strcmp(action, "volume_down") == 0) {
          // Volume down via UDP fast-path (same as hardcoded VOL_DOWN)
          lock_state();
          float predicted_down =
              s_last_known_volume - s_last_known_volume_step;
          if (predicted_down < s_last_known_volume_min) {
            predicted_down = s_last_known_volume_min;
          }
          s_last_known_volume = predicted_down;
          snprintf(
              body, sizeof(body),
              "{\"zone_id\":\"%s\",\"action\":\"vol_abs\",\"value\":%.10g}",
              s_state.cfg.zone_id, predicted_down);
          unlock_state();
          manifest_ui_show_volume_change(predicted_down,
                                         s_last_known_volume_step);
          if (!udp_send_volume(predicted_down)) {
            if (!send_control_json(body)) {
              manifest_ui_set_message("Volume change failed");
            }
          }
        } else {
          // For all other actions, send as JSON to bridge
          lock_state();
          snprintf(body, sizeof(body),
                   "{\"zone_id\":\"%s\",\"action\":\"%s\"}",
                   s_state.cfg.zone_id, action);
          unlock_state();
          if (!send_control_json(body)) {
            manifest_ui_set_message("Action failed");
          }
        }
        return; // Handled via interactions — skip hardcoded switch
      }
      // If input not found in interactions, fall through to hardcoded
    }
  }

  char body[256];
  switch (event) {
  case UI_INPUT_VOL_DOWN: {
    lock_state();
    float predicted_down = s_last_known_volume - s_last_known_volume_step;
    if (predicted_down < s_last_known_volume_min) {
      predicted_down = s_last_known_volume_min;
    }
    s_last_known_volume = predicted_down;
    snprintf(body, sizeof(body),
             "{\"zone_id\":\"%s\",\"action\":\"vol_abs\",\"value\":%.10g}",
             s_state.cfg.zone_id, predicted_down);
    unlock_state();
    manifest_ui_show_volume_change(predicted_down, s_last_known_volume_step);
    if (!udp_send_volume(predicted_down)) {
      if (!send_control_json(body)) {
        manifest_ui_set_message("Volume change failed");
      }
    }
    break;
  }
  case UI_INPUT_VOL_UP: {
    lock_state();
    float predicted_up = s_last_known_volume + s_last_known_volume_step;
    if (predicted_up > s_last_known_volume_max) {
      predicted_up = s_last_known_volume_max;
    }
    s_last_known_volume = predicted_up;
    snprintf(body, sizeof(body),
             "{\"zone_id\":\"%s\",\"action\":\"vol_abs\",\"value\":%.10g}",
             s_state.cfg.zone_id, predicted_up);
    unlock_state();
    manifest_ui_show_volume_change(predicted_up, s_last_known_volume_step);
    if (!udp_send_volume(predicted_up)) {
      if (!send_control_json(body)) {
        manifest_ui_set_message("Volume change failed");
      }
    }
    break;
  }
  case UI_INPUT_PLAY_PAUSE:
    lock_state();
    snprintf(body, sizeof(body),
             "{\"zone_id\":\"%s\",\"action\":\"play_pause\"}",
             s_state.cfg.zone_id);
    unlock_state();
    if (!send_control_json(body)) {
      manifest_ui_set_message("Play/pause failed");
    }
    break;
  case UI_INPUT_NEXT_TRACK:
    lock_state();
    snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"next\"}",
             s_state.cfg.zone_id);
    unlock_state();
    if (!send_control_json(body)) {
      manifest_ui_set_message("Next track failed");
    }
    break;
  case UI_INPUT_PREV_TRACK:
    lock_state();
    snprintf(body, sizeof(body), "{\"zone_id\":\"%s\",\"action\":\"prev\"}",
             s_state.cfg.zone_id);
    unlock_state();
    if (!send_control_json(body)) {
      manifest_ui_set_message("Previous track failed");
    }
    break;
  case UI_INPUT_MUTE:
    lock_state();
    snprintf(body, sizeof(body),
             "{\"zone_id\":\"%s\",\"action\":\"toggle_mute\"}",
             s_state.cfg.zone_id);
    unlock_state();
    if (!send_control_json(body)) {
      manifest_ui_set_message("Mute toggle failed");
    }
    break;
  default:
    break;
  }
}

// Velocity-sensitive volume rotation handler
// Maps encoder tick count over 50ms window to step size:
//   1 tick = slow (step 1)
//   2 ticks = medium (step 3)
//   3+ ticks = fast (step 5)
void bridge_client_handle_volume_rotation(int ticks) {
  if (ticks == 0)
    return;

  // Block volume changes until device is fully operational (WiFi + zones
  // loaded)
  lock_state();
  bool is_operational = (s_device_state == DEVICE_STATE_OPERATIONAL);
  unlock_state();

  if (!is_operational) {
    manifest_ui_set_message("Connecting...");
    return;
  }

  // Determine velocity multiplier (applied to zone's step)
  int abs_ticks = ticks < 0 ? -ticks : ticks;
  int step_multiplier;
  if (abs_ticks >= 3) {
    step_multiplier = 5; // Fast rotation
  } else if (abs_ticks >= 2) {
    step_multiplier = 3; // Medium rotation
  } else {
    step_multiplier = 1; // Slow rotation (fine-grained control)
  }

  // Calculate optimistic new volume with clamping (inside lock for
  // consistency)
  lock_state();
  float delta = step_multiplier * s_last_known_volume_step;
  float predicted_vol = s_last_known_volume + (ticks > 0 ? delta : -delta);
  if (predicted_vol < s_last_known_volume_min) {
    predicted_vol = s_last_known_volume_min;
  }
  if (predicted_vol > s_last_known_volume_max) {
    predicted_vol = s_last_known_volume_max;
  }

  // Update cached volume immediately for next rotation (optimistic tracking)
  s_last_known_volume = predicted_vol;

  // Send volume request to Roon (absolute value for exact match with
  // optimistic UI)
  char body[256];
  snprintf(body, sizeof(body),
           "{\"zone_id\":\"%s\",\"action\":\"vol_abs\",\"value\":%.1f}",
           s_state.cfg.zone_id, predicted_vol);
  unlock_state();

  // Show volume overlay immediately with predicted value (optimistic UI)
  manifest_ui_show_volume_change(predicted_vol, s_last_known_volume_step);

  if (!udp_send_volume(predicted_vol)) {
    if (!send_control_json(body)) {
      manifest_ui_set_message("Volume change failed");
    }
  }
}

void bridge_client_set_network_ready(bool ready) {
  s_network_ready = ready;

  lock_state();
  if (ready) {
    // Clear auto-discovered bridge on network reconnect to force fresh
    // discovery. This enables seamless location switching — if the knob
    // connects to a different WiFi, it finds the local bridge instead of
    // trying the stale one. Manually configured bridges are kept.
    if (s_state.cfg.bridge_from_mdns) {
      LOGI("Clearing mDNS-discovered bridge for fresh discovery");
      s_state.cfg.bridge_base[0] = '\0';
      s_state.cfg.bridge_from_mdns = 0;
      s_state.zone_resolved = false;  // Force zone re-discovery
      s_bridge_verified = false;      // Allow immediate mDNS/UDP
    }
    if (s_device_state == DEVICE_STATE_OPERATIONAL) {
      // Already operational — don't regress to CONNECTED on transient
      // WiFi reconnect. Zones are already loaded.
      LOGI("Device state: OPERATIONAL (network ready, no state change)");
    } else {
      LOGI("Device state: %s -> CONNECTED (network ready)",
           device_state_name(s_device_state));
      s_device_state = DEVICE_STATE_CONNECTED;
      manifest_ui_set_network_status("Loading zones...");
    }
    s_trigger_poll = true; // Trigger immediate poll when network becomes ready
  } else {
    // Transition to RECONNECTING if we were operational, otherwise back to
    // BOOT
    device_state_t new_state = (s_device_state == DEVICE_STATE_OPERATIONAL)
                                   ? DEVICE_STATE_RECONNECTING
                                   : DEVICE_STATE_BOOT;
    LOGI("Device state: %s -> %s (network lost)",
         device_state_name(s_device_state), device_state_name(new_state));
    s_device_state = new_state;
    // Only set banner for RECONNECTING (was operational, lost network).
    // During BOOT, the WiFi event handler owns the banner (AP setup, etc).
    if (new_state == DEVICE_STATE_RECONNECTING) {
      manifest_ui_set_network_status("Reconnecting...");
    }
  }
  unlock_state();
}

const char *bridge_client_get_artwork_url(char *url_buf, size_t buf_len,
                                          int width, int height,
                                          int clip_radius) {
  if (!url_buf || buf_len < 256) {
    return NULL;
  }
  lock_state();
  const char *bridge_base = s_state.cfg.bridge_base;
  const char *zone_id = s_state.cfg.zone_id;
  if (!bridge_base || !bridge_base[0] || !zone_id || !zone_id[0]) {
    unlock_state();
    return NULL;
  }
  if (clip_radius > 0) {
    snprintf(url_buf, buf_len,
             "%s/now_playing/"
             "image?zone_id=%s&scale=fit&width=%d&height=%d&format=rgb565&clip_"
             "radius=%d",
             bridge_base, zone_id, width, height, clip_radius);
  } else {
    snprintf(url_buf, buf_len,
             "%s/now_playing/"
             "image?zone_id=%s&scale=fit&width=%d&height=%d&format=rgb565",
             bridge_base, zone_id, width, height);
  }
  unlock_state();
  return url_buf;
}

bool bridge_client_is_ready_for_art_mode(void) {
  lock_state();
  bool ready = s_state.zone_count > 0;
  unlock_state();
  return ready;
}

// Bridge retry tracking functions
static void reset_bridge_fail_count(void) { s_bridge_fail_count = 0; }

static void increment_bridge_fail_count(void) {
  if (s_bridge_fail_count < BRIDGE_FAIL_THRESHOLD) {
    s_bridge_fail_count++;
  }
}

void bridge_client_set_device_ip(const char *ip) {
  if (ip && ip[0]) {
    strncpy(s_device_ip, ip, sizeof(s_device_ip) - 1);
    s_device_ip[sizeof(s_device_ip) - 1] = '\0';
  } else {
    s_device_ip[0] = '\0';
  }
}

int bridge_client_get_bridge_retry_count(void) { return s_bridge_fail_count; }

int bridge_client_get_bridge_retry_max(void) { return BRIDGE_FAIL_THRESHOLD; }

bool bridge_client_get_bridge_url(char *buf, size_t len) {
  if (!buf || len == 0) {
    return false;
  }
  lock_state();
  bool has_bridge = (s_state.cfg.bridge_base[0] != '\0');
  if (has_bridge) {
    strncpy(buf, s_state.cfg.bridge_base, len - 1);
    buf[len - 1] = '\0';
  } else {
    buf[0] = '\0';
  }
  unlock_state();
  return has_bridge;
}

bool bridge_client_is_bridge_connected(void) { return s_last_net_ok; }

bool bridge_client_is_bridge_mdns(void) {
  lock_state();
  bool from_mdns = (s_state.cfg.bridge_from_mdns != 0);
  unlock_state();
  return from_mdns;
}

// Config fetch and apply implementation

// Data passed to UI thread for config application
struct apply_config_ui_data {
  uint16_t rotation;
  bool is_charging;
  rk_cfg_t cfg; // Copy of config for timeout updates
};

// Called on UI thread to apply config safely
static void apply_config_on_ui_thread(void *arg) {
  struct apply_config_ui_data *data = (struct apply_config_ui_data *)arg;
  if (!data)
    return;

  platform_display_set_rotation(data->rotation);

#ifdef ESP_PLATFORM
  display_update_timeouts(&data->cfg, data->is_charging);
  display_update_power_settings(&data->cfg);
#endif

  LOGI("Config applied on UI thread: rotation=%d", data->rotation);
  free(data);
}

static void apply_knob_config(const rk_cfg_t *cfg) {
  if (!cfg) {
    return;
  }

  // Get current charging state
  bool is_charging = platform_battery_is_charging();
  uint16_t rotation = rk_cfg_get_rotation(cfg, is_charging);

  LOGI("Config apply requested: name='%s' rotation=%d (charging=%s)",
       cfg->knob_name[0] ? cfg->knob_name : "(unnamed)", rotation,
       is_charging ? "yes" : "no");

  // Post to UI thread since LVGL is not thread-safe
  struct apply_config_ui_data *data = malloc(sizeof(*data));
  if (data) {
    data->rotation = rotation;
    data->is_charging = is_charging;
    data->cfg = *cfg;
    platform_task_post_to_ui(apply_config_on_ui_thread, data);
  }
}


static bool fetch_knob_config(void) {
  lock_state();
  char bridge_base[sizeof(s_state.cfg.bridge_base)];
  strncpy(bridge_base, s_state.cfg.bridge_base, sizeof(bridge_base) - 1);
  bridge_base[sizeof(bridge_base) - 1] = '\0';
  unlock_state();

  if (bridge_base[0] == '\0') {
    LOGW("fetch_knob_config: No bridge configured");
    return false;
  }

  // Get knob ID
  char knob_id[16];
  platform_http_get_knob_id(knob_id, sizeof(knob_id));

  // Build URL
  char url[256];
  snprintf(url, sizeof(url), "%s/config/%s", bridge_base, knob_id);

  LOGI("Fetching config from %s", url);

  char *resp = NULL;
  size_t resp_len = 0;
  if (platform_http_get(url, &resp, &resp_len) != 0 || !resp) {
    LOGW("fetch_knob_config: HTTP request failed");
    platform_http_free(resp);
    return false;
  }

  // Parse JSON response using cJSON
  cJSON *root = cJSON_Parse(resp);
  platform_http_free(resp);

  if (!root) {
    LOGW("fetch_knob_config: JSON parse failed");
    return false;
  }

  // Extract config fields
  lock_state();
  rk_cfg_t *cfg = &s_state.cfg;

  // config_sha (at root level)
  cJSON *sha = cJSON_GetObjectItem(root, "config_sha");
  if (cJSON_IsString(sha) && sha->valuestring) {
    strncpy(cfg->config_sha, sha->valuestring, sizeof(cfg->config_sha) - 1);
    cfg->config_sha[sizeof(cfg->config_sha) - 1] = '\0';
  }

  // Get nested config object - all config fields are inside "config"
  cJSON *config_obj = cJSON_GetObjectItem(root, "config");
  if (!cJSON_IsObject(config_obj)) {
    LOGW("fetch_knob_config: missing 'config' object in response");
    unlock_state();
    cJSON_Delete(root);
    return false;
  }

  // name (inside config object)
  cJSON *name = cJSON_GetObjectItem(config_obj, "name");
  if (cJSON_IsString(name) && name->valuestring) {
    strncpy(cfg->knob_name, name->valuestring, sizeof(cfg->knob_name) - 1);
    cfg->knob_name[sizeof(cfg->knob_name) - 1] = '\0';
  }

  // rotation
  cJSON *rot_charging = cJSON_GetObjectItem(config_obj, "rotation_charging");
  if (cJSON_IsNumber(rot_charging)) {
    cfg->rotation_charging = (uint16_t)rot_charging->valueint;
  }
  cJSON *rot_battery = cJSON_GetObjectItem(config_obj, "rotation_not_charging");
  if (cJSON_IsNumber(rot_battery)) {
    cfg->rotation_not_charging = (uint16_t)rot_battery->valueint;
  }

  // art_mode_charging
  cJSON *art_charging = cJSON_GetObjectItem(config_obj, "art_mode_charging");
  if (cJSON_IsObject(art_charging)) {
    cJSON *enabled = cJSON_GetObjectItem(art_charging, "enabled");
    if (cJSON_IsBool(enabled)) {
      cfg->art_mode_charging_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
    }
    cJSON *timeout = cJSON_GetObjectItem(art_charging, "timeout_sec");
    if (cJSON_IsNumber(timeout)) {
      cfg->art_mode_charging_timeout_sec = (uint16_t)timeout->valueint;
    }
  }

  // art_mode_battery
  cJSON *art_battery = cJSON_GetObjectItem(config_obj, "art_mode_battery");
  if (cJSON_IsObject(art_battery)) {
    cJSON *enabled = cJSON_GetObjectItem(art_battery, "enabled");
    if (cJSON_IsBool(enabled)) {
      cfg->art_mode_battery_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
    }
    cJSON *timeout = cJSON_GetObjectItem(art_battery, "timeout_sec");
    if (cJSON_IsNumber(timeout)) {
      cfg->art_mode_battery_timeout_sec = (uint16_t)timeout->valueint;
    }
  }

  // dim_charging
  cJSON *dim_charging = cJSON_GetObjectItem(config_obj, "dim_charging");
  if (cJSON_IsObject(dim_charging)) {
    cJSON *enabled = cJSON_GetObjectItem(dim_charging, "enabled");
    if (cJSON_IsBool(enabled)) {
      cfg->dim_charging_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
    }
    cJSON *timeout = cJSON_GetObjectItem(dim_charging, "timeout_sec");
    if (cJSON_IsNumber(timeout)) {
      cfg->dim_charging_timeout_sec = (uint16_t)timeout->valueint;
    }
  }

  // dim_battery
  cJSON *dim_battery = cJSON_GetObjectItem(config_obj, "dim_battery");
  if (cJSON_IsObject(dim_battery)) {
    cJSON *enabled = cJSON_GetObjectItem(dim_battery, "enabled");
    if (cJSON_IsBool(enabled)) {
      cfg->dim_battery_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
    }
    cJSON *timeout = cJSON_GetObjectItem(dim_battery, "timeout_sec");
    if (cJSON_IsNumber(timeout)) {
      cfg->dim_battery_timeout_sec = (uint16_t)timeout->valueint;
    }
  }

  // sleep_charging
  cJSON *sleep_charging = cJSON_GetObjectItem(config_obj, "sleep_charging");
  if (cJSON_IsObject(sleep_charging)) {
    cJSON *enabled = cJSON_GetObjectItem(sleep_charging, "enabled");
    if (cJSON_IsBool(enabled)) {
      cfg->sleep_charging_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
    }
    cJSON *timeout = cJSON_GetObjectItem(sleep_charging, "timeout_sec");
    if (cJSON_IsNumber(timeout)) {
      cfg->sleep_charging_timeout_sec = (uint16_t)timeout->valueint;
    }
  }

  // sleep_battery
  cJSON *sleep_battery = cJSON_GetObjectItem(config_obj, "sleep_battery");
  if (cJSON_IsObject(sleep_battery)) {
    cJSON *enabled = cJSON_GetObjectItem(sleep_battery, "enabled");
    if (cJSON_IsBool(enabled)) {
      cfg->sleep_battery_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
    }
    cJSON *timeout = cJSON_GetObjectItem(sleep_battery, "timeout_sec");
    if (cJSON_IsNumber(timeout)) {
      cfg->sleep_battery_timeout_sec = (uint16_t)timeout->valueint;
    }
  }

  // deep_sleep_charging
  cJSON *deep_sleep_charging =
      cJSON_GetObjectItem(config_obj, "deep_sleep_charging");
  if (cJSON_IsObject(deep_sleep_charging)) {
    cJSON *enabled = cJSON_GetObjectItem(deep_sleep_charging, "enabled");
    if (cJSON_IsBool(enabled)) {
      cfg->deep_sleep_charging_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
    }
    cJSON *timeout = cJSON_GetObjectItem(deep_sleep_charging, "timeout_sec");
    if (cJSON_IsNumber(timeout)) {
      cfg->deep_sleep_charging_timeout_sec = (uint16_t)timeout->valueint;
    }
  }

  // deep_sleep_battery
  cJSON *deep_sleep_battery =
      cJSON_GetObjectItem(config_obj, "deep_sleep_battery");
  if (cJSON_IsObject(deep_sleep_battery)) {
    cJSON *enabled = cJSON_GetObjectItem(deep_sleep_battery, "enabled");
    if (cJSON_IsBool(enabled)) {
      cfg->deep_sleep_battery_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
    }
    cJSON *timeout = cJSON_GetObjectItem(deep_sleep_battery, "timeout_sec");
    if (cJSON_IsNumber(timeout)) {
      cfg->deep_sleep_battery_timeout_sec = (uint16_t)timeout->valueint;
    }
  }

  // Power management settings
  cJSON *wifi_ps = cJSON_GetObjectItem(config_obj, "wifi_power_save_enabled");
  if (cJSON_IsBool(wifi_ps)) {
    cfg->wifi_power_save_enabled = cJSON_IsTrue(wifi_ps) ? 1 : 0;
  }
  cJSON *cpu_freq = cJSON_GetObjectItem(config_obj, "cpu_freq_scaling_enabled");
  if (cJSON_IsBool(cpu_freq)) {
    cfg->cpu_freq_scaling_enabled = cJSON_IsTrue(cpu_freq) ? 1 : 0;
  }
  cJSON *sleep_poll = cJSON_GetObjectItem(config_obj, "sleep_poll_stopped_sec");
  if (cJSON_IsNumber(sleep_poll)) {
    cfg->sleep_poll_stopped_sec = (uint16_t)sleep_poll->valueint;
  }

  // Log parsed config values
  LOGI("Config parsed: rot=%d/%d art=%d/%ds|%d/%ds dim=%d/%ds|%d/%ds "
       "sleep=%d/%ds|%d/%ds deep=%d/%ds|%d/%ds",
       cfg->rotation_charging, cfg->rotation_not_charging,
       cfg->art_mode_charging_enabled, cfg->art_mode_charging_timeout_sec,
       cfg->art_mode_battery_enabled, cfg->art_mode_battery_timeout_sec,
       cfg->dim_charging_enabled, cfg->dim_charging_timeout_sec,
       cfg->dim_battery_enabled, cfg->dim_battery_timeout_sec,
       cfg->sleep_charging_enabled, cfg->sleep_charging_timeout_sec,
       cfg->sleep_battery_enabled, cfg->sleep_battery_timeout_sec,
       cfg->deep_sleep_charging_enabled, cfg->deep_sleep_charging_timeout_sec,
       cfg->deep_sleep_battery_enabled, cfg->deep_sleep_battery_timeout_sec);
  LOGI("Power config: wifi_ps=%d cpu_scale=%d sleep_poll_stopped=%ds",
       cfg->wifi_power_save_enabled, cfg->cpu_freq_scaling_enabled,
       cfg->sleep_poll_stopped_sec);

  // Make a copy for apply and save
  rk_cfg_t cfg_copy = *cfg;
  unlock_state();

  cJSON_Delete(root);

  // Save to NVS
  platform_storage_save(&cfg_copy);

  // Apply the config
  apply_knob_config(&cfg_copy);

  LOGI("Config fetch complete: sha='%s'", cfg_copy.config_sha);
  return true;
}

// Check for charging state changes and reapply config if needed
static void check_charging_state_change(void) {
  bool current_charging = platform_battery_is_charging();
  if (current_charging != s_last_charging_state) {
    LOGI("Charging state changed: %s -> %s",
         s_last_charging_state ? "charging" : "battery",
         current_charging ? "charging" : "battery");
    s_last_charging_state = current_charging;

    // Update battery indicator immediately (thread-safe post to UI task)
    post_ui_battery_update();

    // Reapply config with new charging state
    lock_state();
    rk_cfg_t cfg_copy = s_state.cfg;
    unlock_state();
    apply_knob_config(&cfg_copy);
  }
}
