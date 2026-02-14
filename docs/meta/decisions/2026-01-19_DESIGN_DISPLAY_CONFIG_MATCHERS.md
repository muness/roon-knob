# Design: Display Configuration via Matchers

**Status:** Draft
**Date:** 2026-01-19
**Context:** Users want different display layouts based on source type and personal preference

## Problem Statement

Different users have different needs for the knob display:

1. **Volume text redundancy**: Some users don't want volume shown as text because the arc already communicates it. Others want it front and center. This is a genuine preference split.

2. **Source-dependent metadata**: Different sources put different things on line 1 vs line 2. "Artist" might be line 1 for Tidal but "Station Name" for radio. The "right" visual hierarchy depends on what's actually displayed.

Current implementation has hardcoded display layout in `ui.c` with no way to adapt to source type or user preference.

## Proposed Solution: Data-Driven Matchers

A Hickey/Clojure-style pattern matching system where:
- **Matchers** are pure data describing conditions
- **Configs** are pure data describing display settings
- The **bridge** evaluates matchers against current playback state
- The **device** receives resolved config and renders accordingly

### Why This Approach

- **Data, not code**: Matchers are declarative, not imperative rules
- **Bridge has context**: Full access to Roon/LMS metadata, zone info, user settings
- **Device stays simple**: Just renders what it's told
- **Updatable without OTA**: New matchers can be added by updating bridge config

## Matcher Structure

### Match Predicates (Inputs)

The bridge knows:
- Source type (radio, streaming, local-file, etc.)
- Metadata presence and values (artist, album, station, program)
- Zone information
- Playback state

Example matcher:
```clojure
{:when {:source-type :radio}
 :display {...}}
```

More specific:
```clojure
{:when {:source-type :radio
        :station-name "WNYC"}
 :display {...}}
```

### Config Outputs (What Matchers Produce)

Each UI element can be configured with visibility, appearance, and behavior.

#### Visibility Modes

| Mode | Behavior |
|------|----------|
| `always` | Element always visible |
| `never` | Element always hidden |
| `on_change` | Visible during relevant event, fades after timeout |

#### What Triggers `on_change` for Each Element

| Element | Trigger Event |
|---------|---------------|
| `volume_text` | Volume adjustment (knob turn) |
| `volume_arc` | Volume adjustment |
| `line1` | Track change |
| `line2` | Track change |
| `zone` | Zone change or tap |
| `progress_arc` | (Not useful - always changing) |

#### Configurable Properties Per Element

**Text elements** (volume_text, line1, line2, zone):

| Property | Values | Notes |
|----------|--------|-------|
| `visibility` | `always` / `never` / `on_change` | When to show |
| `size` | `small` / `medium` / `large` | 22px, 25px, or 28px |
| `family` | `lato` / `notosans` | Font family |
| `align` | `center` / `left` / `right` | Text alignment |
| `color` | hex color | e.g., `#fafafa` |
| `fade_timeout_ms` | milliseconds | For `on_change` mode (0 = default) |

**Arc elements** (volume_arc, progress_arc):

| Property | Values | Notes |
|----------|--------|-------|
| `visibility` | `always` / `never` / `on_change` | When to show |
| `color` | hex color | Indicator color |
| `width` | pixels | Arc/ring thickness (0 = default 6px) |
| `fade_timeout_ms` | milliseconds | For `on_change` mode |

**Button elements** (prev_button, play_button, next_button):

| Property | Values | Notes |
|----------|--------|-------|
| `visibility` | `always` / `never` | When to show (on_change not supported) |
| `icon_color` | hex color | Icon/glyph color (e.g., `#fafafa`) |
| `bg_color` | hex color | Button background color |
| `border_color` | hex color | Button border color |
| `icon_size` | `normal` / `large` | 44px or 60px Material Icons |

**Metadata routing** (bridge resolves these before sending):

| Property | Values | Notes |
|----------|--------|-------|
| `line1_source` | metadata field name | What to display on line 1 |
| `line2_source` | metadata field name | What to display on line 2 |

### Composition

- First matching rule wins (ordered list)
- Default matcher at end catches everything
- User overrides could trump automatic matchers (future)

## Worked Examples

### Example 1: Radio - Minimal Display

User says: *"When listening to radio, show station name big, program small, hide volume text. Show track info briefly on change then fade."*

Context from bridge:
```clojure
{:source-type :radio
 :station "WNYC"
 :program "All Things Considered"
 :artist nil
 :track nil}
```

Matcher:
```clojure
{:when {:source-type :radio}
 :display {:volume_text {:visibility :never}
           :line1 {:from :station :size :large :visibility :on_change :fade_timeout_ms 5000}
           :line2 {:from :program :size :small :visibility :on_change :fade_timeout_ms 5000}
           :volume_arc {:visibility :on_change :fade_timeout_ms 3000}
           :progress_arc {:visibility :never}}}
```

Device receives (JSON):
```json
{
  "line1": "WNYC",
  "line2": "All Things Considered",
  "volume": 45,
  "display_config": {
    "volume_text": {"visibility": "never"},
    "line1": {"visibility": "on_change", "size": "large", "color": "#fafafa", "fade_timeout_ms": 5000},
    "line2": {"visibility": "on_change", "size": "small", "color": "#aaaaaa", "fade_timeout_ms": 5000},
    "volume_arc": {"visibility": "on_change", "color": "#5a9fd4", "fade_timeout_ms": 3000},
    "progress_arc": {"visibility": "never"}
  }
}
```

### Example 2: Streaming - Full Info

User says: *"For Tidal/Qobuz, always show artist small on top, track big below, volume on change only."*

Context from bridge:
```clojure
{:source-type :streaming
 :service :tidal
 :artist "Radiohead"
 :track "Everything In Its Right Place"
 :album "Kid A"}
```

Matcher:
```clojure
{:when {:source-type #{:tidal :qobuz :streaming}}
 :display {:volume_text {:visibility :on_change :fade_timeout_ms 3000}
           :line1 {:from :artist :size :small :visibility :always}
           :line2 {:from :track :size :large :visibility :always}
           :volume_arc {:visibility :always}
           :progress_arc {:visibility :always}}}
```

Device receives (JSON):
```json
{
  "line1": "Radiohead",
  "line2": "Everything In Its Right Place",
  "volume": 45,
  "display_config": {
    "volume_text": {"visibility": "on_change", "size": "large", "color": "#fafafa", "fade_timeout_ms": 3000},
    "line1": {"visibility": "always", "size": "small", "color": "#aaaaaa"},
    "line2": {"visibility": "always", "size": "large", "color": "#fafafa"},
    "volume_arc": {"visibility": "always", "color": "#5a9fd4"},
    "progress_arc": {"visibility": "always", "color": "#7bb9e8"}
  }
}
```

### Example 3: Default

Catch-all matching current behavior:
```clojure
{:when :default
 :display {:volume_text {:visibility :always :size :large :color "#fafafa"}
           :line1 {:from :line1 :visibility :always :size :small :color "#aaaaaa"}
           :line2 {:from :line2 :visibility :always :size :large :color "#fafafa"}
           :zone {:visibility :always :size :small :color "#bbbbbb"}
           :volume_arc {:visibility :always :color "#5a9fd4"}
           :progress_arc {:visibility :always :color "#7bb9e8"}}}
```

## Implementation Layers

### 1. Bridge (Rust)

- Maintains ordered list of matchers (config file or embedded)
- On playback change, evaluates matchers against current state
- Sends resolved display config alongside existing now_playing data
- Future: ChatGPT/natural language → matcher translation for user config

### 2. Protocol Extension

Current `/now_playing` response:
```json
{
  "line1": "Artist Name",
  "line2": "Track Title",
  "volume": 45,
  ...
}
```

Extended response:
```json
{
  "line1": "Artist Name",
  "line2": "Track Title",
  "volume": 45,
  "display_config": {
    "volume_text": {"visibility": "always", "size": "large", "color": "#fafafa"},
    "line1": {"visibility": "always", "size": "small", "color": "#aaaaaa"},
    "line2": {"visibility": "always", "size": "large", "color": "#fafafa"},
    "prev_button": {"visibility": "always", "icon_color": "#fafafa", "bg_color": "#1a1a1a", "border_color": "#4a4a4a", "icon_size": "normal"},
    "play_button": {"visibility": "always", "icon_color": "#fafafa", "bg_color": "#2c2c2c", "border_color": "#5a9fd4", "icon_size": "large"},
    "next_button": {"visibility": "always", "icon_color": "#fafafa", "bg_color": "#1a1a1a", "border_color": "#4a4a4a", "icon_size": "normal"}
  }
}
```

Backward compatible: device ignores unknown fields if bridge doesn't send config.

### 3. Device Firmware (C/LVGL)

Changes needed:
- Parse `display_config` from JSON response
- Apply font size dynamically: `font_small()` vs `font_normal()`
- Show/hide volume label based on `volume_text`
- Default to current behavior if no config received

## Device Implementation Details

### Current LVGL Layout

From `ui.c`, the "Now Playing" group uses flex layout:
- `s_volume_label_large` - 28px (`font_normal()`), white (0xfafafa)
- `s_artist_label` - 22px (`font_small()`), grey (0xaaaaaa) - currently line2
- `s_track_label` - 28px (`font_normal()`), white (0xfafafa) - currently line1
- `s_volume_arc` - blue (0x5a9fd4) indicator
- `s_progress_arc` - lighter blue (0x7bb9e8) indicator
- `s_zone_label` - 22px (`font_small()`), light grey (0xbbbbbb)

The layout uses `LV_LAYOUT_FLEX` with `LV_FLEX_FLOW_COLUMN`, so hiding elements automatically reflows.

**Note:** Existing code has `s_volume_emphasis_timer` for volume fade - implementation should integrate with this rather than duplicate.

### Config Pack Data Structures

```c
// Visibility modes
typedef enum {
    VIS_ALWAYS = 0,     // Always visible
    VIS_NEVER = 1,      // Always hidden
    VIS_ON_CHANGE = 2,  // Show on event, fade after timeout
} visibility_mode_t;

// Font sizes (three available)
typedef enum {
    FONT_SMALL = 0,     // 22px
    FONT_MEDIUM = 1,    // 25px
    FONT_LARGE = 2,     // 28px
} font_size_t;

// Font families
typedef enum {
    FONT_LATO = 0,      // Lato (current default for small)
    FONT_NOTOSANS = 1,  // Noto Sans (current default for large)
} font_family_t;

// Text alignment
typedef enum {
    TEXT_ALIGN_CENTER = 0,  // Center (default)
    TEXT_ALIGN_LEFT = 1,    // Left aligned
    TEXT_ALIGN_RIGHT = 2,   // Right aligned
} text_align_t;

// Config for text elements (volume_text, line1, line2, zone)
typedef struct {
    visibility_mode_t visibility;
    font_size_t size;
    font_family_t family;        // Font family (lato or notosans)
    text_align_t align;          // Text alignment (center, left, right)
    uint32_t color;              // RGB hex (0xfafafa)
    uint16_t fade_timeout_ms;    // For VIS_ON_CHANGE (0 = use default 3000ms)
} text_element_config_t;

// Config for arc elements (volume_arc, progress_arc)
typedef struct {
    visibility_mode_t visibility;
    uint32_t color;              // Indicator color
    uint8_t width;               // Arc width in pixels (0 = use default 6px)
    uint16_t fade_timeout_ms;    // For VIS_ON_CHANGE (0 = use default 3000ms)
} arc_element_config_t;

// Icon sizes for buttons (Material Icons font sizes)
typedef enum {
    ICON_SIZE_NORMAL = 0,   // 44px (for secondary buttons)
    ICON_SIZE_LARGE = 1,    // 60px (for primary play/pause button)
} icon_size_t;

// Config for transport buttons (prev_button, play_button, next_button)
typedef struct {
    visibility_mode_t visibility;
    uint32_t icon_color;         // Icon color (0xfafafa default)
    uint32_t bg_color;           // Background color
    uint32_t border_color;       // Border color
    icon_size_t icon_size;       // Icon font size (normal=44px, large=60px)
} button_config_t;

// Complete display config
typedef struct {
    text_element_config_t volume_text;
    text_element_config_t line1;
    text_element_config_t line2;
    text_element_config_t zone;
    arc_element_config_t volume_arc;
    arc_element_config_t progress_arc;
    button_config_t prev_button;
    button_config_t play_button;
    button_config_t next_button;
} display_config_t;
```

### Default Config (Matches Current Behavior)

```c
#define DEFAULT_FADE_TIMEOUT_MS 3000
#define DEFAULT_ARC_WIDTH 6

static display_config_t s_display_config = {
    .volume_text = { .visibility = VIS_ALWAYS, .size = FONT_LARGE, .family = FONT_NOTOSANS, .align = TEXT_ALIGN_CENTER, .color = 0xfafafa, .fade_timeout_ms = DEFAULT_FADE_TIMEOUT_MS },
    .line1       = { .visibility = VIS_ALWAYS, .size = FONT_SMALL, .family = FONT_LATO, .align = TEXT_ALIGN_CENTER, .color = 0xaaaaaa, .fade_timeout_ms = DEFAULT_FADE_TIMEOUT_MS },
    .line2       = { .visibility = VIS_ALWAYS, .size = FONT_LARGE, .family = FONT_NOTOSANS, .align = TEXT_ALIGN_CENTER, .color = 0xfafafa, .fade_timeout_ms = DEFAULT_FADE_TIMEOUT_MS },
    .zone        = { .visibility = VIS_ALWAYS, .size = FONT_SMALL, .family = FONT_LATO, .align = TEXT_ALIGN_CENTER, .color = 0xbbbbbb, .fade_timeout_ms = DEFAULT_FADE_TIMEOUT_MS },
    .volume_arc  = { .visibility = VIS_ALWAYS, .color = 0x5a9fd4, .width = DEFAULT_ARC_WIDTH, .fade_timeout_ms = DEFAULT_FADE_TIMEOUT_MS },
    .progress_arc= { .visibility = VIS_ALWAYS, .color = 0x7bb9e8, .width = DEFAULT_ARC_WIDTH, .fade_timeout_ms = DEFAULT_FADE_TIMEOUT_MS },
    .prev_button = { .visibility = VIS_ALWAYS, .icon_color = 0xfafafa, .bg_color = 0x1a1a1a, .border_color = 0x4a4a4a, .icon_size = ICON_SIZE_NORMAL },
    .play_button = { .visibility = VIS_ALWAYS, .icon_color = 0xfafafa, .bg_color = 0x2c2c2c, .border_color = 0x5a9fd4, .icon_size = ICON_SIZE_LARGE },
    .next_button = { .visibility = VIS_ALWAYS, .icon_color = 0xfafafa, .bg_color = 0x1a1a1a, .border_color = 0x4a4a4a, .icon_size = ICON_SIZE_NORMAL },
};
```

### Firmware Changes Required

#### 1. New file: `common/display_config.h`

- Define enums and structs above
- Declare default config
- Declare `display_config_parse_json()` function
- Declare `display_config_get_default()` function

#### 2. New file: `common/display_config.c`

- Default config instance
- JSON parsing function using cJSON
- Helper to parse visibility string ("always"/"never"/"on_change") to enum
- Helper to parse color string ("#fafafa") to uint32

#### 3. Modify: `common/ui.c`

- Add `#include "display_config.h"`
- Add `ui_apply_display_config()` function
- Add `ui_on_volume_change()` and `ui_on_track_change()` hooks
- Integrate with existing `s_volume_emphasis_timer` for fade logic
- Use `s_state_lock` mutex for thread safety

**Key LVGL APIs:**
- `lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)` - Hide element
- `lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN)` - Show element
- `lv_obj_set_style_text_font()` - Change font dynamically
- `lv_obj_set_style_text_color()` - Change color dynamically
- `lv_obj_set_style_arc_color()` - Change arc color
- Flex layout auto-reflows when elements hidden

#### 4. Modify: `common/roon_api.c` (or equivalent)

- Parse `display_config` from JSON response
- Call `ui_apply_display_config()` when config changes
- Detect track changes and call `ui_on_track_change()`

#### 5. Modify: Volume handling (encoder.c or similar)

- Call `ui_on_volume_change()` when volume adjusts

### Font System

Two font families at three sizes each (6 fonts total):

| Family | Small (22px) | Medium (25px) | Large (28px) |
|--------|-------------|---------------|--------------|
| Lato | `lato_22` | `lato_25` | `lato_28` |
| Noto Sans | `notosans_22` | `notosans_25` | `notosans_28` |

Each text font has a corresponding Material Icons fallback at the same size for icon rendering.

**API**: `font_manager_get_font(family, size)` returns the correct font for any family/size combination.

### Testing Strategy

1. **Without bridge changes**: Firmware uses default config, behaves exactly as today
2. **With mock config**: Hardcode test configs to verify font/color/visibility changes
3. **With bridge**: Full integration once bridge sends display_config

## Scope Boundaries

### In Scope
- All text element visibility (always/never/on_change)
- All text element font size (small/medium/large: 22/25/28px)
- All text element font family (lato/notosans)
- All text element alignment (center/left/right)
- All text element color
- All arc element visibility
- All arc element color
- All arc element width (ring thickness)
- Fade timeout per element
- JSON parsing from bridge response
- Backward compatibility (no config = current behavior)

### Out of Scope (Future)
- Position/spacing overrides
- User-authored matchers via device UI
- Per-zone matcher overrides
- Opacity levels (just show/hide for now)
- Animation/transition effects

## Open Questions

1. **Fade animation**: Snap visibility or animate opacity? Start with snap, add animation later if desired.

2. **Config persistence**: Should device cache last config in NVS? Probably not needed - bridge sends fresh config.

3. **Partial config updates**: Does bridge always send full config, or can it send deltas? Start with full config for simplicity.

## References

- Rich Hickey's talks on data-driven design
- Current UI implementation: `/common/ui.c`
- Bridge architecture: [ADR 002](./2025-12-22_DECISION_BRIDGE_ARCHITECTURE.md)
