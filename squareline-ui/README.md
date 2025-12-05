# Roon Knob UI - Squareline Studio Project

This directory contains the Squareline Studio project for the Roon Knob user interface, based on [BlueKnob](https://github.com/joshuacant/BlueKnob) by joshuacant.

## Quick Start

1. **Install Squareline Studio** (free version works)
   - Download from: https://squareline.io/downloads
   - Free version supports basic features we need

2. **Open the Project**
   - Launch Squareline Studio
   - Open `BlueKnob.spj`
   - **IMPORTANT**: First time you open it, you'll need to set export paths

3. **Set Export Paths** (one-time setup)
   - Go to: File → Project Settings
   - **Project Export Root**: `<your-path>/roon-knob/idf_app/main`
   - **UI Files Export Path**: `<your-path>/roon-knob/idf_app/main/ui_squareline`
   - Click "Save Project"

4. **Export UI Code**
   - After making changes: File → Export → Export UI Files (Ctrl+E)
   - This generates C code in `idf_app/main/ui_squareline/`

## Current Screens

### 1. Media Controls (Main Screen)
- Play/Pause button (center)
- Previous/Next buttons
- Volume control via knob
- Bluetooth status indicator (will change to WiFi)
- Swipe left → Settings
- Swipe right → D-Pad controls

**Modifications needed for Roon:**
- [ ] Add track title label (top)
- [ ] Add artist label (below title)
- [ ] Add album label (below artist)
- [ ] Add progress bar/arc
- [ ] Replace Bluetooth icon with WiFi icon
- [ ] Add zone name label

### 2. Settings Screen
- Brightness control (arc widget)
- Display timeout selector (roller)
- Device sleep timeout selector (roller)
- Theme toggle (light/dark buttons)
- Battery percentage display
- Swipe right → Media Controls

**Modifications needed:**
- [ ] Add WiFi SSID display
- [ ] Add battery voltage display
- [ ] Remove unnecessary controls

### 3. D-Pad Screen
- Up/Down/Left/Right buttons
- Enter/Back buttons
- Swipe left → Media Controls
- Swipe right → Trackpad

**For Roon: Can be repurposed as Zone Selector**
- [ ] Replace D-pad with zone list
- [ ] Add zone switching callbacks

### 4. Trackpad Screen
- Full-screen trackpad area
- Mouse cursor control
- Long press to exit

**For Roon: Can be removed or repurposed**
- [ ] Consider removing (not needed for music control)
- [ ] Or repurpose as album art view (if we add image support later)

### 5. Blank Screen
- Used during sleep mode
- Black screen, no elements
- Keep as-is

## Design Resources

### Background Images (24 retro media images)
- Location: `assets/bg001.png` through `bg024.png`
- Resolution: 360×360 pixels
- Format: PNG with transparency
- Theme: Cassette tapes, vinyl records, CDs

**For Roon:**
- Can keep these as static backgrounds
- Or replace with Roon-themed backgrounds
- Or remove to save flash space (4.6MB total)

### Custom Fonts
- Montserrat (various weights)
- Already included in LVGL

### Colors
- Current: Gradient backgrounds (purple/orange, etc.)
- Button colors: Configurable in theme
- Text colors: White/Black based on theme

## Integration with ESP-IDF

### Build System Changes

The exported UI code needs to be integrated into `idf_app/main/CMakeLists.txt`:

```cmake
# Add Squareline-generated UI sources
file(GLOB_RECURSE UI_SOURCES "ui_squareline/*.c")
list(APPEND SRCS ${UI_SOURCES})

# Add UI include directory
set(COMPONENT_ADD_INCLUDEDIRS
    "."
    "ui_squareline"
    "ui_squareline/screens"
    "ui_squareline/components"
)
```

### Code Integration Pattern

**Old way (manual LVGL code in `common/ui.c`):**
```c
void ui_init(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_t *button = lv_btn_create(screen);
    // ... hundreds of lines of manual layout
}
```

**New way (Squareline-generated + callbacks):**
```c
// In main_idf.c
#include "ui_squareline/ui.h"

void ui_init_roon(void) {
    ui_init();  // Call Squareline-generated init

    // UI is now created, just update text
    lv_label_set_text(ui_labelTrackTitle, "No track playing");
    lv_label_set_text(ui_labelArtist, "");
}

// Roon state updates
void roon_update_ui(const RoonState *state) {
    if (lvgl_lock(10)) {
        lv_label_set_text(ui_labelTrackTitle, state->track_title);
        lv_label_set_text(ui_labelArtist, state->artist);
        lv_arc_set_value(ui_progressArc, state->progress_percent);
        lvgl_unlock();
    }
}
```

### Event Callbacks

Squareline generates event function declarations in `ui_events.h`. We implement them:

```c
// In main_idf.c or new ui_callbacks.c
#include "ui_squareline/ui_events.h"

void buttonPlay_onShortClicked(lv_event_t *e) {
    ESP_LOGI(TAG, "Play button clicked");
    roon_playpause();  // Call our Roon API
}

void buttonPrev_onShortClicked(lv_event_t *e) {
    ESP_LOGI(TAG, "Previous button clicked");
    roon_previous();
}

void buttonNext_onShortClicked(lv_event_t *e) {
    ESP_LOGI(TAG, "Next button clicked");
    roon_next();
}

void screenMediaControls_onGestureLeft(lv_event_t *e) {
    ESP_LOGI(TAG, "Swipe left - show settings");
    lv_scr_load(ui_screenSettings);
}
```

## Migration Strategy

### Phase 1: Side-by-side (Current)
- Keep old `common/ui.c` working
- Build new Squareline UI separately in `ui_squareline/`
- Test screens individually
- No changes to main.c yet

### Phase 2: Switch UI implementation
- Change `main_idf.c` to call `ui_init()` from Squareline
- Implement all event callbacks
- Wire up Roon state updates
- Test all functionality

### Phase 3: Cleanup
- Remove old `common/ui.c` code
- Move `ui_squareline/` → `ui/`
- Update documentation
- Optimize asset sizes if needed

## Customization Tips

### Adding a New Label (in Squareline Studio)
1. Drag "Label" widget from left panel
2. Position on screen
3. Set properties:
   - Text: "Track Title" (placeholder)
   - Font: Montserrat 20
   - Color: White (#FFFFFF)
   - Alignment: Center
4. Name it: `labelTrackTitle` (in properties)
5. Export code
6. Update in C code: `lv_label_set_text(ui_labelTrackTitle, track_title);`

### Adding a Progress Bar
1. Drag "Arc" or "Bar" widget
2. Set range: 0-100 (for percentage)
3. Style indicator color
4. Export code
5. Update value: `lv_arc_set_value(ui_progressArc, percent);`

### Changing Colors
1. Select object
2. Properties panel → Style
3. Background Color → Pick color
4. Or use hex: `#1DB954` (Spotify green)
5. Export code

### Adding Swipe Gestures
1. Select screen object
2. Events tab → Add Event
3. Event: GESTURE
4. Action: Call function
5. Function name: `screenName_onGestureLeft`
6. Implement function in C code

## Asset Management

### Reducing Flash Usage

Background images take 4.6MB. Options:
1. **Keep all 24** - Device has 8MB flash, plenty of room
2. **Reduce to 5-10 favorites** - Saves ~3MB
3. **Remove all backgrounds** - Saves 4.6MB, use solid colors
4. **Add Roon branding** - Replace with Roon-themed images

### Adding New Images
1. Place PNG in `assets/` folder (360×360 recommended)
2. In Squareline: Image widget → Browse → Select file
3. Studio converts to C array automatically
4. Export includes new image

### Image Formats
- **PNG**: Best for UI elements, transparency support
- **JPG**: Smaller files, no transparency (future album art?)
- **Raw RGB565**: Fastest rendering, largest files

## Troubleshooting

### "Project paths not set" error
- Open Project Settings
- Set absolute paths to `idf_app/main` and `idf_app/main/ui_squareline`

### Build errors about missing files
- Make sure you exported UI files (Ctrl+E)
- Check CMakeLists.txt includes ui_squareline directory

### UI doesn't update
- Make sure to call `lvgl_lock()` before modifying LVGL objects
- Always call `lvgl_unlock()` after
- Updates from non-LVGL tasks must use locking

### Images not displaying
- Check image paths in generated code match actual files
- Verify images compiled into firmware (check build output)

## Resources

- **Squareline Studio Docs**: https://docs.squareline.io/
- **LVGL Documentation**: https://docs.lvgl.io/8.3/
- **BlueKnob Original**: https://github.com/joshuacant/BlueKnob
- **Waveshare Hardware**: https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8

## Credits

Original UI design by [Joshua Cantrell](https://github.com/joshuacant) for the BlueKnob project.
Adapted for Roon Knob with modifications for music streaming control.
