# Decision: Frame display recovery and retained maintenance evidence

Status: implementation proposed in a draft PR; exact-artifact physical validation
remains a separate gate. Scope: #318, #320, #322 and UHC #906.

## Evidence and selected approach

The physical PhotoPainter accepted changed artwork identities and downloaded
complete 180000-byte frames while showing an image unchanged for days. Serial
showed an AXP2101 probe failure and a panel BUSY timeout. A full power cycle
restored PMIC telemetry and a subsequent refresh. This supports a power/bus
readiness failure; it does not prove one exclusive electrical cause.

Keep the existing firmware and bridge boundary. Recover the I2C bus before PMIC
initialization, bound probe retries and read back required voltage/enable
settings before panel operations. Do not continue a refresh after a failed
PMIC, SPI or BUSY check. Require the refresh command to make BUSY assert before
accepting its later deassertion, so a disconnected pulled-up line is not success.

Record requested, downloaded and last successfully refreshed identities
separately. A failed physical refresh keeps cached artwork pending through
backoff and leaves the last successful identity alone. `/display-health` exposes
this boot's phases, failures and durations; its PMIC verification is register
readback, not a voltage measurement. No software handshake proves optical image
quality. Keep networking and the maintenance portal available when panel power
is unavailable; preserve the physical e-ink image rather than clearing it.

The shared next-boot crash collector retains a versioned, checksummed NVS
summary. A dump's ELF identity is recorded independently from the firmware
version that captured it. Failed summary creation, persistence or readback must
leave the raw dump intact. Normal boots do not replace the last crash record.
The shared report appears under the existing power diagnostics, without adding
maintenance routes on every target.

Only Frame initially opts into a flash core-dump partition. Other targets can
retain panic/watchdog reset summaries, and the collector supports dump summaries
when their target layout deliberately opts in. ESP-IDF 6 flash dumps are ELF;
older supported SDKs additionally gate on their selected ELF format. RISC-V
must not pretend its stack dump is an on-device backtrace.

## Preservation and delivery

NVS, factory and OTA slot offsets are unchanged. The new Frame coredump region
is at 0x790000, size 0x20000, after all application slots and within the existing
16 MB configuration. An application OTA cannot add that partition: the collector
detects older tables and retains reset-only evidence. A USB flash of the exact
CI partition table is needed for full panic dumps. Preserve NVS during flashing;
do not erase the device to add diagnostics.

The preferred renderer lives in UHC: keep zenresize ahead of stock measured
palette/tone mapping/float diffusion. Remove the old renderer. Calibration was
explicitly excluded by the maintainer; no calibration UI or API is introduced.

## Alternatives and dissent

A refresh trigger or album-change workaround cannot fix the observed PMIC/BUSY
failures. Replacing the entire donor firmware would discard proven controller,
BLE and provisioning behavior. Copying its panel driver verbatim would preserve
the same timeout-as-success defect. Blind PMIC power cycling could interrupt
ESP/system rails; this implementation does not do that. The donor stock palette
is an adopted default, not a per-unit color measurement.

## Verification boundary

Host checks inject stuck SDA/SCL, missing PMIC, failed register writes/readback,
BUSY timeout, missing BUSY acknowledgement, SPI failure, and recovery. Crash tests
exercise corruption, summary/storage/readback failure and an older partition
layout. Actual HTTP handler and browser checks cover stale successful identity,
mobile/desktop layout and offline state. Builds and stack/dependency checks are
necessary but do not retire exact-artifact flash, sustained operation, display,
BLE/input, crash-capture and repeated wake/recovery hardware checks. No merge or
release is authorized by this decision record.
