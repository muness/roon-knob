# Connection ownership and recovery (#249, #253)

## Aim
Users reach playback and recover from ordinary address changes/outages without managing IP addresses. Web and knob Settings explain the exact remaining obstacle using one shared connection snapshot.

## Ownership
`controller_connection_t` owns evidence: selected name, current address, discovery observed, resolver used, mDNS lookup failure, API reachability, current/stale zones, selected-zone availability, last success and next attempt. `controller_connection_ready` is the sole usability predicate for play/pause, previous, next, volume, Settings and art-mode entry. The bridge worker drives I/O; copied snapshots cross the state lock. The old device-state enum, connected flag, zone-resolved flag, retry counters and compatibility getters are removed.

Wi-Fi availability is an input observation, not a separate definition of controller usability. Duplicate network-ready notifications do not reset readiness. Zone metadata can remain cached; it does not establish readiness without current API evidence. A missing saved zone is not silently replaced with another zone.

## Selection and persistence
DNS-SD hostname plus port identifies the chosen local service. Keep that name in the existing configuration URL; current numeric addresses live only in runtime state. This reuses existing storage and requires no new HTTP endpoints, protocol fields, or storage migration. DNS-SD naming provides continuity, not authentication or protection against a malicious LAN participant.

An empty automatic selection searches all returned records and refuses distinct competing services. An existing hostname scopes the search; an old numeric selection matches only a record advertising that same address and port. A valid zones response is required before persisting a discovered name. Manual edits win over both in-flight probes and queued writes through endpoint-generation tokens.

A legacy IP whose address changed before its identity was learned cannot be recovered unambiguously. Preserve it and allow explicit selection rather than silently choosing another bridge. A hostname change, rather than just an IP change, likewise requires explicit selection. This is the accepted compatibility limit of the existing identity format.

## Recovery and presentation
Resolution tries mDNS then ordinary DNS; a literal IP needs neither. A returned service record without an address is recorded as discovered, unresolved. API response/schema validation, zone availability, and selected-zone availability are separate. Last success is timestamped; readiness expires after two minutes without successful evidence.

Local recovery starts immediately, then backs off 5/15/30/60 seconds, capped at one minute. Stable operation checks the selected service every minute while retaining ordinary playback polling. Failed playback cannot continually reset backoff merely because the zones probe succeeds. Address changes do not write flash repeatedly; a stable selected name produces no configuration writes. Reboot starts a new bounded local attempt sequence; these are local LAN requests, not HiPhi Cloud traffic.

## Evidence and risk retirement
- Runtime harness executes the actual bridge recovery/commit functions with controlled network/config I/O: unresolved discovery then recovery; invalid API response; zero zones; restart and changed IP; no repeated probe before deadline; manual URL preservation; manual edit during I/O and before queued persistence.
- Selection tests reject unrelated numeric responders, preserve a named service when response order changes, recognize discovered-without-address, migrate matching legacy IPs, and distinguish multiple installations from repeated interfaces.
- Connection tests cover empty zones, missing selected zone, readiness, failure/stale evidence, selection reset and capped retries.
- Command tests require the same readiness gate for previous/next/play-pause/volume. The runtime volume regression reproduces the formerly split read-versus-control state.
- Both Settings implementations call the same summary/details formatter; neither derives connectivity from retry counters. Status refresh preserves editing controls. Config owner tests preserve concurrent manual edits and degraded-write handling.

## Review and dissent
The selected approach survives the two alternatives: merely showing more labels would retain competing command gates; merely storing IPs would require manual recovery after DHCP changes. No global rediscovery may substitute another selected service. Model-checkable risks are covered by the tests above; naming continuity is not a unique installation identifier and the compatibility limits above remain explicit.

Exact-artifact hardware validation is still required: flash/boot Dial and Frame, reproduce the reported volume failure, compare web/knob Settings, test HA/QNAP discovery, induce an outage and an address change, and verify UI readability, retained settings and normal playback. Host tests and compilation do not establish those physical results. Keep PR #250 draft until that evidence is recorded.

All ESP-IDF targets compile one mDNS adapter (`common/platform/platform_mdns_esp.c`). Product identity comes from the platform identity provider (with the existing RLCD product override preserved); discovery, candidate selection, and hostname resolution do not vary by board.

### Diagnosing discovery on hardware

Serial `platform_mdns` messages report each PTR query's service, timeout, saved
selection and initialization state; the query error or empty-result outcome;
each returned service's hostname, port and TTL; each advertised IPv4 endpoint;
and ignored addresses/records with reasons. The final line reports record and
address counts, selected identity/endpoint and ambiguity. A queries and DNS
fallback failures are reported separately. Logging follows the existing bounded
retry schedule; it does not add network queries.

A PTR service response can include SRV, TXT and A/AAAA address records. UHC's
`mdns_sd` advertisement already supplies these standard additional records;
there is no separate IP-list TXT protocol. On 2026-09-13 a Mac-side packet check
of NAS2 observed `_roonknob._tcp.local`, `NAS2.local:8088`, and A `192.168.1.2`
in one response. This verifies the server response on the Mac's network path,
not receipt on the Dial's Wi-Fi path.

The recovery screen uses the controller's own current IP for manual setup, never
the bridge address. With no local IP it directs the user to Connection settings.
Initial search becomes “Cannot find your bridge” after an unsuccessful attempt;
automatic retries continue. The startup zone label never displays the persisted
provider zone ID as a user-facing name.

Discovery attempts have a derived prerequisite lifecycle:
`WAIT_NETWORK -> WAIT_DISCOVERY_INIT -> WAIT_RETRY -> ATTEMPT_READY`.
Transitions skip prerequisites already satisfied. Waiting consumes no failure or
retry delay; a manually configured IPv4 endpoint does not depend on mDNS.
The platform adapter retains a defensive initialization check as well.

For a local receive/parser diagnostic build, run
`python3 scripts/instrument_mdns_receive.py` after the Dial's managed components
are installed, then rebuild. This explicitly patches the local managed mDNS
sources; it is not part of CI/release builds. It traces the first 256 packets at
each layer: UDP receipt/source/size, interface and queue disposition, parser
entry and record-to-query matching. Reinstall the managed mDNS component to
remove it. The script rejects unexpected source layouts and is idempotent.
Timing-sensitive failures can change under logging; compare good/bad attempts
before concluding that a packet was lost on Wi-Fi rather than in software.
