# ADR 002: Bridge Architecture Rationale

**Status:** Accepted
**Date:** 2025-12-21
**Context:** User suggestion to eliminate the bridge and run Roon API directly on ESP32-S3

## Summary

**The bridge cannot be eliminated.** This document explains why.

## The Challenge

A user suggested running the Roon API directly on the ESP32-S3, eliminating the need for a separate bridge service. This would simplify deployment (no Docker required) and reduce network hops.

## Investigation

### Current Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  Roon Core (on network)                                         │
└──────────────────────┬──────────────────────────────────────────┘
                       │ Proprietary RPC over WebSocket
                       │ (node-roon-api required)
┌──────────────────────▼──────────────────────────────────────────┐
│  Roon Extension Bridge (Docker, port 8088)                      │
│  • Maintains Roon API connection                                │
│  • Caches zones, now_playing, artwork                           │
│  • Throttles volume commands                                    │
│  • 1,500 lines Node.js + 60MB dependencies                      │
└──────────────────────┬──────────────────────────────────────────┘
                       │ Simple HTTP/REST (polling)
┌──────────────────────▼──────────────────────────────────────────┐
│  ESP32-S3 Knob (on WiFi)                                        │
│  • Renders UI, handles input                                    │
│  • Polls bridge at adaptive intervals                           │
│  • ~4,000 lines C                                               │
└─────────────────────────────────────────────────────────────────┘
```

### Blocker #1: Roon's API is Proprietary

The Roon API uses a **custom RPC protocol** over WebSocket. Key facts:

- **No REST API**: Roon doesn't expose a simple HTTP interface
- **Node.js only**: The only official library is [node-roon-api](https://github.com/RoonLabs/node-roon-api)
- **Undocumented protocol**: The wire format isn't publicly documented
- **Discovery uses UDP**: Roon discovery requires mDNS/UDP, not available in all contexts

To run Roon directly on ESP32 would require **reverse-engineering the protocol** - estimated 100+ hours of work with ongoing maintenance risk as Roon evolves.

### Blocker #2: ESP32-S3 Memory Constraints

Even if we had a C implementation of the Roon protocol:

| Resource | ESP32-S3 | Required for Roon |
|----------|----------|-------------------|
| Internal RAM | 512 KB | TLS alone can exhaust this |
| PSRAM | 8 MB (external) | Slower, can't be used for all allocations |
| TLS heap | ~50 KB minimum | Roon requires secure WebSocket |
| Flash | Limited | Current firmware already substantial |

**Known issues:**
- TLS + WebSocket on ESP32 causes memory fragmentation
- Some allocations require internal RAM even with PSRAM available
- Documented allocation failures with just 65KB internal RAM free

The current knob firmware already uses significant resources for:
- LVGL display rendering
- WiFi stack
- HTTP client
- Image decoding (album art)
- Touch and encoder handling

Adding a full Roon protocol stack would likely not fit.

### Blocker #3: No Existing Embedded Implementation

There is no C, Rust, or embedded Roon API implementation anywhere:

- **node-roon-api** - Node.js only
- **roonapi (Python)** - Requires Python 3.7+
- **pyroon** - Also Python

Every Roon integration project either:
1. Uses Node.js directly
2. Uses an HTTP bridge in front of Node.js (like we do)

## What the Bridge Actually Does

The bridge is more than a protocol translator:

1. **Roon Protocol Handling** (~500 lines)
   - Connection management, pairing tokens
   - Zone subscription and event handling
   - Service initialization (transport, image, status)

2. **State Caching** (~400 lines)
   - Maintains current zones and playback state
   - 5-second grace period on disconnect
   - Prevents hammering Roon Core with requests

3. **Volume Rate Limiting** (~60 lines)
   - 100ms throttling for smooth knob feel
   - Queue management with exponential backoff

4. **Per-Knob Configuration** (~200 lines)
   - Stores settings per device MAC address
   - Config hash for change detection

5. **Image Processing**
   - Fetches album art from Roon
   - Resizes and converts to RGB565 for display

## Why the Bridge is Actually Good

1. **Isolation**: Roon protocol complexity stays in the bridge; knob firmware stays simple
2. **Updateability**: Bridge updates don't require reflashing devices
3. **Multi-device**: One bridge serves N knobs efficiently
4. **Debuggability**: Easy to inspect HTTP traffic and bridge logs
5. **Resilience**: Bridge can recover from Roon Core restarts independently

## Alternatives Considered

### Direct WebSocket from ESP32
- Would still need to implement Roon's RPC protocol
- Memory constraints make this impractical
- No way to store extension pairing token securely

### Python Bridge
- Same problem: still need a bridge, just in Python
- Docker deployment is actually simpler than managing Python dependencies

### Lightweight C Proxy
- Still need something to speak Roon protocol
- Rewriting node-roon-api in C is the 100+ hour project

## Decision

**Keep the bridge architecture.** Focus instead on:

1. Making bridge deployment simpler (already Docker-based)
2. Better error messages when bridge is unreachable
3. Automatic bridge discovery via mDNS (already implemented)
4. Clear documentation of the architecture

## References

- [node-roon-api](https://github.com/RoonLabs/node-roon-api) - Official Roon JavaScript API
- [ESP32-S3 Memory Types](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/memory-types.html)
- [ESP32 PSRAM Limitations](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/external-ram.html)
- [Roon API Community](https://community.roonlabs.com/c/tinkering/roonapi/51)
