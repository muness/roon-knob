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
4. **iPhone widgets & Live Activities** - Lock screen, home screen, Dynamic Island
5. **Shared codebase** - Swift package for bridge API client used by both apps
6. **Sustainable monetization** - Free tier + one-time purchase + tips

## Non-Goals

- Browsing/search (knob doesn't have this)
- Queue management
- Roon-specific features (DSP, radio, etc.)
- Direct Roon API integration (uses bridge like knob)
- Subscriptions (one-time purchase only)

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

### 8. iPhone Widgets (Home Screen & Lock Screen)

WidgetKit widgets for quick glance at now playing and controls.

**Widget Families:**
- `systemSmall` - Album art + play state indicator
- `systemMedium` - Album art + track info + play/pause button
- `systemLarge` - Full now playing with transport controls
- `accessoryCircular` - Lock screen: album art thumbnail
- `accessoryRectangular` - Lock screen: track + artist
- `accessoryInline` - Lock screen: "▶ Track - Artist"

```swift
struct NowPlayingWidget: Widget {
    var body: some WidgetConfiguration {
        StaticConfiguration(
            kind: "NowPlayingWidget",
            provider: NowPlayingWidgetProvider()
        ) { entry in
            NowPlayingWidgetView(entry: entry)
        }
        .configurationDisplayName("Now Playing")
        .description("Shows what's playing and quick controls")
        .supportedFamilies([
            .systemSmall,
            .systemMedium,
            .systemLarge,
            .accessoryCircular,
            .accessoryRectangular,
            .accessoryInline
        ])
    }
}

struct NowPlayingWidgetView: View {
    let entry: NowPlayingEntry
    @Environment(\.widgetFamily) var family

    var body: some View {
        switch family {
        case .systemSmall:
            SmallWidgetView(entry: entry)
        case .systemMedium:
            MediumWidgetView(entry: entry)
        case .systemLarge:
            LargeWidgetView(entry: entry)
        case .accessoryCircular:
            CircularLockScreenView(entry: entry)
        case .accessoryRectangular:
            RectangularLockScreenView(entry: entry)
        case .accessoryInline:
            InlineLockScreenView(entry: entry)
        default:
            EmptyView()
        }
    }
}
```

**Widget Intents (Interactive Controls):**
```swift
struct PlayPauseIntent: AppIntent {
    static var title: LocalizedStringResource = "Play/Pause"

    @Parameter(title: "Zone")
    var zoneId: String?

    func perform() async throws -> some IntentResult {
        let client = BridgeClient.shared
        if let zoneId = zoneId ?? client.selectedZoneId {
            try await client.playPause(zoneId: zoneId)
        }
        return .result()
    }
}
```

### 9. Live Activities (Dynamic Island & Lock Screen)

Live Activities for persistent now playing during active playback.

**When to show:**
- User starts playback from app
- Automatically ends when playback stops or after timeout

**Dynamic Island (Compact):**
- Leading: Album art thumbnail
- Trailing: Play/pause icon + volume level

**Dynamic Island (Expanded):**
- Album art, track, artist
- Play/pause, next/prev buttons
- Volume slider

**Lock Screen Banner:**
- Album art + track info
- Transport controls
- Progress bar (optional)

```swift
struct NowPlayingLiveActivity: Widget {
    var body: some WidgetConfiguration {
        ActivityConfiguration(for: NowPlayingActivityAttributes.self) { context in
            // Lock screen banner
            LockScreenLiveActivityView(context: context)
        } dynamicIsland: { context in
            DynamicIsland {
                // Expanded regions
                DynamicIslandExpandedRegion(.leading) {
                    AsyncImage(url: context.state.artworkURL)
                        .frame(width: 60, height: 60)
                        .cornerRadius(8)
                }
                DynamicIslandExpandedRegion(.center) {
                    VStack(alignment: .leading) {
                        Text(context.state.line1)
                            .font(.headline)
                        Text(context.state.line2)
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                    }
                }
                DynamicIslandExpandedRegion(.trailing) {
                    HStack {
                        Button(intent: PreviousIntent()) {
                            Image(systemName: "backward.fill")
                        }
                        Button(intent: PlayPauseIntent()) {
                            Image(systemName: context.state.isPlaying
                                  ? "pause.fill" : "play.fill")
                        }
                        Button(intent: NextIntent()) {
                            Image(systemName: "forward.fill")
                        }
                    }
                }
            } compactLeading: {
                AsyncImage(url: context.state.artworkURL)
                    .frame(width: 24, height: 24)
                    .cornerRadius(4)
            } compactTrailing: {
                Image(systemName: context.state.isPlaying
                      ? "pause.fill" : "play.fill")
            } minimal: {
                AsyncImage(url: context.state.artworkURL)
                    .frame(width: 24, height: 24)
                    .cornerRadius(4)
            }
        }
    }
}

struct NowPlayingActivityAttributes: ActivityAttributes {
    public struct ContentState: Codable, Hashable {
        var line1: String
        var line2: String
        var isPlaying: Bool
        var volume: Double
        var artworkURL: URL?
        var seekPosition: Int
        var length: Int
    }

    var zoneId: String
    var zoneName: String
}
```

**Live Activity Lifecycle:**
```swift
class LiveActivityManager {
    private var currentActivity: Activity<NowPlayingActivityAttributes>?

    func startActivity(for zone: Zone, nowPlaying: NowPlaying) {
        let attributes = NowPlayingActivityAttributes(
            zoneId: zone.zone_id,
            zoneName: zone.zone_name
        )
        let state = NowPlayingActivityAttributes.ContentState(
            line1: nowPlaying.line1,
            line2: nowPlaying.line2,
            isPlaying: nowPlaying.is_playing,
            volume: nowPlaying.volume,
            artworkURL: artworkURL(for: zone),
            seekPosition: nowPlaying.seek_position,
            length: nowPlaying.length
        )

        currentActivity = try? Activity.request(
            attributes: attributes,
            content: .init(state: state, staleDate: nil),
            pushType: nil  // We poll, no push notifications
        )
    }

    func updateActivity(nowPlaying: NowPlaying) {
        let state = NowPlayingActivityAttributes.ContentState(...)
        Task {
            await currentActivity?.update(
                ActivityContent(state: state, staleDate: nil)
            )
        }
    }

    func endActivity() {
        Task {
            await currentActivity?.end(nil, dismissalPolicy: .immediate)
        }
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
│   └── BridgeClient/              # Shared Swift package
│       ├── Package.swift
│       └── Sources/
│           └── BridgeClient/
│               ├── Models.swift
│               ├── BridgeClient.swift
│               └── BridgeDiscovery.swift
├── Shared/                        # Code shared across all targets
│   ├── ZoneAccessManager.swift
│   ├── StoreManager.swift
│   └── AppIntents.swift           # PlayPauseIntent, NextIntent, etc.
├── HiFiControl/                   # iPhone app
│   ├── App.swift
│   ├── Views/
│   │   ├── NowPlayingView.swift
│   │   ├── ZonePickerView.swift
│   │   ├── SettingsView.swift
│   │   └── UpgradeView.swift
│   ├── Services/
│   │   ├── WatchSyncManager.swift
│   │   └── LiveActivityManager.swift
│   └── Assets.xcassets
├── HiFiControlWidgets/            # iPhone widgets (WidgetKit)
│   ├── NowPlayingWidget.swift
│   ├── WidgetViews/
│   │   ├── SmallWidgetView.swift
│   │   ├── MediumWidgetView.swift
│   │   ├── LargeWidgetView.swift
│   │   └── LockScreenWidgetViews.swift
│   └── Assets.xcassets
├── HiFiControlLiveActivity/       # Live Activities extension
│   ├── NowPlayingLiveActivity.swift
│   ├── DynamicIslandViews.swift
│   └── LockScreenBannerView.swift
├── HiFiControl Watch App/         # Watch app
│   ├── App.swift
│   ├── Views/
│   │   ├── NowPlayingView.swift
│   │   ├── ZonePickerView.swift
│   │   └── VolumeOverlay.swift
│   ├── Services/
│   │   └── PhoneSyncManager.swift
│   └── Assets.xcassets
├── HiFiControl Watch Widgets/     # Watch complications (WidgetKit)
│   ├── NowPlayingComplication.swift
│   └── ComplicationViews.swift
└── README.md
```

---

## Feature Parity Checklist

### Core Features (Knob Parity)

| Knob Feature | Watch/iPhone Feature | Status |
|--------------|---------------------|--------|
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

### Beyond Knob (Platform Features)

| Feature | Platform | Status |
|---------|----------|--------|
| Watch complications | Watch | Planned |
| Home screen widgets | iPhone | Planned |
| Lock screen widgets | iPhone | Planned |
| Live Activity (Dynamic Island) | iPhone | Planned |
| Live Activity (lock screen banner) | iPhone | Planned |
| Interactive widget controls | iPhone | Planned |
| 1 zone free tier | All | Planned |
| Unlock all zones (IAP) | All | Planned |
| Tip jar | iPhone | Planned |

---

## Monetization

### Pricing Model

**Free Tier:**
- 1 zone (user picks which one)
- Full functionality for that zone
- Watch app, iPhone app, widgets, Live Activities
- No time limits or nag screens

**Unlock All Zones (One-Time Purchase):**
- Unlimited zones
- Suggested price: $4.99 - $9.99
- StoreKit 2 for purchase handling
- No subscription, own it forever

**Tip Jar (Optional):**
- For users who want to support development
- Small ($1), Medium ($3), Large ($5) tip options
- Accessible from Settings
- "Buy me a coffee" style

### Why This Model

1. **Low friction adoption** - Free tier is fully functional, not crippled
2. **Fair value exchange** - Multi-zone is a power user feature worth paying for
3. **No recurring costs** - Bridge runs on user's hardware, no server costs for us
4. **Tips for goodwill** - Some users want to support open source adjacent projects

### Implementation

```swift
import StoreKit

enum ProductID: String {
    case unlockAllZones = "com.hificontrol.unlock_all_zones"
    case tipSmall = "com.hificontrol.tip_small"
    case tipMedium = "com.hificontrol.tip_medium"
    case tipLarge = "com.hificontrol.tip_large"
}

class StoreManager: ObservableObject {
    @Published var isUnlocked: Bool = false
    @Published var products: [Product] = []

    func loadProducts() async {
        products = try? await Product.products(for: [
            ProductID.unlockAllZones.rawValue,
            ProductID.tipSmall.rawValue,
            ProductID.tipMedium.rawValue,
            ProductID.tipLarge.rawValue
        ])
    }

    func purchase(_ product: Product) async throws {
        let result = try await product.purchase()
        switch result {
        case .success(let verification):
            let transaction = try checkVerified(verification)
            await transaction.finish()
            if product.id == ProductID.unlockAllZones.rawValue {
                isUnlocked = true
            }
        case .pending, .userCancelled:
            break
        @unknown default:
            break
        }
    }

    func restorePurchases() async {
        for await result in Transaction.currentEntitlements {
            if case .verified(let transaction) = result {
                if transaction.productID == ProductID.unlockAllZones.rawValue {
                    isUnlocked = true
                }
            }
        }
    }
}
```

### Zone Limiting Logic

```swift
class ZoneAccessManager: ObservableObject {
    @Published var allowedZoneId: String?  // Free tier: one zone
    @Published var isUnlocked: Bool = false

    var accessibleZones: [Zone] {
        if isUnlocked {
            return allZones
        } else if let allowedId = allowedZoneId {
            return allZones.filter { $0.zone_id == allowedId }
        } else {
            // First launch: let user pick one zone
            return allZones
        }
    }

    func selectFreeZone(_ zone: Zone) {
        guard !isUnlocked else { return }
        allowedZoneId = zone.zone_id
        UserDefaults.standard.set(zone.zone_id, forKey: "free_zone_id")
    }

    func canAccessZone(_ zone: Zone) -> Bool {
        isUnlocked || zone.zone_id == allowedZoneId
    }
}
```

### UI Touchpoints

**Zone Picker (Free Tier):**
- Show all zones, but locked ones have lock icon
- Tapping locked zone shows upgrade prompt
- "Unlock All Zones" button at bottom

**Settings:**
- Current plan: "Free (1 zone)" or "Unlocked"
- "Unlock All Zones" button (if not purchased)
- "Restore Purchases" button
- "Tip Jar" section with tip buttons

**First Launch:**
- "Choose your zone" picker
- Explain: "Free version controls one zone. Upgrade anytime for unlimited."

---

## Open Questions

1. **App naming**: "HiFi Control"? "Roon Knob"? "Unified Control"?
2. **Complications**: Which families to support? What info to show?
3. **Background refresh**: Is it worth the complexity for complications?
4. **Volume haptics**: How aggressive should Digital Crown haptics be?
5. **Offline mode**: Show last known state when bridge unreachable?
6. **Pricing**: $4.99, $6.99, or $9.99 for unlock? Market research needed.
7. **Family Sharing**: Enable for the unlock purchase?

---

## Implementation Phases

### Phase 1: Core Infrastructure
- [ ] Create Xcode project with Watch target
- [ ] Implement `BridgeClient` Swift package
- [ ] Basic iPhone app with manual bridge URL entry
- [ ] WatchConnectivity sync
- [ ] Zone access manager (free tier logic)

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

### Phase 4: Widgets & Live Activities
- [ ] iPhone home screen widgets (small, medium, large)
- [ ] iPhone lock screen widgets
- [ ] Live Activities (Dynamic Island + lock screen banner)
- [ ] Interactive widget controls (App Intents)

### Phase 5: Watch Complications
- [ ] Circular complication (album art)
- [ ] Rectangular complication (track + artist)
- [ ] Inline complication
- [ ] Background refresh for complications

### Phase 6: Monetization
- [ ] StoreKit 2 integration
- [ ] "Unlock All Zones" purchase
- [ ] Tip jar products
- [ ] First-launch zone picker (free tier)
- [ ] Restore purchases flow
- [ ] App Store Connect product setup

### Phase 7: Polish
- [ ] Art mode / full-screen artwork
- [ ] Volume haptic feedback tuning
- [ ] Error handling and offline states
- [ ] App Store assets and screenshots

---

## References

- [Bridge HTTP Protocol Spec](../../BRIDGE_DRAFT_SPEC.md)
- [Roon Knob roon_client.c](https://github.com/muness/roon-knob/blob/master/common/roon_client.c)
- [WatchConnectivity Guide](https://developer.apple.com/documentation/watchconnectivity)
- [Digital Crown Programming Guide](https://developer.apple.com/documentation/swiftui/digitalcrownrotation)
