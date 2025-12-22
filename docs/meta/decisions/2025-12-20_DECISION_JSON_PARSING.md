# Decision: Use cJSON for Config Parsing

**Date:** 2025-12-20
**Status:** Accepted

## Context

The firmware needs to parse knob configuration from the bridge API (`GET /config/{knob_id}`). The config response contains 15+ fields across multiple types (strings, integers, booleans) with nested objects for dim/sleep settings.

## Options Considered

### Option A: cJSON (ESP-IDF standard component)

Use the cJSON library that's bundled with ESP-IDF to parse the JSON response.

**Pros:**
- Already included in ESP-IDF v5.x as standard component (zero binary overhead)
- Robust parsing - handles edge cases, escaping, whitespace variations
- Industry standard, well-tested, maintained by ESP-IDF team
- No bridge API changes required
- Standard REST pattern - config endpoint returns JSON
- ~20 lines of parsing code

**Cons:**
- Adds `json` to CMakeLists.txt PRIV_REQUIRES

### Option B: Simple line-based format from bridge

Have the bridge return a custom `key=value` format instead of JSON.

**Pros:**
- Simpler firmware parsing with sscanf/strtok
- No library dependency visible in code

**Cons:**
- Requires bridge API changes (new format parameter or endpoint)
- Custom format maintenance - another thing to document and test
- Less robust - custom parsing for custom format
- Breaks RESTful design pattern
- Future brittleness when config schema evolves
- Bridge already returns JSON everywhere else

### Option C: Manual JSON string parsing

Extend existing `strstr()`/`extract_json_string()` pattern used for simple responses.

**Pros:**
- No new dependencies
- Consistent with existing simple parsing

**Cons:**
- ~200 lines of repetitive, fragile parsing code
- Breaks if whitespace or formatting changes
- Error-prone for nested objects
- Existing pattern only works for 2-3 simple fields

## Decision

**Use cJSON (Option A)**

## Rationale

1. **Zero real overhead** - cJSON is already bundled with ESP-IDF. We're just enabling a component that ships with every ESP32 project. No additional binary size impact.

2. **Robustness** - Config parsing happens on every boot and when config changes. Using a battle-tested JSON library prevents subtle parsing bugs that would be hard to diagnose on deployed devices. Handles edge cases like escaping, whitespace variations, and malformed input.

3. **Maintainability** - 20 lines of cJSON API calls vs 200 lines of manual string parsing. When the config schema evolves, cJSON gracefully handles missing/extra fields with simple null checks.

4. **Fewer moving parts** - A custom format requires defining, documenting, and testing the format on both bridge and firmware sides. JSON is a known quantity.

5. **Debuggability** - JSON responses can be inspected with standard tools (curl, browser). Custom formats require custom tooling to debug.

## Implementation

```c
#include <cJSON.h>

// In CMakeLists.txt PRIV_REQUIRES:
//   json

// Parsing example:
cJSON *root = cJSON_Parse(response);
cJSON *name = cJSON_GetObjectItem(root, "name");
if (cJSON_IsString(name)) {
    strncpy(cfg->knob_name, name->valuestring, sizeof(cfg->knob_name) - 1);
}
cJSON_Delete(root);
```

## Consequences

- Firmware size: Negligible (cJSON is ~10KB, already compiled into ESP-IDF)
- Bridge: No changes required
- Testing: Standard JSON responses, easy to mock
