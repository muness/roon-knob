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

// ── Config-driven input/control limits ──────────────────────────────────────
#define MAX_INTERACTIONS 12
#define MAX_CONTROLS 6
#define MAX_ACTION_LEN 32
#define MAX_INPUT_LEN 32

// ── Command-pattern limits (schema v2) ──────────────────────────────────────
#define MAX_ELEMENTS 6
#define MAX_PARAMS_JSON 128
#define MAX_ICON_LEN 32
#define MAX_LABEL_LEN 64

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

// ── Interaction mapping (config-driven inputs) ──────────────────────────────

/// Maps a physical input (e.g. "encoder_cw") to a logical action (e.g. "volume_up").
typedef struct {
    char input[MAX_INPUT_LEN];
    char action[MAX_ACTION_LEN];
} interaction_mapping_t;

/// Collection of input-to-action mappings from the manifest.
typedef struct {
    interaction_mapping_t mappings[MAX_INTERACTIONS];
    int count;
} interactions_t;

// ── Command-pattern types (schema v2) ────────────────────────────────────────

/// An action triggered by user interaction.
typedef struct {
    char action[MAX_ACTION_LEN];       // e.g., "toggle_playback", "previous"
    bool has_params;
    char params_json[MAX_PARAMS_JSON]; // Raw JSON string for bridge to interpret
} manifest_action_t;

/// Display properties for a command-pattern element.
typedef struct {
    char icon[MAX_ICON_LEN];           // Material Symbols icon name
    char label[MAX_LABEL_LEN];         // Text label (alternative to icon)
    bool active;                       // Visual active state
} manifest_display_t;

/// A command-pattern element — self-contained {display, behavior} unit.
typedef struct {
    manifest_display_t display;
    bool has_on_tap;
    manifest_action_t on_tap;
    bool has_on_long_press;
    manifest_action_t on_long_press;
} manifest_element_t;

/// Per-screen encoder configuration.
typedef struct {
    manifest_action_t cw;              // Clockwise rotation
    manifest_action_t ccw;             // Counter-clockwise rotation
    bool has_press;
    manifest_action_t press;           // Encoder press
    bool has_long_press;
    manifest_action_t long_press;      // Encoder long press
} manifest_encoder_t;

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
  // v1 backward compat: Config-driven controls (e.g. "prev", "play", "next", "mute")
  char controls[MAX_CONTROLS][MAX_ACTION_LEN];
  int controls_count; // 0 = show all defaults
  // v2 command-pattern elements
  manifest_element_t elements[MAX_ELEMENTS];
  int element_count;  // 0 = fall back to controls[]
  // v2 per-screen encoder config
  bool has_encoder;
  manifest_encoder_t encoder;
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
  interactions_t interactions;  // Input-to-action mappings
  bool has_interactions;        // false = use hardcoded defaults
} manifest_t;

/// Look up an action for a given input name in the interactions table.
/// Returns the action string, or NULL if not found.
const char *manifest_lookup_interaction(const interactions_t *interactions,
                                         const char *input_name);

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
