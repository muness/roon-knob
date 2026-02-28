# WiFi Provisioning

This document covers how the Roon Knob connects to WiFi and handles provisioning when credentials are missing or invalid.

## Overview

The device operates in two WiFi modes:

| Mode | Purpose | SSID |
|------|---------|------|
| **STA (Station)** | Normal operation - connects to your home network | Your WiFi |
| **AP (Access Point)** | Provisioning - creates a setup network | `roon-knob-setup` |

On first boot or after failed connections, the device creates an open WiFi network for configuration via a captive portal.

## State Machine

```
                    ┌─────────────────────────────────────────┐
                    │              BOOT                        │
                    └─────────────────┬───────────────────────┘
                                      │
                                      ▼
                    ┌─────────────────────────────────────────┐
                    │         Load config from NVS            │
                    └─────────────────┬───────────────────────┘
                                      │
                         ┌────────────┴────────────┐
                         │                         │
                    SSID empty?              SSID exists
                         │                         │
                         ▼                         ▼
              ┌──────────────────┐    ┌──────────────────────┐
              │  Start AP Mode   │    │   Connect to WiFi    │
              │  (provisioning)  │    │     (STA mode)       │
              └────────┬─────────┘    └──────────┬───────────┘
                       │                         │
                       │              ┌──────────┴──────────┐
                       │              │                     │
                       │           Success              5 failures
                       │              │                     │
                       │              ▼                     │
                       │    ┌─────────────────┐             │
                       │    │  Got IP address │             │
                       │    │ (normal operation)            │
                       │    └─────────────────┘             │
                       │                                    │
                       ◄────────────────────────────────────┘
                       │
                       ▼
              ┌──────────────────┐
              │  Captive Portal  │
              │  (HTTP + DNS)    │
              └────────┬─────────┘
                       │
                  User submits
                  credentials
                       │
                       ▼
              ┌──────────────────┐
              │  Save to NVS     │
              │  Stop AP mode    │
              │  Try STA connect │
              └──────────────────┘
```

## STA Mode (Normal Operation)

### Connection Flow

1. `wifi_mgr_start()` initializes WiFi in STA mode
2. Loads credentials from NVS storage
3. Attempts connection with exponential backoff on failure
4. Fires `RK_NET_EVT_GOT_IP` event when connected

### Exponential Backoff

Failed connections retry with increasing delays:

```c
static const uint32_t s_backoff_ms[] = {500, 1000, 2000, 4000, 8000, 16000, 30000};
```

After 5 consecutive failures (`STA_FAIL_THRESHOLD`), switches to AP mode for reprovisioning.

### Power Management

```c
// Disable WiFi power save for reliable HTTP polling
esp_wifi_set_ps(WIFI_PS_NONE);

// Reduce TX power for battery operation (11 dBm instead of 20 dBm)
// Reduces peak current from ~500mA to ~200mA
esp_wifi_set_max_tx_power(44);  // Units are 0.25 dBm
```

### Network Events

The WiFi manager uses a weak callback for UI notifications:

```c
typedef enum {
    RK_NET_EVT_CONNECTING,   // Attempting STA connection
    RK_NET_EVT_GOT_IP,       // STA connected with IP
    RK_NET_EVT_FAIL,         // STA connection failed (will retry)
    RK_NET_EVT_AP_STARTED,   // Switched to AP mode
    RK_NET_EVT_AP_STOPPED,   // AP mode stopped
} rk_net_evt_t;

// Override this in your app to handle events
void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt);
```

### Device Name on Network

Your router's client list will show the device as:
- **Default:** `roon-knob-a1b2c3` (unique ID based on device MAC)
- **Custom:** Set a friendly name like "Kitchen Knob" via the bridge admin UI

**To set a custom name:**
1. Open bridge admin UI at `http://your-bridge-ip:8088`
2. Set the "Knob Name" field (e.g., "Kitchen Knob")
3. Device reboots automatically and appears with new name

**Note:** Some routers take a few minutes to update after name changes.

## AP Mode (Provisioning)

### When AP Mode Activates

1. **No credentials stored** - First boot or after factory reset
2. **Too many STA failures** - 5 consecutive connection failures
3. **Manual trigger** - Future: settings menu option

### AP Configuration

| Setting | Value |
|---------|-------|
| SSID | `roon-knob-setup` |
| Password | (none - open network) |
| IP Address | `192.168.4.1` |
| Max Connections | 2 |
| Channel | 1 |

### Components Started in AP Mode

1. **WiFi AP** - Creates the setup network
2. **HTTP Server** - Serves the configuration form on port 80
3. **DNS Server** - Hijacks all DNS queries for captive portal detection

## Captive Portal

The captive portal provides a web-based configuration interface.

### How It Works

1. User connects phone/laptop to `roon-knob-setup` network
2. DNS server responds to ALL queries with `192.168.4.1`
3. Phone detects captive portal (connectivity check fails)
4. Phone auto-opens browser to `http://192.168.4.1/`
5. User enters WiFi credentials
6. Device saves credentials and switches to STA mode

### DNS Hijacking

The DNS server is key to captive portal detection. When your phone connects to a new network, it makes HTTP requests to known URLs (like `captive.apple.com` for iOS). The DNS server redirects these to the AP IP:

```c
// Every DNS query returns 192.168.4.1
response[ans_start + 12] = 192;
response[ans_start + 13] = 168;
response[ans_start + 14] = 4;
response[ans_start + 15] = 1;
```

### HTTP Endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/` | GET | Serve configuration form |
| `/configure` | POST | Accept credentials |
| `/*` | GET | Redirect to `/` (captive portal) |

### Configuration Form

The form collects:

- **WiFi SSID** (required)
- **WiFi Password** (optional, for open networks)
- **Bridge URL** (optional, defaults to mDNS discovery)

### After Submission

1. Credentials saved to NVS
2. 2-second delay (lets HTTP response complete)
3. AP mode stops
4. STA mode starts with new credentials
5. User reconnects phone to home WiFi

## API Reference

### wifi_manager.h

```c
// Start WiFi manager (call once at boot)
void wifi_mgr_start(void);

// Apply new config and reconnect
void wifi_mgr_reconnect(const rk_cfg_t *cfg);

// Clear WiFi credentials and reconnect (triggers AP mode)
void wifi_mgr_forget_wifi(void);

// Get current IP address (returns false if not connected)
bool wifi_mgr_get_ip(char *buf, size_t n);

// Get configured SSID
void wifi_mgr_get_ssid(char *buf, size_t n);

// Check if in AP provisioning mode
bool wifi_mgr_is_ap_mode(void);

// Stop AP mode and attempt STA connection
void wifi_mgr_stop_ap(void);
```

### captive_portal.h

```c
// Start captive portal (HTTP server + DNS hijacking)
void captive_portal_start(void);

// Stop captive portal
void captive_portal_stop(void);

// Check if captive portal is running
bool captive_portal_is_running(void);
```

## Configuration Storage

WiFi credentials are stored in NVS (Non-Volatile Storage):

```c
typedef struct {
    uint8_t cfg_ver;           // Config version (0 = unconfigured)
    char ssid[33];             // WiFi SSID
    char pass[65];             // WiFi password
    char bridge_base[128];     // Bridge URL (optional)
    char zone_id[64];          // Selected Roon zone
} rk_cfg_t;
```

See [NVS_STORAGE.md](../dev/NVS_STORAGE.md) for details on persistence.

## Kconfig Options

Build-time defaults in `esp_dial/main/Kconfig.projbuild`:

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_RK_DEFAULT_SSID` | `""` | Default WiFi SSID (for development) |
| `CONFIG_RK_DEFAULT_PASS` | `""` | Default WiFi password |
| `CONFIG_RK_DEFAULT_BRIDGE_BASE` | `""` | Default bridge URL |

These are applied when NVS has no saved configuration.

## Troubleshooting FAQ

### I connected to `roon-knob-setup` but nothing happened — where do I enter my WiFi details?

The device runs a captive portal that *should* pop up automatically, but some phones don't detect it. Here's what to do:

1. **Turn off mobile data** on your phone (this is the most common fix — with cellular on, your phone bypasses the captive portal detection)
2. Connect to the `roon-knob-setup` WiFi network
3. If the portal still doesn't pop up, **open a browser and go to `http://192.168.4.1`**
4. You'll see a form asking for your WiFi SSID and password — fill it in and submit
5. The device will reboot and connect to your home network

**Tip:** Wait a few seconds after connecting before opening the browser. Some phones need a moment to settle onto the new network.

### The captive portal page won't load

- Make sure you're still connected to `roon-knob-setup` (some phones auto-disconnect from networks without internet)
- Double-check mobile data is off
- Try `http://192.168.4.1` in a private/incognito browser window (avoids cached redirects)
- On iPhone, try opening Safari specifically — the built-in captive portal browser can be finicky

### I don't see the `roon-knob-setup` network at all

- The device only creates the setup network when it has no saved WiFi credentials (first boot) or after 5 failed connection attempts
- If it previously connected to a network, it may be trying to reconnect — wait about 30 seconds for it to give up and switch to AP mode
- To force it back to setup mode: long-press the zone label → Settings → Forget WiFi

### I entered my WiFi details but the device won't connect

- Double-check the SSID and password are exactly right (passwords are case-sensitive)
- Make sure your router is running on 2.4 GHz — the device does not support 5 GHz networks
- Some routers block new devices by default (check your router's MAC filtering or client approval settings)
- The device will retry 5 times, then return to AP mode so you can try again

### The device connected to WiFi but can't find the bridge

- The bridge URL is optional — the device discovers it automatically via mDNS
- Make sure the [bridge](https://github.com/cloud-atlas-ai/unified-hifi-control) is running on the same network as the knob
- If mDNS discovery isn't working, you can enter the bridge URL manually in settings

## Implementation Files

| File | Purpose |
|------|---------|
| `wifi_manager.c` | STA/AP mode management, connection logic |
| `wifi_manager.h` | Public API |
| `captive_portal.c` | HTTP server for configuration form |
| `dns_server.c` | DNS hijacking for captive portal detection |
