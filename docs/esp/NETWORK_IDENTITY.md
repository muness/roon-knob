# Network Identity for ESP32 Devices

**Problem:** ESP32 devices appear as "espressif" or "Unknown" in router client lists (UniFi, consumer routers), making them hard to identify and debug.

**Solution:** Set both DHCP hostname and mDNS hostname to make devices visible with meaningful names in router UIs and resolvable via `.local` domain.

**Audience:** This document is for ESP-IDF developers implementing network identity for ESP32 devices. Patterns are reusable for other projects.

---

## Quick Start

**Minimal implementation:**
```c
// 1. Set DHCP hostname BEFORE WiFi starts
esp_netif_t *netif = esp_netif_create_default_wifi_sta();
esp_netif_set_hostname(netif, "my-device-abc123");

// 2. Initialize WiFi
esp_wifi_init(...);
esp_wifi_start();

// 3. Set mDNS hostname AFTER IP acquired
mdns_init();
mdns_hostname_set("my-device-abc123");
```

**Result:**
- Router shows: "my-device-abc123" in client list
- mDNS works: `ping my-device-abc123.local`

---

## Mechanisms

### DHCP Hostname (Option 12)

**What:** Hostname sent during DHCP DISCOVER/REQUEST packets
**Visibility:** Router admin UIs (UniFi, TP-Link, pfSense, etc.)
**API:** `esp_netif_set_hostname(netif, "hostname")`
**Timing:** MUST be set after `esp_netif_create_*()` but BEFORE `esp_wifi_start()`

### mDNS / Bonjour

**What:** Multicast DNS for `.local` domain resolution
**Visibility:** mDNS-aware clients (macOS, Linux, iOS, Android apps)
**API:** `mdns_hostname_set("hostname")`
**Timing:** After WiFi connects and IP is acquired

### Consistency

**Critical:** DHCP and mDNS hostnames should match to avoid confusion.

---

## Implementation Patterns

### Pattern 1: Static Hostname

Simple hardcoded name for single-device deployments.

```c
#define DEVICE_HOSTNAME "my-esp32"

void wifi_init() {
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, DEVICE_HOSTNAME);

    // ... WiFi init ...
}

void mdns_init_handler() {
    mdns_init();
    mdns_hostname_set(DEVICE_HOSTNAME);
    mdns_instance_name_set(DEVICE_HOSTNAME);
}
```

**Pros:** Simple, easy to debug
**Cons:** Name collisions with multiple devices

---

### Pattern 2: MAC-Based Hostname (Recommended)

Unique hostname per device using MAC address suffix.

```c
static char s_hostname[32] = {0};

const char *get_device_hostname(void) {
    if (s_hostname[0] != '\0') {
        return s_hostname;  // Cached
    }

    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        snprintf(s_hostname, sizeof(s_hostname), "esp32-device");
        return s_hostname;
    }

    // Use last 3 bytes for uniqueness: esp32-a1b2c3
    snprintf(s_hostname, sizeof(s_hostname), "esp32-%02x%02x%02x",
             mac[3], mac[4], mac[5]);
    return s_hostname;
}
```

**Pros:** Unique per device, no user config needed
**Cons:** Not human-friendly ("esp32-a1b2c3" vs "Kitchen Sensor")

---

### Pattern 3: Hybrid (MAC + User Override)

Default to MAC-based, allow user configuration.

```c
const char *get_device_hostname(void) {
    if (s_hostname[0] != '\0') {
        return s_hostname;
    }

    // Check user-configured name (from NVS, web UI, etc.)
    if (user_config.device_name[0] != '\0') {
        sanitize_hostname(user_config.device_name, s_hostname, sizeof(s_hostname));
        return s_hostname;
    }

    // Fallback to MAC-based
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_hostname, sizeof(s_hostname), "esp32-%02x%02x%02x",
             mac[3], mac[4], mac[5]);
    return s_hostname;
}

void sanitize_hostname(const char *input, char *output, size_t len) {
    size_t j = 0;
    for (size_t i = 0; input[i] && j < len - 1; i++) {
        char c = input[i];
        // RFC 1123: lowercase alphanumeric + hyphen only
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            output[j++] = c;
        } else if (c >= 'A' && c <= 'Z') {
            output[j++] = c + 32;  // Lowercase
        } else if (c == ' ' || c == '_') {
            output[j++] = '-';  // Space/underscore → hyphen
        }
    }
    output[j] = '\0';

    // Trim leading/trailing hyphens
    size_t start = 0;
    while (output[start] == '-') start++;
    if (start > 0) {
        memmove(output, output + start, j - start + 1);
        j -= start;
    }
    while (j > 0 && output[j - 1] == '-') {
        output[--j] = '\0';
    }

    // Fallback if empty
    if (j == 0) {
        snprintf(output, len, "esp32-device");
    }
}
```

**Pros:** Zero-config for first device, friendly names when desired
**Cons:** Requires user configuration UI

---

## Critical Timing and Gotchas

### 1. Known ESP-IDF Issue: Hostname Resets to "espressif" After esp_wifi_start()

**Issue:** [espressif/esp-idf#4737](https://github.com/espressif/esp-idf/issues/4737) (reported 2020, supposedly fixed but still reproduces in v5.4.3)

**Symptom:**
- Call `esp_netif_set_hostname()` before `esp_wifi_start()`
- Router briefly shows correct hostname
- Then reverts to "espressif"
- `esp_netif_get_hostname()` returns "espressif" after WiFi starts

**Root Cause (per GitHub issue):**
During WiFi low-level initialization, lwIP overwrites the hostname with `CONFIG_LWIP_LOCAL_HOSTNAME` default value, ignoring any previously-set hostname.

**Workaround (testing in this project):**
Set hostname in `WIFI_EVENT_STA_START` event handler - after WiFi init completes but before DHCP runs:

```c
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_STA_START) {
        // Netif is now ready - set hostname before connection/DHCP
        const char *hostname = get_device_hostname();
        esp_err_t err = esp_netif_set_hostname(s_sta_netif, hostname);
        ESP_LOGI(TAG, "Hostname set in STA_START event: %s", hostname);

        // Now connect (triggers DHCP with correct hostname)
        esp_wifi_connect();
    }
}
```

**Verification:**
- Boot logs: Look for "Hostname set in STA_START event: [hostname]"
- After connection: `esp_netif_get_hostname()` should return your hostname, not "espressif"
- Router UI: Should display custom hostname
- Packet capture: DHCP REQUEST should contain hostname in option 12

**See:** `esp_dial/main/wifi_manager.c:314-322` for implementation

**Status:** Workaround implemented, testing in progress against UniFi Controller

### 1. DHCP Hostname MUST Be Set Before WiFi Starts

**Correct order:**
```c
esp_netif_t *netif = esp_netif_create_default_wifi_sta();  // 1. Create netif
esp_netif_set_hostname(netif, "hostname");                 // 2. Set hostname
esp_wifi_init(&cfg);                                       // 3. Init WiFi
esp_wifi_start();                                          // 4. Start WiFi (triggers DHCP)
```

**Why:** The DHCP client reads the hostname from the netif object when building DHCP DISCOVER packets. Setting it after WiFi starts means the first DHCP request uses the default ("espressif").

### 2. Set Hostname on ALL Network Interfaces

If using AP mode (captive portal, provisioning), set hostname on BOTH netifs:

```c
// STA mode
esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
esp_netif_set_hostname(sta_netif, hostname);

// AP mode (when active)
esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
esp_netif_set_hostname(ap_netif, hostname);  // Prevents "espressif" blip
```

**Why:** UniFi and other routers may see both interfaces briefly during mode transitions. If AP netif has default "espressif", the router may cache that name.

### 3. Re-Assert Hostname After IP Acquisition (UniFi Workaround)

Some routers (notably UniFi) don't update their client list from the initial DHCP DISCOVER. Re-asserting after getting IP forces a DHCP INFORM that refreshes the router's cache.

```c
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        // Re-assert hostname to force DHCP INFORM
        esp_netif_set_hostname(sta_netif, get_device_hostname());
        ESP_LOGI(TAG, "Hostname re-asserted after IP acquisition");
    }
}
```

**Why:** UniFi may query the device via SNMP, NetBIOS, or mDNS after initial connection. Re-asserting ensures the hostname is fresh for these queries.

### 4. mDNS Instance Name Should Match Hostname

UniFi's "Device Discovery" may read the mDNS instance name instead of (or in addition to) the hostname.

```c
mdns_init();
mdns_hostname_set("my-device");        // For .local resolution
mdns_instance_name_set("my-device");   // For device discovery (NOT "My Device")
```

**Why:** Some discovery protocols query the instance name. Keeping it consistent prevents mismatches.

### 5. Advertise HTTP Service for Discovery

UniFi and Home Assistant scan for mDNS services. Advertising `_http._tcp` makes devices more discoverable.

```c
mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
```

**Why:** Even if you don't have an HTTP server, advertising the service helps discovery protocols identify the device.

---

## Router-Specific Behavior

### UniFi

**Symptoms:**
- Device briefly shows correct hostname, then reverts to "espressif"
- DHCP hostname doesn't stick in client list

**Cause:** UniFi queries devices via multiple protocols (SNMP, mDNS, UPnP) and prioritizes certain responses over DHCP hostname.

**Fix:**
1. Set hostname on ALL netifs (STA + AP)
2. Re-assert after IP acquisition
3. Set mDNS instance name to match hostname
4. Advertise HTTP service via mDNS

**Verification:**
```bash
# Test DHCP hostname
grep "hostname:" [device logs]  # Should show your name

# Test mDNS
dns-sd -Q my-device.local        # Should resolve
ping my-device.local             # Should work
```

**Workaround if still failing:**
- Manually edit client name in UniFi UI (Settings → Name)
- Forget client and let it re-discover
- Update `CONFIG_LWIP_LOCAL_HOSTNAME` in menuconfig

### Consumer Routers (TP-Link, Netgear, Asus)

**Behavior:** Generally respect DHCP hostname option 12.

**Note:** May take several minutes to update after hostname change (wait for DHCP renewal).

### pfSense / OPNsense

**Behavior:** Full DHCP hostname support, updates immediately.

---

## Configuration

### sdkconfig Settings

**`CONFIG_LWIP_LOCAL_HOSTNAME`** - Default hostname for LwIP stack:

```bash
# In menuconfig:
Component config → LWIP → Local hostname

# Or in sdkconfig.defaults:
CONFIG_LWIP_LOCAL_HOSTNAME="my-device"
```

**When it's used:**
- Fallback if `esp_netif_set_hostname()` is never called
- May be used for NetBIOS/LLMNR (if enabled)
- **Best practice:** Set this to match your runtime hostname, even if you call `esp_netif_set_hostname()`

**NetBIOS/LLMNR** (usually disabled by default):
```
CONFIG_LWIP_NETBIOS_RESPOND_NAME_QUERY=n
CONFIG_LWIP_LLMNR=n
```

Leave these disabled unless you specifically need Windows network discovery.

---

## Testing and Verification

### Verify DHCP Hostname

**In device logs:**
```
I (2100) wifi_mgr: DHCP hostname set: my-device
I (3500) wifi_mgr: Connected to WiFi, hostname: my-device
```

**On router:**
- Check client list for device name
- Look for DHCP lease logs showing hostname option

### Verify mDNS

**From macOS/Linux:**
```bash
# Direct query (bypasses system resolver, tests mDNS directly)
dns-sd -Q my-device.local

# System resolver (what ping uses)
ping -c 3 my-device.local

# List all mDNS services
dns-sd -B _services._dns-sd._udp local.
```

**Expected:**
- `dns-sd -Q` shows device IP immediately
- `ping` works (may have slight delay on first ping)
- Service browse shows `_http._tcp` and `_device-info._udp` services

---

## Common Issues

### Hostname Shows Briefly Then Reverts

**Symptom:** Router shows correct name for 1-2 seconds, then changes to "espressif" or default.

**Cause:**
- AP mode netif created without hostname set
- Router queries secondary protocol (SNMP, mDNS) and gets default name
- `CONFIG_LWIP_LOCAL_HOSTNAME` not updated

**Fix:**
1. Set hostname on ALL netifs (STA + AP)
2. Re-assert hostname in `IP_EVENT_STA_GOT_IP` handler
3. Update `CONFIG_LWIP_LOCAL_HOSTNAME` in menuconfig
4. Set mDNS instance name to match hostname

### mDNS Works But Router UI Wrong

**Symptom:** `ping device.local` works, but router shows wrong name.

**Cause:** Router is reading name from different source than DHCP/mDNS.

**Verification:**
```c
// In IP_EVENT_STA_GOT_IP handler, verify hostname persists:
const char *check = NULL;
esp_netif_get_hostname(netif, &check);
ESP_LOGI(TAG, "Hostname after connection: %s", check);
```

If this shows correct name but router doesn't, it's a router display issue (not your code).

**Workaround:** Manually edit client name in router UI.

### Multiple Devices Show Same Name

**Cause:** Using static hostname without device-specific identifier.

**Fix:** Use MAC-based suffix (Pattern 2) or user-configurable names (Pattern 3).

---

## Complete Working Example (Roon Knob Implementation)

**Files:**
- `esp_dial/main/wifi_manager.c` - Hostname generation and DHCP setup
- `esp_dial/main/platform_mdns_idf.c` - mDNS configuration
- `common/rk_cfg.h` - Config structure with `knob_name` field

**Hostname Generation (`wifi_manager.c`):**
```c
static char s_device_hostname[32] = {0};

static const char *get_device_hostname(void) {
    if (s_device_hostname[0] != '\0') {
        return s_device_hostname;  // Cached
    }

    // Priority: user config → MAC-based → fallback
    if (user_cfg.device_name[0] != '\0') {
        sanitize_hostname(user_cfg.device_name, s_device_hostname, sizeof(s_device_hostname));
        return s_device_hostname;
    }

    // MAC-based fallback
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_hostname, sizeof(s_device_hostname), "roon-knob-%02x%02x%02x",
             mac[3], mac[4], mac[5]);
    return s_device_hostname;
}
```

**DHCP Setup (`wifi_manager.c`):**
```c
void wifi_mgr_start(void) {
    // Create STA netif
    s_sta_netif = esp_netif_create_default_wifi_sta();

    // Set hostname BEFORE WiFi starts
    const char *hostname = get_device_hostname();
    esp_err_t err = esp_netif_set_hostname(s_sta_netif, hostname);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "DHCP hostname set: %s", hostname);
    }

    // Initialize and start WiFi
    esp_wifi_init(&cfg);
    esp_wifi_start();
}

// Re-assert after IP acquisition (UniFi workaround)
static void ip_event_handler(...) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        // Verify hostname persists
        const char *check = NULL;
        esp_netif_get_hostname(s_sta_netif, &check);
        ESP_LOGI(TAG, "Connected, hostname: %s", check);

        // Re-assert to force DHCP INFORM
        esp_netif_set_hostname(s_sta_netif, get_device_hostname());
    }
}
```

**AP Mode Hostname (`wifi_manager.c`):**
```c
void start_ap_mode(void) {
    // Create AP netif
    s_ap_netif = esp_netif_create_default_wifi_ap();

    // Set hostname on AP too (prevents "espressif" during mode transitions)
    const char *hostname = get_device_hostname();
    esp_netif_set_hostname(s_ap_netif, hostname);

    // Configure and start AP
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_start();
}
```

**mDNS Setup (`platform_mdns_idf.c`):**
```c
void platform_mdns_init(const char *hostname) {
    mdns_init();

    const char *host = (hostname && hostname[0]) ? hostname : "esp32-device";

    // Set both hostname and instance name for consistency
    mdns_hostname_set(host);
    mdns_instance_name_set(host);  // Match hostname, not "ESP32 Device"

    // Advertise HTTP for UniFi device discovery
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    // Optional: Advertise device info
    mdns_txt_item_t txt[] = {{"product", "my-device"}};
    mdns_service_add(NULL, "_device-info", "_udp", 9, txt, 1);
}
```

---

## Debugging Checklist

When hostname doesn't appear in router:

- [ ] `esp_netif_set_hostname()` called BEFORE `esp_wifi_start()`?
- [ ] Hostname set on ALL netifs (STA + AP if using both)?
- [ ] Logs show `"DHCP hostname set: [name]"`?
- [ ] Logs show `"Connected, hostname: [name]"` after IP acquisition?
- [ ] `CONFIG_LWIP_LOCAL_HOSTNAME` updated in menuconfig?
- [ ] mDNS hostname matches DHCP hostname?
- [ ] mDNS instance name matches hostname (not "ESP32 Device")?
- [ ] Device responds to `ping device.local`?
- [ ] Router DHCP lease renewed? (try forgetting client)

---

## Key Learnings from Roon Knob

### The "Toothless Flicker" Issue

**Symptom:** UniFi briefly showed "toothless", then reverted to "espressif".

**Root Cause:** Multiple network identities being advertised:
1. DHCP hostname: "toothless" ✅
2. mDNS hostname: "toothless" ✅
3. mDNS instance name: "Roon Knob" (different!) ❌
4. AP mode netif: "espressif" (no hostname set) ❌

**Fix:**
- Set hostname on AP netif when created
- Make mDNS instance name match hostname
- Re-assert hostname after IP acquisition
- Advertise `_http._tcp` service for device discovery

**Result:** Consistent "toothless" across all protocols and interfaces.

---

## References

- ESP-IDF Network Interfaces: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_netif.html
- mDNS Component: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mdns.html
- DHCP Option 12 (RFC 2132): https://datatracker.ietf.org/doc/html/rfc2132#section-3.14
- Hostname RFC 1123: https://datatracker.ietf.org/doc/html/rfc1123#section-2.1

---

## Roon Knob Implementation

See working implementation:
- `esp_dial/main/wifi_manager.c:99-165` - Hostname generation with sanitization
- `esp_dial/main/wifi_manager.c:410-421` - DHCP hostname setup (STA)
- `esp_dial/main/wifi_manager.c:363-372` - DHCP hostname setup (AP)
- `esp_dial/main/wifi_manager.c:338-344` - Hostname re-assertion after IP
- `esp_dial/main/platform_mdns_idf.c:35-49` - mDNS configuration

**GitHub PR:** https://github.com/muness/roon-knob/pull/68
