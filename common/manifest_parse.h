#pragma once

/// Manifest-driven knob protocol — JSON parser.
///
/// Parses the bridge's /knob/manifest response into C structs.
/// The fast/slow split means the knob only re-parses screens when
/// the SHA changes — fast fields (volume, seek) are read every cycle.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Limits ──────────────────────────────────────────────────────────────────

#define MANIFEST_MAX_SCREENS 4
#define MANIFEST_MAX_LINES 4
#define MANIFEST_MAX_LIST_ITEMS 16
#define MANIFEST_MAX_TEXT 128
#define MANIFEST_MAX_ID 32
#define MANIFEST_MAX_URL 256
#define MANIFEST_SHA_LEN 9 // 8 hex chars + null

// ── Screen types ────────────────────────────────────────────────────────────

typedef enum {
  SCREEN_TYPE_MEDIA = 0,
  SCREEN_TYPE_LIST,
  SCREEN_TYPE_CARD,
  SCREEN_TYPE_PROGRESS,
  SCREEN_TYPE_STATUS,
  SCREEN_TYPE_UNKNOWN
} screen_type_t;

// ── Text line styles ────────────────────────────────────────────────────────

typedef enum {
  TEXT_STYLE_TITLE = 0, // Large, white
  TEXT_STYLE_SUBTITLE,  // Medium, grey
  TEXT_STYLE_DETAIL     // Small, dim
} text_style_t;

// ── Data types ──────────────────────────────────────────────────────────────

typedef struct {
  char text[MANIFEST_MAX_TEXT];
  text_style_t style;
} manifest_text_line_t;

typedef struct {
  char id[MANIFEST_MAX_ID];
  char label[MANIFEST_MAX_TEXT];
  char sublabel[MANIFEST_MAX_TEXT];
  bool selected;
} manifest_list_item_t;

/// Transport permissions
typedef struct {
  bool play;
  bool pause;
  bool next;
  bool prev;
} manifest_transport_t;

/// Fast-path state — read every poll cycle.
typedef struct {
  char zone_id[64];
  bool is_playing;
  float volume;
  float volume_min;
  float volume_max;
  float volume_step;
  char volume_type[16]; // "db", "number", "fixed"
  int seek_position;
  int length;
  manifest_transport_t transport;
} manifest_fast_t;

/// Media screen data (now_playing equivalent).
typedef struct {
  char image_url[MANIFEST_MAX_URL];
  char image_key[MANIFEST_MAX_TEXT];
  manifest_text_line_t lines[MANIFEST_MAX_LINES];
  int line_count;
  char bg_color[8];   // "#rrggbb" hex string from bridge
} manifest_media_t;

/// List screen data (zone picker equivalent).
typedef struct {
  char title[MANIFEST_MAX_TEXT];
  manifest_list_item_t items[MANIFEST_MAX_LIST_ITEMS];
  int item_count;
} manifest_list_t;

/// Card screen data (Memex context).
typedef struct {
  manifest_text_line_t lines[MANIFEST_MAX_LINES];
  int line_count;
} manifest_card_t;

/// Progress screen data (OTA).
typedef struct {
  char label[MANIFEST_MAX_TEXT];
  float progress; // 0.0 to 1.0
} manifest_progress_t;

/// Status screen data (errors, network).
typedef struct {
  char message[MANIFEST_MAX_TEXT];
  char icon[32]; // Material Symbols name
} manifest_status_t;

/// A single screen in the manifest.
typedef struct {
  char id[MANIFEST_MAX_ID];
  screen_type_t type;
  union {
    manifest_media_t media;
    manifest_list_t list;
    manifest_card_t card;
    manifest_progress_t progress;
    manifest_status_t status;
  } data;
} manifest_screen_t;

/// Navigation configuration.
typedef struct {
  char order[MANIFEST_MAX_SCREENS][MANIFEST_MAX_ID];
  int count;
  char default_screen[MANIFEST_MAX_ID];
} manifest_nav_t;

/// Complete parsed manifest.
typedef struct {
  uint32_t version;
  char sha[MANIFEST_SHA_LEN];
  manifest_fast_t fast;
  manifest_screen_t screens[MANIFEST_MAX_SCREENS];
  int screen_count;
  manifest_nav_t nav;
} manifest_t;

// ── API ─────────────────────────────────────────────────────────────────────

/// Parse a manifest JSON response into the manifest struct.
/// Returns true on success, false on parse error.
/// On failure, the manifest struct is zeroed.
bool manifest_parse(const char *json, size_t json_len, manifest_t *out);

/// Parse only the fast state from a manifest JSON response.
/// JSON parsing cost is identical to manifest_parse; only C-struct population
/// is cheaper (skips screen and nav parsing). Use when SHA hasn't changed.
bool manifest_parse_fast(const char *json, size_t json_len,
                         manifest_fast_t *out);

/// Parse only the SHA from a manifest JSON response.
/// Cheapest operation — just string extraction.
bool manifest_parse_sha(const char *json, size_t json_len, char *sha_out,
                        size_t sha_len);

#ifdef __cplusplus
}
#endif
