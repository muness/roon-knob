# Prepared ESP-IDF runner

The four Linux ESP jobs use `esp-idf-5-5-5` from homelab-infra's builder catalog
when `LOCAL_LINUX_CI_ENABLED=true` and the event comes from this repository.
Fork PRs and disabled fleet routing retain the original Espressif Docker action.

The official ESP-IDF v5.5.5 image is the base of the maintained fleet runner recipe.
One prepared runner is published to the existing NAS registry and retained locally
by both fleet Docker hosts. Jobs source `/opt/esp/idf/export.sh` and run the original
build/verification command directly; they need no Docker socket or per-job image
pull. The image contains no roon-knob source, components, compiler cache or firmware.

`esp-profile.json` records jobs, versions and publisher; `esp-requirements.json`
declares compatibility. `esp-source.yml` records the unconverted workflow used
for this adoption. To prepare the runner using homelab-infra:

```sh
runner/builder/builder prepare-esp \
  --workflow /path/to/roon-knob/build/prepared-builders/esp-source.yml \
  --job build-idf --job build-frame --job build-rlcd --job build-ble-off \
  --esp-idf-version v5.5.5 \
  --tools-image espressif/idf@sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2 \
  --runner-version 2.337.0 \
  --runner-repository 192.168.1.36:5443/fleet/esp-idf-5-5-5 \
  --output /tmp/esp-runner
```

After tool checks/publication/catalog promotion, explicitly reuse the builder:

```sh
runner/builder/builder reuse esp-idf-5-5-5 --fleet \
  --repo muness/roon-knob \
  --workflow /path/to/roon-knob/build/prepared-builders/esp-source.yml \
  --job build-idf \
  --requirements /path/to/roon-knob/build/prepared-builders/esp-requirements.json \
  --output /tmp/esp-adoption
```

Feed each resulting `workflow.yml` to the next job conversion, then review/apply the
final workflow and generated broker/cache/access artifacts. Do not regenerate from
an obsolete source snapshot after unrelated workflow edits: update the canonical
unconverted source first and verify all non-adapted steps remain identical.

The deployed broker policy permits only this repository, with 4 CPUs/8 GiB per
job, and polls this exact repository. Existing approvals/leases are preserved.
Per-board Actions ccache and managed-components caches remain unchanged; mutable
build state is isolated per ephemeral job. Different hosted/fleet checkout paths
may cause first-run compiler-cache misses, so measure cold and warm runs separately.
This conversion does not change firmware code or optimized release settings and
does not establish physical-device correctness. See issue #267 for verification.
