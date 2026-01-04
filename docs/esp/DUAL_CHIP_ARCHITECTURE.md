# Dual-Chip Architecture: ESP32 + ESP32-S3

> **Note**: Bluetooth mode was removed from this project. The secondary ESP32 chip is unused. This documentation is kept as reference for the Waveshare board's hardware capabilities.

The Waveshare ESP32-S3-Knob-Touch-LCD-1.8 board contains **two separate ESP32 chips** that communicate via UART. This enables capabilities not possible with either chip alone.

## Hardware Overview

| Chip | Model | Bluetooth | Primary Role |
|------|-------|-----------|--------------|
| ESP32-S3 | ESP32-S3-WROOM-1 (R8) | BLE 5.0 only | Display, Touch, WiFi, BLE HID |
| ESP32 | ESP32-WROOM | Classic BT + BLE | Audio DAC, AVRCP, A2DP |

## Why Two Chips?

The ESP32-S3 has superior graphics performance and more GPIO, but **lacks Classic Bluetooth**. The original ESP32 supports Classic Bluetooth (required for AVRCP/A2DP audio profiles) but has weaker graphics.

By combining both:
- ESP32-S3 handles display rendering, touch input, WiFi, and BLE HID
- ESP32 handles Classic Bluetooth audio profiles and the audio DAC

## Inter-Chip Communication

### UART Connection

From the schematic (3_ESP32-CHIP.png), the bottom-left signal table shows:

| Signal Name | Description |
|-------------|-------------|
| `ESP32I_TX` | ESP32's TX → ESP32-S3's RX |
| `ESP32I_RV` | ESP32's RX ← ESP32-S3's TX |

**GPIO Assignments:**

| Chip | Direction | GPIO | Signal | Notes |
|------|-----------|------|--------|-------|
| ESP32 | TX (out) | **GPIO23** | → S3 RX | UART1 TX |
| ESP32 | RX (in) | **GPIO18** | ← S3 TX | UART1 RX |
| ESP32-S3 | TX (out) | **GPIO38** | → ESP32 RX | UART1 TX |
| ESP32-S3 | RX (in) | **GPIO48** | ← ESP32 TX | UART1 RX |

> **Note**: GPIO pins verified via testing (Dec 2025). The original GPIO17/18 assumption
> was incorrect - those pins are used by the display QSPI (DATA2/DATA3).
> UART0 (GPIO1/3) cannot be used as it's the USB programming port.

**UART Configuration:**
- Baud rate: **1,000,000** (1 Mbps) for low latency
- Data bits: 8
- Parity: None
- Stop bits: 1
- Flow control: None

**ESP32 UART Peripheral**: Use UART1 or UART2 (UART0 is for USB programming)
**ESP32-S3 UART Peripheral**: Use UART1 (UART0 is for USB programming)

### USB Programming

The board uses a **CH445P 4-SPDT analog switch** to share a single USB-C port between both chips. This allows programming either chip without hardware modifications.

To switch programming target:
1. Flip the USB-C connector orientation
2. The CH445P routes USB signals to the alternate chip
3. Use appropriate serial port for each chip

## Peripheral Connections

### ESP32-S3 Peripherals
- **Display**: SH8601 AMOLED via QSPI
- **Touch**: CST816 via I2C
- **Rotary Encoder**: Quadrature input
- **WiFi**: 2.4GHz 802.11 b/g/n
- **BLE**: Bluetooth 5.0 LE

### ESP32 Peripherals
- **Audio DAC**: PCM5100PWR via I2S
  - `ESP32_I2S_DAC_BCK` - Bit clock
  - `ESP32_I2S_DAC_DIN` - Data in
  - `ESP32_I2S_DAC_LRCK` - Left/Right clock
- **Classic Bluetooth**: BR/EDR for A2DP/AVRCP
- **Rotary Encoder**: Second encoder input (board has dual encoders)

### Shared/Other
- **DRV2605**: Haptic driver via I2C
- **SD Card**: SPI interface
- **Battery**: Charging circuit with ADC monitoring

## Use Cases

### 1. Current Implementation (ESP32-S3 only)
- Roon control via WiFi
- BLE HID media control
- ESP32 likely running minimal USB-UART bridge firmware

### 2. AVRCP Metadata (requires ESP32 firmware)
The ESP32 can:
1. Connect to phone via Classic Bluetooth
2. Run AVRCP controller to receive track metadata
3. Send metadata to ESP32-S3 via UART
4. ESP32-S3 displays track info

### 3. A2DP Audio Sink (requires ESP32 firmware)
The ESP32 can:
1. Act as Bluetooth speaker (A2DP sink)
2. Output audio via PCM5100 DAC
3. Show audio visualization on ESP32-S3 display

## Key Discoveries: Bluetooth Compatibility

Through extensive testing with iPhones and DAPs (KANN Ultra), we discovered critical
compatibility requirements:

### A2DP Required for AVRCP (Some Devices)

**Problem**: Phones (iPhone, Android) require A2DP Sink service to establish AVRCP.
Without A2DP visible, the phone won't connect AVRCP for metadata.

**But**: A2DP audio data causes heap corruption crashes in ESP-IDF when we don't
actually process the audio stream.

**Solution**: Initialize A2DP Sink, then delete its SDP record after initialization:
```c
esp_a2d_sink_init();  // Required for AVRCP
// After 100ms, delete the SDP record
SDP_DeleteRecord(bta_av_cb.sdp_a2d_snk_handle);
```

This allows phones to connect AVRCP (service was visible during pairing) but
prevents new audio connections.

### DAPs Need A2DP to Maintain AVRCP

**Problem**: Some DAPs (like KANN Ultra) require A2DP to maintain the AVRCP
connection. When A2DP disconnects, AVRCP drops too.

**Solution**: Use BLE HID for controls on DAPs. BLE HID works independently
and provides volume, play/pause, next/prev functionality.

### Dual-Mode Solution: BLE HID + AVRCP

The final architecture uses both BLE and Classic Bluetooth:

1. **BLE HID "Knob control"** (advertises first):
   - Volume up/down
   - Play/pause (using PLAY as toggle for compatibility)
   - Next/previous track
   - Works on ALL devices (phones and DAPs)
   - Uses random MAC address (separate from Classic BT)

2. **Classic BT "Knob info"** (enabled after BLE connects):
   - AVRCP for metadata (title, artist, album, duration)
   - Play state and position updates
   - Works on phones; DAPs may not connect (need A2DP)

### Device Compatibility Matrix

| Device | BLE HID Controls | AVRCP Metadata | Notes |
|--------|-----------------|----------------|-------|
| iPhone | ✓ | ✓ | Full functionality |
| Android | ✓ | ✓ | Full functionality |
| KANN Ultra | ✓ | ✗ | DAP needs A2DP for AVRCP |
| Other DAPs | ✓ | ? | BLE HID likely works |

### HID Consumer Control Quirks

Different devices respond differently to HID consumer control codes:

| Code | Value | iPhone | KANN | Notes |
|------|-------|--------|------|-------|
| PLAY | 0xB0 | Play only | Toggle | Use for play AND pause on DAPs |
| PAUSE | 0xB1 | Pause only | Ignored | iPhone needs explicit PAUSE |
| PLAY_PAUSE | 0xCD | N/A | N/A | Not supported by ESP-IDF HID |

**Workaround**: Send PLAY for both play and pause functions. Works as toggle on
DAPs, and iPhone accepts PLAY to resume (use separate PAUSE for iPhone).

## Inter-Chip Protocol Design

Binary TLV (Type-Length-Value) protocol for robust, extensible communication.

### Frame Format

```
| Start | Type | Length | Payload  | CRC8 | End  |
| 0x7E  | 1B   | 2B LE  | 0-255B   | 1B   | 0x7F |
```

- **Start**: Frame delimiter (0x7E)
- **Type**: Message type (1 byte)
- **Length**: Payload length, little-endian (2 bytes)
- **Payload**: Variable length data (0-255 bytes)
- **CRC8**: Checksum over Type+Length+Payload
- **End**: Frame delimiter (0x7F)

### Commands (S3 → ESP32)

| Type | Name | Payload | Description |
|------|------|---------|-------------|
| 0x01 | CMD_PLAY | - | AVRCP play |
| 0x02 | CMD_PAUSE | - | AVRCP pause |
| 0x03 | CMD_NEXT | - | Next track |
| 0x04 | CMD_PREV | - | Previous track |
| 0x05 | CMD_VOL_UP | - | Volume up |
| 0x06 | CMD_VOL_DOWN | - | Volume down |
| 0x10 | CMD_BT_CONNECT | - | Connect to last device |
| 0x11 | CMD_BT_DISCONNECT | - | Disconnect |
| 0x12 | CMD_BT_PAIR_MODE | - | Enter discoverable mode |
| 0xF0 | CMD_PING | - | Heartbeat request |

### Events (ESP32 → S3)

| Type | Name | Payload | Description |
|------|------|---------|-------------|
| 0x20 | EVT_BT_STATE | u8 state | Connection state changed |
| 0x21 | EVT_PLAY_STATUS | u8 status | Playing/paused/stopped |
| 0x22 | EVT_METADATA | u8 type + string | Track metadata (title/artist/album) |
| 0x23 | EVT_DEVICE_NAME | string | Connected device name |
| 0xF1 | EVT_PONG | - | Heartbeat response |
| 0xFE | EVT_ACK | u8 cmd_type | Command acknowledged |
| 0xFF | EVT_ERROR | u8 code + string | Error occurred |

### State Enums

**BT State (EVT_BT_STATE payload):**
- 0x00: STATE_DISCONNECTED
- 0x01: STATE_DISCOVERABLE
- 0x02: STATE_CONNECTING
- 0x03: STATE_CONNECTED

**Play Status (EVT_PLAY_STATUS payload):**
- 0x00: PLAY_UNKNOWN
- 0x01: PLAY_STOPPED
- 0x02: PLAY_PLAYING
- 0x03: PLAY_PAUSED

**Metadata Type (EVT_METADATA first byte):**
- 0x01: META_TITLE
- 0x02: META_ARTIST
- 0x03: META_ALBUM

### Heartbeat

- S3 sends CMD_PING every 3 seconds
- ESP32 responds with EVT_PONG
- 3 missed pongs indicates ESP32 failure

## Implementation Roadmap

### Phase 1: Research (Complete)
- [x] Identify dual-chip architecture
- [x] Find UART connection in schematic
- [x] Document peripheral assignments
- [x] Identify exact UART GPIO pins
- [x] Design binary protocol (TLV with CRC)

### Phase 2: ESP32 Firmware (Complete)
- [x] Create ESP-IDF project (`esp32_bt/`)
- [x] Implement binary UART protocol
- [x] Implement Classic BT + AVRCP Controller
- [x] Implement AVRCP passthrough commands
- [x] Implement pairing and auto-reconnect
- [x] Implement state machine and heartbeat

### Phase 3: ESP32-S3 Integration (Complete)
- [x] Add UART driver and protocol handler
- [x] Create Bluetooth controller mode
- [x] Implement command forwarding
- [x] Display track metadata in UI
- [x] BLE HID on ESP32 (works alongside AVRCP)

### Phase 4: Testing & Polish
- [ ] Integration testing (Android, iOS)
- [ ] Reconnection and edge case testing
- [ ] Memory and stability testing

## References

- [Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8)
- [CNX Software Article](https://www.cnx-software.com/2025/06/25/battery-powered-knob-display-board-pairs-esp32-s3-and-esp32-wireless-socs-features-audio-dac-for-audio-visualization/)
- Schematic: `ESP32-S3-Knob-Touch-LCD-1.8-schematic.zip` from Waveshare

## Schematic Pages

| File | Contents |
|------|----------|
| 1_LCD&POWER.png | Display, power supply, battery charging |
| 2_ESP32S3-R8.png | ESP32-S3 module and GPIO assignments |
| 3_ESP32-CHIP.png | ESP32 module, UART connection to S3 |
| 4_OTHER.png | SD card, microphone, misc peripherals |
| 5_DAC.png | PCM5100 audio DAC, DRV2605 haptic |
