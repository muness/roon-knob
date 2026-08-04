# Decision: Migrate HiPhi Dial Identity by Compatibility Boundary

**Date:** 2026-07-30
**Status:** Accepted
**Issue:** [#162](https://github.com/muness/roon-knob/issues/162)

## Context

The shipping round controller is now named **HiPhi Dial**, but the former Roon
Knob name appears in several different kinds of identity:

- user-facing product copy;
- provisioning SSID, hostname, logs, and mDNS TXT metadata;
- CMake application and firmware filenames;
- bridge-selected OTA assets;
- `_roonknob._tcp` discovery protocol;
- repository and hosted-flasher URLs;
- source directory names;
- historical release and analysis records.

Changing all of them as text in one operation would break active consumers and
rewrite truthful provenance.

## Decision

Migrate each identity according to its own compatibility boundary.

| Surface | Primary after #162 | Compatibility policy | Owner |
| --- | --- | --- | --- |
| Product copy | HiPhi Dial | No legacy copy in active UI | #162 |
| Dial slug / default hostname | `hiphi-dial` | Stored custom `knob_name` remains unchanged | #162 |
| Provisioning SSID | `hiphi-dial-setup` | Upgraded AP-mode devices show matching new UI copy | #162 |
| Dial application | `hiphi_dial.bin` | Publish byte-identical `roon_knob.bin` alias | #162, then UHC #277 |
| Merged web image | `hiphi_dial_merged.bin` | Publish byte-identical `roon_knob_merged.bin` alias | #162 |
| Discovery service | `_roonknob._tcp` | No change until dual migration | #163 |
| Device-info TXT product | `hiphi-dial` | No repository consumer found; `_device-info._udp` remains | #162 |
| Repository/domain | Existing `muness/roon-knob` and `roon-knob.muness.com` | Keep until redirects, CNAME, host checks, and previews move together | #164 |
| Source directories | `idf_app/`, `frame_app/` | No change in #162 | #174 |
| Persistent configuration | Existing `rk_cfg` / `cfg` / `rk_cfg_t` / `knob_name` | No schema or key rename | Not scheduled; unnecessary |
| Historical records | Original name where historically true | Preserve provenance | Permanent |

The primary web manifest and current product documentation use the new name.
Legacy binary URLs remain available as generated aliases. Alias retirement
requires cross-repository evidence from
`open-horizon-labs/unified-hifi-control#277` and a separate explicit release
decision; it is not inferred from elapsed time alone.

## Options Considered

### Visible strings only

Rejected because owners would still download and flash old-named artifacts for
a newly named device, leaving the repository internally contradictory.

### Global textual rename

Rejected because it would break discovery, OTA/release consumers, hosted Pages,
and repository URLs, while falsifying historical evidence.

### General runtime product registry

Deferred to target-profile work. Dial and Frame are compile-time targets today;
a generalized runtime registry would expand #162 without evidence that it solves
the next target problem.

## Consequences

- Release and Pages storage temporarily contain both primary and compatibility
  firmware filenames.
- CI must prove aliases are byte-identical and that the current manifest points
  to the primary image.
- Old repository/domain strings remain visible in explicitly classified
  endpoint locations until #164.
- `_roonknob._tcp` remains visible until #163; it must never be treated as a
  missed product rename.
- Contributors must distinguish active branding from compatibility and
  provenance rather than relying on a repository-wide zero-occurrence rule.
