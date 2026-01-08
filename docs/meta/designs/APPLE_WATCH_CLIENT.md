# Design: Apple Watch Client for Unified Hi-Fi Control

**Status:** Draft
**Date:** 2026-01-08
**Branch:** `claude/watch-client-knob-parity-vkAQP`

## Summary

An Apple Watch app with iPhone companion that provides Roon Knob feature parity using the [Bridge HTTP Protocol](../../BRIDGE_DRAFT_SPEC.md). The Digital Crown maps naturally to the knob's rotary encoder for volume control.

## Goals

1. **Feature parity with Roon Knob** - Play/pause, volume, next/prev, zone selection, album art
2. **Native Watch experience** - Digital Crown for volume, complications for now playing
3. **iPhone companion for setup** - Bridge discovery, zone filtering, full controls
4. **Shared codebase** - Swift package for bridge API client used by both apps

## Non-Goals

- Browsing/search (knob doesn't have this)
- Queue management
- Roon-specific features (DSP, radio, etc.)
- Direct Roon API integration (uses bridge like knob)

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  iPhone Companion App                                   │
│  ┌─────────────────────────────────────────────────┐   │
│  │  BridgeDiscovery (NWBrowser for mDNS)           │   │
│  │  BridgeClient (shared Swift package)            │   │
│  │  ZoneFilterManager (select zones for Watch)     │   │
│  │  NowPlayingView (full controls)                 │   │
│  │  SettingsView (bridge URL, zone selection)      │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────┬───────────────────────────────────────┘
                  │ WatchConnectivity
                  │ - bridge_url: String
                  │ - allowed_zone_ids: [String]
                  │ - selected_zone_id: String?
┌─────────────────▼───────────────────────────────────────┐
│  Watch App                                              │
│  ┌─────────────────────────────────────────────────┐   │
│  │  BridgeClient (shared Swift package)            │   │
│  │  NowPlayingView (album art, line1/line2)        │   │
│  │  DigitalCrownVolume (WKCrownDelegate)           │   │
│  │  ZonePickerView (filtered list)                 │   │
│  │  Complications (now playing info)               │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────┬───────────────────────────────────────┘
                  │ HTTP (WiFi direct or via iPhone)
┌─────────────────▼───────────────────────────────────────┐
│  unified-hifi-control bridge (:8088)                    │
│  GET /zones, GET /now_playing, POST /control            │
└─────────────────────────────────────────────────────────┘
```

---

## Components

### 1. Shared Swift Package: `BridgeClient`

Shared between iPhone and Watch apps.

```swift
// Models
struct Zone: Codable, Identifiable {
    let zone_id: String
    let zone_name: String
    let state: String  // playing, paused, stopped
    var id: String { zone_id }
}

struct NowPlaying: Codable {
    let line1: String
    let line2: String
    let is_playing: Bool
    let volume: Double
    let volume_min: Double
    let volume_max: Double
    let volume_step: Double
    let seek_position: Int
    let length: Int
    let image_key: String?
    let zones_sha: String?
    let config_sha: String?
}

struct ControlRequest: Encodable {
    let zone_id: String
    let action: String
    var value: Double?
}

// Client
class BridgeClient: ObservableObject {
    @Published var bridgeURL: URL?
    @Published var zones: [Zone] = []
    @Published var nowPlaying: NowPlaying?
    @Published var isConnected: Bool = false

    func fetchZones() async throws -> [Zone]
    func fetchNowPlaying(zoneId: String) async throws -> NowPlaying
    func fetchArtwork(zoneId: String, size: CGSize) async throws -> UIImage?
    func control(zoneId: String, action: String, value: Double?) async throws

    // Convenience
    func playPause(zoneId: String) async throws
    func next(zoneId: String) async throws
    func previous(zoneId: String) async throws
    func setVolume(zoneId: String, volume: Double) async throws
}
```

### 2. iPhone App: Bridge Discovery

```swift
import Network

class BridgeDiscovery: ObservableObject {
    @Published var discoveredBridges: [NWBrowser.Result] = []

    private let browser: NWBrowser

    init() {
        // mDNS service type used by unified-hifi-control
        let descriptor = NWBrowser.Descriptor.bonjour(
            type: "_roonextknob._tcp",
            domain: nil
        )
        browser = NWBrowser(for: descriptor, using: .tcp)
    }

    func startDiscovery()
    func stopDiscovery()
    func resolveEndpoint(_ result: NWBrowser.Result) -> URL?
}
```

### 3. iPhone App: Settings & Zone Filter

```swift
struct SettingsView: View {
    @ObservedObject var bridgeClient: BridgeClient
    @ObservedObject var discovery: BridgeDiscovery
    @Binding var allowedZoneIds: Set<String>

    var body: some View {
        Form {
            // Bridge selection
            Section("Bridge") {
                // Discovered bridges from mDNS
                // Manual URL entry
                // Connection status
            }

            // Zone filtering for Watch
            Section("Zones on Watch") {
                // List all zones with toggles
                // Only selected zones sync to Watch
            }
        }
    }
}
```

### 4. iPhone ↔ Watch Sync

```swift
// Shared between both apps
struct WatchConfig: Codable {
    let bridgeURL: String
    let allowedZoneIds: [String]
    let selectedZoneId: String?
}

// iPhone side
class WatchSyncManager: NSObject, WCSessionDelegate {
    func sendConfigToWatch(_ config: WatchConfig)
    func session(_ session: WCSession,
                 didReceiveMessage message: [String: Any])
}

// Watch side
class PhoneSyncManager: NSObject, WCSessionDelegate {
    @Published var config: WatchConfig?

    func session(_ session: WCSession,
                 didReceiveApplicationContext context: [String: Any])
}
```

### 5. Watch App: Now Playing

```swift
struct NowPlayingView: View {
    @ObservedObject var bridgeClient: BridgeClient
    @State var selectedZoneId: String?

    var body: some View {
        VStack {
            // Album art (cached by image_key)
            AsyncImage(url: artworkURL)
                .frame(width: 100, height: 100)
                .cornerRadius(8)

            // Track info
            Text(nowPlaying.line1)
                .font(.headline)
            Text(nowPlaying.line2)
                .font(.caption)
                .foregroundColor(.secondary)

            // Transport controls
            HStack {
                Button(action: previous) {
                    Image(systemName: "backward.fill")
                }
                Button(action: playPause) {
                    Image(systemName: nowPlaying.is_playing
                          ? "pause.fill" : "play.fill")
                }
                Button(action: next) {
                    Image(systemName: "forward.fill")
                }
            }

            // Volume indicator (controlled by Digital Crown)
            VolumeIndicator(volume: nowPlaying.volume,
                           min: nowPlaying.volume_min,
                           max: nowPlaying.volume_max)
        }
        .focusable()  // Enable Digital Crown
        .digitalCrownRotation($crownValue)
    }
}
```

### 6. Watch App: Digital Crown Volume

```swift
struct DigitalCrownVolumeModifier: ViewModifier {
    @Binding var volume: Double
    let min: Double
    let max: Double
    let step: Double
    let onVolumeChange: (Double) -> Void

    @State private var crownValue: Double = 0

    func body(content: Content) -> some View {
        content
            .focusable()
            .digitalCrownRotation(
                $crownValue,
                from: min,
                through: max,
                by: step,
                sensitivity: .medium,
                isContinuous: false,
                isHapticFeedbackEnabled: true
            )
            .onChange(of: crownValue) { newValue in
                // Debounce and send to bridge
                onVolumeChange(newValue)
            }
    }
}
```

### 7. Watch App: Complications

```swift
struct NowPlayingComplication: Widget {
    var body: some WidgetConfiguration {
        StaticConfiguration(
            kind: "NowPlaying",
            provider: NowPlayingTimelineProvider()
        ) { entry in
            NowPlayingComplicationView(entry: entry)
        }
        .configurationDisplayName("Now Playing")
        .description("Shows current track")
        .supportedFamilies([
            .accessoryCircular,
            .accessoryRectangular,
            .accessoryInline
        ])
    }
}
```

---

## Polling Strategy

### Watch App (Active)
- Poll `/now_playing` every **3 seconds** when app is in foreground
- Use `image_key` to avoid re-fetching artwork
- Check `zones_sha` to refresh zone list on change

### Watch App (Background)
- Use `WKExtendedRuntimeSession` for workout-style sessions (optional)
- Complications update via `BGAppRefreshTask` (15+ min intervals)
- Consider: is background refresh needed? User opens app to control.

### iPhone App
- Poll every **2 seconds** when in foreground
- No background polling (user opens app when needed)

---

## Data Persistence

### iPhone App (UserDefaults / Keychain)
- `bridge_url`: Last used bridge URL
- `allowed_zone_ids`: Zones selected for Watch
- `selected_zone_id`: Last active zone

### Watch App (UserDefaults)
- Receives config via `WatchConnectivity`
- Caches `bridge_url` for when iPhone not reachable
- Caches `allowed_zone_ids` for offline zone picker

---

## Project Structure

```
hifi-control/
├── HiFiControl.xcodeproj
├── Packages/
│   └── BridgeClient/           # Shared Swift package
│       ├── Package.swift
│       └── Sources/
│           └── BridgeClient/
│               ├── Models.swift
│               ├── BridgeClient.swift
│               └── BridgeDiscovery.swift
├── HiFiControl/                # iPhone app
│   ├── App.swift
│   ├── Views/
│   │   ├── NowPlayingView.swift
│   │   ├── ZonePickerView.swift
│   │   └── SettingsView.swift
│   ├── Services/
│   │   └── WatchSyncManager.swift
│   └── Assets.xcassets
├── HiFiControl Watch App/      # Watch app
│   ├── App.swift
│   ├── Views/
│   │   ├── NowPlayingView.swift
│   │   ├── ZonePickerView.swift
│   │   └── VolumeOverlay.swift
│   ├── Services/
│   │   └── PhoneSyncManager.swift
│   ├── Complications/
│   │   └── NowPlayingComplication.swift
│   └── Assets.xcassets
└── README.md
```

---

## Feature Parity Checklist

| Knob Feature | Watch Feature | Status |
|--------------|---------------|--------|
| Rotary encoder → volume | Digital Crown | Planned |
| Tap → play/pause | Tap button | Planned |
| Encoder press → zone picker | Button / swipe | Planned |
| Now playing display | Main view | Planned |
| Album artwork | AsyncImage | Planned |
| Swipe → art mode | Long-press / full screen | Planned |
| Zone switching | List view | Planned |
| Bridge mDNS discovery | iPhone companion | Planned |
| Manual bridge URL | iPhone settings | Planned |
| Volume overlay | Crown indicator | Planned |
| Progress ring | Optional | Deferred |
| Seek position | Text display | Planned |

---

## Open Questions

1. **App naming**: "HiFi Control"? "Roon Knob"? "Unified Control"?
2. **Complications**: Which families to support? What info to show?
3. **Background refresh**: Is it worth the complexity for complications?
4. **Volume haptics**: How aggressive should Digital Crown haptics be?
5. **Offline mode**: Show last known state when bridge unreachable?

---

## Implementation Phases

### Phase 1: Core Infrastructure
- [ ] Create Xcode project with Watch target
- [ ] Implement `BridgeClient` Swift package
- [ ] Basic iPhone app with manual bridge URL entry
- [ ] WatchConnectivity sync

### Phase 2: Watch App MVP
- [ ] Now playing view (line1, line2, play state)
- [ ] Play/pause, next/prev buttons
- [ ] Digital Crown volume control
- [ ] Album artwork display

### Phase 3: iPhone App Features
- [ ] mDNS bridge discovery
- [ ] Zone filtering for Watch
- [ ] Full playback controls
- [ ] Settings persistence

### Phase 4: Polish
- [ ] Complications
- [ ] Art mode / full-screen artwork
- [ ] Volume haptic feedback tuning
- [ ] Error handling and offline states

---

## References

- [Bridge HTTP Protocol Spec](../../BRIDGE_DRAFT_SPEC.md)
- [Roon Knob roon_client.c](https://github.com/muness/roon-knob/blob/master/common/roon_client.c)
- [WatchConnectivity Guide](https://developer.apple.com/documentation/watchconnectivity)
- [Digital Crown Programming Guide](https://developer.apple.com/documentation/swiftui/digitalcrownrotation)
