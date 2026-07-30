# Waveshare ESP32-S3-Knob-Touch-LCD-1.8 — Canonical Target Identity Record

**This file is the single canonical identity and provenance record for the shipping round
target.** Other documents must not independently restate product identity, panel type, panel
controller, colour depth, backlight, touch part, memory sizes, or encoder/button facts. They
link here instead and keep only the facts they genuinely own (pin tables, driver mechanics,
UI behaviour, porting steps).

Issue [#211](https://github.com/muness/roon-knob/issues/211) created this record. Decision
record: [2026-07-30_DECISION_TARGET_IDENTITY_PROVENANCE.md](../../meta/decisions/2026-07-30_DECISION_TARGET_IDENTITY_PROVENANCE.md).

This file has no blanket exemption from the regression checker. Its current-fact sections are
scanned exactly like every other document, so mutating a live row of the identity table below
fails CI. Only the [Historical claims](#historical-claims-now-superseded) section is exempt, and
only because restating superseded claims is that section's whole purpose.

---

## How to read this record

Every fact below carries a provenance class. Nothing is stated without one.

| Class | Meaning | What it does *not* mean |
|-------|---------|-------------------------|
| **V — Vendor-declared** | Waveshare's own product page, wiki, or shipped demo sources say it | That we have measured it on the owned device |
| **R — Repository-observed** | This repository's source code does it, and the file/line is cited | That the hardware requires it, or that it is optimal |
| **A — Artifact-observed** | Read out of an immutable published release binary | That the value is correct for the physical flash |
| **H — Historical** | A previously published claim in this repository, now superseded | That it was ever true |
| **U — Physically unverified** | Requires the owned device (`esptool`, visual inspection, meter) | That it is doubted — only that nobody here has looked |

**No claim in this file is based on physical inspection of an owned device.** Section
[Still requires physical inspection](#still-requires-physical-inspection) lists what is
therefore still open, and no `U` fact is treated anywhere as if it were verified.

---

## Identity summary

| Fact | Value | Class |
|------|-------|-------|
| Product | Waveshare **ESP32-S3-Knob-Touch-LCD-1.8** | V, R |
| Primary SoC | **ESP32-S3R8** (Xtensa LX7 dual-core, up to 240 MHz, 8 MB in-package PSRAM) | V |
| Secondary SoC | **ESP32-U4WDH** (4 MB in-package flash) | V |
| External flash | 16 MB | V |
| PSRAM | 8 MB | V |
| SRAM / ROM | 512 KB SRAM, 384 KB ROM | V |
| Panel technology | **IPS LCD** (the superseded panel-technology claim is listed under [Historical claims](#historical-claims-now-superseded)) | V |
| Panel size / resolution | 1.8 in round, 360 × 360 | V |
| Panel colour depth | **262 K colours** | V |
| Panel brightness / contrast | 600 cd/m², 1200:1 | V |
| Panel interface | QSPI | V, R |
| Panel driver IC | **ST77916** | V |
| Software panel driver used | `esp_lcd_sh8601` managed component | R |
| Backlight | **PWM-controlled, GPIO 47**; initial duty is `CONFIG_RK_BACKLIGHT_NORMAL`, Kconfig default 100 of 255 (≈ 39 %) | V, R |
| Touch controller | **CST816** (vendor-linked datasheet is `CST816D_datasheet_En_V1.3.pdf`) | V |
| Touch bus | I²C, 7-bit address `0x15` | V, R |
| Encoders | Vendor declares **two**; this firmware drives **one** (GPIO 8 / GPIO 7) | V, R |
| Encoder push switch | None wired as far as this firmware is concerned; shaft switch presence on the owned unit unconfirmed | R, U |
| Audio DAC | PCM5100A over I²S (unused by this firmware) | V |
| Haptics | DRV2605 over I²C | V |
| USB routing | CH445P analog switch shares one USB-C port between the two SoCs | V |

The Product row deliberately carries both **V** and **R**. Waveshare supplies the product
name; the repository binding is an inference from repository-observed convergence with that
product's shipped demo: the 185-entry project panel-init array is byte-for-byte identical to
the demo array, and the implemented QSPI/backlight mapping matches the vendor sources cited
below. This binds the repository target, not the physical revision of an owned unit. Revision
silkscreen and fitted-part markings remain **U** until #189 records them.

---

## Vendor-declared facts

Primary sources, both retrieved 2026-07-30:

- Product page — <https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm>
- Wiki — <https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8>
- Demo archive — <https://files.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8/ESP32-S3-Knob-Touch-LCD-1.8-Demo.zip>
  (68 704 748 bytes, `sha256:11e382444fe93470fbe463829c1e0ebad5bdb5115fd2d72f6159cd7700015030`)

### Display, quoted from the product page's own specification block

> display panel **IPS** · display size **1.8 inch** · resolution **360 × 360** ·
> display color **262K** · brightness **600cd/m²** · contrast ratio **1200:1** ·
> Communication interface **QSPI** · Driver IC **ST77916** · Touch Type **Capacitive** ·
> Touch IC **CST816**

The product page and the wiki also carry panel wording that does **not** belong to this board.
It appears in cross-sell tables for other Waveshare products and in tutorial screenshots reused
from a different product's wiki, and it is the most likely origin of the superseded identity
described under [Historical claims](#historical-claims-now-superseded).

The other product concerned, named here so the confusion is traceable:

<!-- rk-ident-allow-next-line: PANEL_AMOLED target="ESP32-S3-AMOLED-1.91" reason="truthful fact about a different Waveshare product whose wiki screenshots were reused on this product's page; naming it is what makes the origin of the superseded claim traceable" -->
- ESP32-S3-AMOLED-1.91 — a separate Waveshare product with its own panel technology and its own
  driver IC. Nothing on its wiki is a statement about the round knob board.

### Wiki "Onboard Resources", quoted

> 1. **ESP32-S3R8** — Wi-Fi and Bluetooth SoC, 240MHz operating frequency, with 8MB PSRAM
> 2. **ESP32-U4WDH** — Wi-Fi and classic Bluetooth, 240MHz operating frequency, with 4MB Flash
> 3. **PCM5100A** — High-performance stereo DAC using I2S interface
> 5. **16MB Flash** — Expanded 16MB Flash
> 6. **Dual encoder** — Acting on ESP32S3 and ESP32 respectively
> 8. **DRV2605** — Vibration motor driver, using I2C interface
> 9. **CH445P** — Four-pole double-throw 3.3V low resistance analog switch chip

The wiki also states "Built-in 512KB SRAM, 384KB ROM, 16MB Flash and 8MB PSRAM" and
describes the product as having "a built-in 1.8inch **LCD** screen".

The wiki's separately listed buttons are a **Power button** and an **ESP32-S3R8 BOOT
button**. The wiki does not describe an encoder shaft switch either way.

### Backlight, from the vendor's own demo sources

`ESP-IDF/08_LVGL_Test/main/user_config.h`:

```c
#define EXAMPLE_PIN_NUM_BK_LIGHT    (gpio_num_t)47
```

`ESP-IDF/08_LVGL_Test/components/lcd_bl_pwm_bsp/lcd_bl_pwm_bsp.c` configures an LEDC timer
at `.duty_resolution = LEDC_TIMER_8_BIT`, `.freq_hz = 50 * 1000` on that pin, and exposes
`setUpduty()`. The vendor demo also offers a `Backlight_Testing` build switch.

A vendor-supplied PWM backlight on GPIO 47 is consistent with an LCD, and inconsistent with the
superseded panel-technology claim listed under
[Historical claims](#historical-claims-now-superseded).

### Touch and display pins, from the vendor's `user_config.h`

`LCD_CS 14 · LCD_PCLK 13 · LCD_DATA0..3 15/16/17/18 · LCD_RST 21 · BK_LIGHT 47` ·
`TOUCH_ADDR 0x15 · TOUCH_RST 10 · TOUCH_INT 9 · SDA 11 · SCL 12` ·
`EXAMPLE_LCD_H_RES 360 · EXAMPLE_LCD_V_RES 360`.

### The vendor drives this LCD through `esp_lcd_sh8601` too

`ESP-IDF/08_LVGL_Test/main/idf_component.yml`:

```yaml
dependencies:
  esp_lcd_sh8601:
    version: '*'
    public: true
```

This is the decisive reason the component name is not a controller claim: Waveshare declares
a **ST77916** driver IC on the same page from which it ships a demo that binds the panel
through the **`esp_lcd_sh8601`** software component. See
[Panel controller versus software driver](#panel-controller-versus-software-driver).

---

## Repository-observed facts

| Fact | Evidence |
|------|----------|
| QSPI pins `CS 14 · PCLK 13 · D0-D3 15/16/17/18 · RST 21` | `idf_app/main/platform_display_idf.c:55` – `:61` |
| Backlight on GPIO 47 driven by LEDC PWM, `LEDC_TIMER_8_BIT` resolution at 5 kHz | `idf_app/main/platform_display_idf.c:62`, `:457` – `:464` |
| Initial backlight duty is `CONFIG_RK_BACKLIGHT_NORMAL`, whose Kconfig default is 100 of 255 (≈ 39 %) | `idf_app/main/platform_display_idf.c:471` sets `.duty = CONFIG_RK_BACKLIGHT_NORMAL`; `idf_app/main/Kconfig.projbuild:35` – `:41` declares `default 100`, `range 0 255`, help text "approximately 40% brightness"; [../../dev/KCONFIG.md](../../dev/KCONFIG.md) already documented 100 (~40 %) |
| The stale comment at `idf_app/main/platform_display_idf.c:456` says the initial backlight duty is 50 %. The channel configuration at `:471` reads the Kconfig value; no `128` literal exists in the file, and no code path implements that stale 50 % claim. Values, not comments, are the evidence for the row above | `idf_app/main/platform_display_idf.c:456` – `:474` |
| Panel bound via `esp_lcd_new_panel_sh8601()` with `bits_per_pixel = 16` and a project-supplied `init_cmds` array | `idf_app/main/platform_display_idf.c:496` – `:514` |
| Project init array is 185 entries, 146 distinct opcodes, ends with `{0x36, {0x00}, 1, 0}` | `idf_app/main/platform_display_idf.c:65` – `:251` |
| The comment at `idf_app/main/platform_display_idf.c:329` attributes the panel's big-endian RGB565 byte order to the SH8601-named component. It is **stale**: the byte swap proves the transport's byte order, not the physical controller identity | `idf_app/main/platform_display_idf.c:319` – `:336`; vendor/controller evidence in [Panel controller versus software driver](#panel-controller-versus-software-driver) |
| Exactly one encoder: channel A on GPIO 8, channel B on GPIO 7, 3 ms software-polled quadrature with 2-tick debounce | `idf_app/main/platform_input_idf.c:27` – `:28`, `:38` – `:39` |
| No physical button is read anywhere in the firmware | Derived from what the code does, not from any comment: `gpio_config()` is called exactly twice, at `idf_app/main/platform_input_idf.c:74` and `:84`, for `ENCODER_GPIO_A` and `ENCODER_GPIO_B` only; `gpio_get_level()` appears only at `:87` – `:88` and `:120` – `:121`, on those same two pins; no other GPIO is configured as an input or sampled anywhere in `idf_app/` or `common/` |
| The comment at `idf_app/main/platform_input_idf.c:31` says the device has no physical buttons. It is **stale**: the code proves only that this firmware reads no button; whether an encoder shaft switch is fitted remains physically unverified | `idf_app/main/platform_input_idf.c:27` – `:39`; repository-wide input-use derivation in the row above |
| Touch is a raw 7-byte register read at I²C `0x15`, not a CST816-specific component | `idf_app/components/lcd_touch_bsp/lcd_touch_bsp.c`, `idf_app/components/i2c_bsp/i2c_bsp.c` |
| The touch BSP is a whitespace-reformatted copy of Waveshare's `lcd_touch_bsp.c` — identical logic | diff against the demo archive's `ESP-IDF/08_LVGL_Test/components/lcd_touch_bsp/lcd_touch_bsp.c` |
| The panel consumes big-endian RGB565; the flush callback swaps each 16-bit pixel | `idf_app/main/platform_display_idf.c` flush callback; see [COLORTEST_HELLOWORLD.md](COLORTEST_HELLOWORLD.md) |
| Zone picker opens on a **touch click of the zone header**, and on no encoder input | `common/ui.c:389` registers `zone_label_event_cb` on `LV_EVENT_CLICKED`; that callback at `common/ui.c:542` – `:552` is the **only** emitter of `UI_INPUT_MENU`, handled at `common/bridge_client.c:954` – `:961`. The default zone label text is literally `"Tap here to select zone"` (`common/bridge_client.c:862`) |
| `esp_lcd_sh8601` is declared with `version: '*'` — unpinned | `idf_app/main/idf_component.yml` |

Managed-component pinning and a committed dependency lock are **not** decided here; that
acceptance criterion belongs to [#203](https://github.com/muness/roon-knob/issues/203).

---

## Panel controller versus software driver

The precise, supported statement — no more, no less:

1. **The project replaces the component's vendor-specific initialisation array.**
   `esp_lcd_sh8601` (2.0.1~1) declares its own three-command
   `vendor_specific_init_default[]` = `{0x44, {0x00,0xC8}}`, `{0x35, {0x00}}`,
   `{0x53, {0x20}}`. Because the project passes `vendor_config.init_cmds`, that default is
   **not** sent at all — `panel_sh8601_init()` selects one array or the other, never both.

2. **The component still sends standard DCS setup before the array.**
   `panel_sh8601_init()` transmits `LCD_CMD_MADCTL` (`0x36`) and then `LCD_CMD_COLMOD`
   (`0x3A`) from its cached values *before* iterating the chosen array. So the panel receives
   standard DCS setup regardless of what the project supplies.

3. **The project's array overwrites MADCTL.** Its final entry is `{0x36, {0x00}, 1, 0}`. The
   component detects this, logs *"The 36h command has been used and will be overwritten by
   external initialization sequence"*, updates its cached `madctl_val`, and sends the
   project's value. The project's MADCTL therefore wins.

4. **COLMOD remains governed by the panel config.** The project's array contains **no**
   `0x3A`. `colmod_val` stays whatever `esp_lcd_new_panel_sh8601()` derived from
   `panel_dev_config.bits_per_pixel`; at the project's `bits_per_pixel = 16` that is `0x55`
   (RGB565). Changing the pixel format is a panel-config edit, not an init-array edit.

### Structural comparison — reproducible

Measured 2026-07-30 with Python's `difflib` over the opcode sequences:

| Comparison | Result |
|---|---|
| Project array vs Waveshare demo `lcd_init_cmds[]`, default (`EXAMPLE_Rotate_90` undefined) branch | **Byte-for-byte identical**, 185/185 entries |
| Project array vs `esp_lcd_st77916` 2.0.2 `esp_lcd_st77916_spi.c` `vendor_specific_init_default[]` (216 entries) | opcode-sequence similarity **0.8928**; **179 of 185** project opcodes fall inside common blocks; longest equal run **175**; payloads identical at **84 of 179** (46.9 %) |
| Project array vs `esp_lcd_sh8601` 2.0.1~1 `vendor_specific_init_default[]` (3 entries) | opcode overlap **zero** |
| `esp_lcd_st77916` default vs `esp_lcd_sh8601` default | opcode overlap **zero** |

Reproduce. Prerequisites: network access, `bash`, Python 3, and the host tools `curl`,
`shasum` and `unzip`. Only the *analysis* is Python-3-standard-library-only — it imports
nothing but `difflib`, `os` and `re`. Edit the first line to point at your own checkout of
this repository; everything after it is copy-pasteable as written:

```bash
REPO=/path/to/roon-knob            # <-- your checkout of this repository
export REPO

mkdir -p /tmp/rk-panel && cd /tmp/rk-panel

curl -sSL -o sh8601.zip \
  'https://components-file.espressif.com/components/espressif/esp_lcd_sh8601/2.0.1~1/espressif__esp_lcd_sh8601-v2.0.1_1.zip'
curl -sSL -o st77916.zip \
  'https://components-file.espressif.com/components/espressif/esp_lcd_st77916/2.0.2/espressif__esp_lcd_st77916-v2.0.2.zip'
curl -sSL -A 'Mozilla/5.0' -o knobdemo.zip \
  'https://files.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8/ESP32-S3-Knob-Touch-LCD-1.8-Demo.zip'

shasum -a 256 sh8601.zip st77916.zip knobdemo.zip
# 0807c6439c1a91199f34dfd8fc6ff994964d8e2b589be06a2a19faeb317015ae  sh8601.zip
# e31f466ea9759eddbda3e89756f068e34f7f6ef1698925def508e7f8b2cc2696  st77916.zip
# 11e382444fe93470fbe463829c1e0ebad5bdb5115fd2d72f6159cd7700015030  knobdemo.zip

for z in sh8601 st77916 knobdemo; do unzip -qo "$z.zip" -d "$z"; done

python3 - <<'PY'
import difflib, os, re
def parse(path, name, typ):
    src = open(path, encoding='utf-8', errors='replace').read()
    body = re.search(r'static\s+const\s+%s\s+%s\s*\[\s*\]\s*=\s*\{(.*?)\n\}\s*;'
                     % (typ, name), src, re.S).group(1)
    return [(int(m.group(1), 16),
             tuple(int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]+', m.group(2))))
            for m in re.finditer(r'\{\s*(0x[0-9A-Fa-f]+)\s*,\s*\(uint8_t\s*\[\]\s*\)'
                                 r'\s*\{([^}]*)\}\s*,\s*(\d+)\s*,\s*(\d+)\s*\}', body)]
repo = os.environ['REPO']
P = parse(repo + '/idf_app/main/platform_display_idf.c', 'lcd_init_cmds', 'sh8601_lcd_init_cmd_t')
W = parse('knobdemo/ESP32-S3-Knob-Touch-LCD-1.8-Demo/ESP-IDF/08_LVGL_Test/main/main.c',
          'lcd_init_cmds', 'sh8601_lcd_init_cmd_t')
S = parse('st77916/esp_lcd_st77916_spi.c', 'vendor_specific_init_default', 'st77916_lcd_init_cmd_t')
H = parse('sh8601/esp_lcd_sh8601.c', 'vendor_specific_init_default', 'sh8601_lcd_init_cmd_t')
Wdef = [e for e in W if e != (0x36, (0x60,))]          # drop the EXAMPLE_Rotate_90 branch
ops = lambda e: [c for c, _ in e]
print('project entries          :', len(P))
print('project == vendor default:', P == Wdef)
sm = difflib.SequenceMatcher(a=ops(P), b=ops(S), autojunk=False)
eq = [(i1, i2, j1) for t, i1, i2, j1, _ in sm.get_opcodes() if t == 'equal']
n = sum(i2 - i1 for i1, i2, _ in eq)
same = sum(1 for i1, i2, j1 in eq for k in range(i2 - i1) if P[i1 + k][1] == S[j1 + k][1])
print('vs st77916 similarity    : %.4f' % sm.ratio())
print('matched opcodes / longest: %d of %d / %d' % (n, len(P), max(i2 - i1 for i1, i2, _ in eq)))
print('identical payloads       : %d of %d' % (same, n))
print('overlap with sh8601 dflt :', sorted(set(ops(P)) & set(ops(H))))
print('project has 0x36 / 0x3A  :', 0x36 in ops(P), '/', 0x3A in ops(P))
PY
```

`esp_lcd_sh8601` 2.0.1 and 2.0.1~1 differ only in the `commit_sha` and `version` fields of
`idf_component.yml`; the C source is identical, so the conclusion does not depend on which
of the two a build resolves.

### What this does and does not establish

**Supported.** The panel's initialisation sequence is the vendor's own sequence for this
exact product, and it is structurally an ST77916-family sequence: page-switched command sets
(`0xF0`/`0xF1`/`0xF2`/`0xF3`), 14-byte `0xE0`/`0xE1` gamma pairs, and the long
`0x60`–`0xD9` timing block, in Espressif's ST77916 order across a 175-entry unbroken run.
It shares **no** opcode with the SH8601 component's own default. That corroborates
Waveshare's ST77916 declaration and confirms that `esp_lcd_sh8601` is functioning here as a
QSPI command/DCS transport whose vendor-array hook the project fills with an ST77916
sequence.

**Not supported, and not claimed.** None of this reads the silicon. Sequence shape is
evidence about the command set a panel accepts, not proof of a die marking. Payload values
differ from Espressif's ST77916 default at 95 of 179 matched opcodes, which is ordinary
per-panel tuning but is also exactly the sort of difference that a related-but-distinct
controller would produce. The physical controller marking stays in
[Still requires physical inspection](#still-requires-physical-inspection). This record does
not describe `esp_lcd_sh8601` as the physical controller, and does not describe it as "merely
a transport shim" either — it performs real DCS setup, as item 2 above shows.

---

## Release-artifact-observed facts

Immutable v2.5.1 assets, published 2026-03-23T00:04:50Z:

| Asset | URL | Bytes | SHA-256 |
|---|---|---|---|
| `roon_knob.bin` | <https://github.com/muness/roon-knob/releases/download/v2.5.1/roon_knob.bin> | 1 753 184 | `1fd8de2bc1c105a7259775dfc52ab4ed8fd9bf57bb251f73d55f91aa9bc0f2c7` |
| `roon_knob_merged.bin` | <https://github.com/muness/roon-knob/releases/download/v2.5.1/roon_knob_merged.bin> | 1 818 720 | `3218db765e56ea0469e330a8a2d496ed0fbf9e17ac168d3b9ba01731b413251b` |

### Image headers — the flash-size disagreement

Byte 3 of an ESP image header packs SPI flash frequency in the low nibble and **flash size
in the high nibble** (`0x3` = 8 MB, `0x4` = 16 MB).

| Artifact | First four bytes | Segments | Flash-size nibble | Declared size |
|---|---|---|---|---|
| `roon_knob_merged.bin` (bootloader header at offset 0) | `e9 04 02 3f` | 4 | **`0x3`** | **8 MB** |
| `roon_knob.bin` (application header at offset 0) | `e9 07 02 4f` | 7 | **`0x4`** | **16 MB** |

Both carry `chip_id = 0x0009` (ESP32-S3) at offset 12 and SPI mode `0x02` (DIO).

The 8 MB value comes from `esptool ... merge-bin --flash-size 8MB` in the release workflow;
the 16 MB value comes from `CONFIG_ESPTOOLPY_FLASHSIZE="16MB"` in `idf_app/sdkconfig.defaults`.

**This record makes no geometry decision and recommends none.** It supplies the measurement
only. Which value is correct, and what to change, is
[#203](https://github.com/muness/roon-knob/issues/203)'s acceptance criterion, and #203's
16 MB direction in turn depends on physical flash verification.

### Application descriptor of `roon_knob.bin`

`esp_app_desc_t` begins at file offset 32, magic `0xABCD5432`.

| Field | Value |
|---|---|
| `magic_word` | `0xABCD5432` |
| `secure_version` | `0` |
| `version` | `2.5.1` |
| `project_name` | `roon_knob` |
| `time` | `00:02:47` |
| `date` | `Mar 23 2026` |
| `idf_ver` | `v5.4.3-1245-gbdc4dc5e9e` |
| `app_elf_sha256` | `882819856bd8e224d15209f945b8b0180bd1656976bb768d40aa4d5558c252e9` |
| `min_efuse_blk_rev_full` | `0` (encoded `major*100 + minor`, so v0.0) |
| `max_efuse_blk_rev_full` | `199` (v1.99 — the IDF default upper bound, not a measured device revision) |
| `mmu_page_size` | `16` — stored as log2, so 64 KiB |

### Reproduce

Prerequisites: network access, `bash`, and the host tools `curl`, `shasum`, `wc` and `xxd`.

```bash
mkdir -p /tmp/rk-v251 && cd /tmp/rk-v251
for f in roon_knob.bin roon_knob_merged.bin; do
  curl -sSL -o "$f" "https://github.com/muness/roon-knob/releases/download/v2.5.1/$f"
done
shasum -a 256 roon_knob.bin roon_knob_merged.bin
wc -c roon_knob.bin roon_knob_merged.bin

# flash-size nibble = high nibble of byte 3
for f in roon_knob_merged.bin roon_knob.bin; do
  printf '%s byte3=0x%s\n' "$f" "$(xxd -p -s 3 -l 1 "$f")"
done

xxd -l 24 roon_knob_merged.bin      # magic, segments, spi mode, byte3, entry, chip_id
xxd -l 24 roon_knob.bin
xxd -s 32 -l 224 roon_knob.bin      # application descriptor
```

A maintained tool reports the same fields. The spelling depends on which esptool generation is
installed, and the two forms must not be combined — the hyphenated command does not take
`--version`, and the underscored one is not the current name:

```bash
# esptool v5 (current): hyphenated command, no --version option
esptool image-info roon_knob.bin

# esptool v4 (legacy): underscored command, v2 layout must be requested explicitly
esptool.py image_info --version 2 roon_knob.bin
```

See Espressif's [esptool migration guide](https://docs.espressif.com/projects/esptool/en/latest/esp32/migration-guide.html)
for the full renaming. The `xxd` form above is given because it does not depend on which
esptool generation is present, not because it needs nothing installed.

---

## Historical claims, now superseded

These statements were published in this repository and are **incorrect for this board**.
They are recorded so the mistake is traceable and so the regression checker has a named
target list.

| Superseded claim | Where it appeared | Corrected value |
|---|---|---|
| Product is "Waveshare ESP32-S3 Touch AMOLED 1.8″" | this file's former title, `HARDWARE_PINS.md`, `COLORTEST_HELLOWORLD.md` | ESP32-S3-Knob-Touch-LCD-1.8 |
| Panel is a round AMOLED | this file, `HARDWARE_PINS.md`, `DISPLAY.md`, `DUAL_CHIP_ARCHITECTURE.md`, `docs/README.md` | IPS LCD |
| 16.7 M colours | this file | 262 K colours |
| "No PWM backlight control needed (AMOLED is self-emitting)" / "Backlight (always on ...)" | this file, `HARDWARE_PINS.md` | PWM backlight on GPIO 47; initial duty is `CONFIG_RK_BACKLIGHT_NORMAL`, Kconfig default 100 of 255 (≈ 39 %) |
| Backlight "started at 50 % duty" | this file's first draft, `HARDWARE_PINS.md`, `DISPLAY.md` | ≈ 39 % (100 of 255). The 50 % duty figure came from a stale comment at `platform_display_idf.c:456`, not from any value the code sets |
| SH8601 is the display controller | this file, `DISPLAY.md`, `HARDWARE_PINS.md`, `IMPLEMENTATION_NOTES.md`, `docs/README.md` | Vendor declares ST77916; `esp_lcd_sh8601` is the software component |
| Primary module is "ESP32-S3-WROOM-1 (R8)" | this file, `DUAL_CHIP_ARCHITECTURE.md` | Vendor declares the SoC as ESP32-S3R8; the module package is unverified |
| Secondary chip is "ESP32-WROOM" | `DUAL_CHIP_ARCHITECTURE.md` | Vendor declares ESP32-U4WDH with 4 MB flash |
| "Rotary encoder with push button" | `DEVELOPMENT.md` | No button is read by this firmware; shaft switch presence unconfirmed |
| "Press the knob → open zone picker" | `README.md` | The zone picker opens on a touch click of the zone name |
| "Tap the screen → play/pause" | `README.md` | Play/pause is the centre button of the transport row (`common/ui.c:477` – `:496`); a tap elsewhere does not toggle playback, and a double-tap enters art mode (`idf_app/main/platform_display_idf.c:422` – `:436`) |
| Touch part asserted as a specific variant ("CST816D", "CST816S") | `IMPLEMENTATION_NOTES.md`, `TOUCH_INPUT.md`, `cst816d.md` | Vendor declares the CST816 family; the fitted marking is unverified |

The superseded wordings quoted in this section — the panel technology, the colour-depth figure,
the component-as-controller claim, the module package, the knob press — are quotations, not
assertions. `scripts/check_docs_identity.py` exempts **this section only**, by heading, and
requires the section to keep restating something guarded: an allowance that stops suppressing
anything is reported as a structure failure rather than left as a silent hole.

---

## Still requires physical inspection

None of the following is treated as verified anywhere in this repository, and no document
may state them as fact without new evidence.

| Open question | Why it needs the device | Blocks |
|---|---|---|
| Exact flash chip part, die revision, and true capacity | Vendor "16MB Flash" is a marketing figure; only `esptool flash-id` / `flash_id` on the owned unit reads the actual part | #203's 16 MB direction |
| Physical panel controller marking | Sequence shape and vendor declaration corroborate ST77916 but cannot read a die | Porting advice precision; nothing releasable |
| Exact touch controller marking (CST816 vs CST816D vs CST816S vs CST816T) | Vendor declares the family; the linked datasheet is CST816D; earlier docs in this repository asserted CST816S; the project reads raw registers that several variants share | Nothing currently |
| ESP32-S3 module package and revision | Vendor names the SoC, not the module | #203, #193 profiles |
| Whether the encoder shaft has a push switch, wired or not | The firmware reads no button, which proves the firmware's behaviour, not the mechanism | Any future press gesture |
| Whether the second encoder is populated and reachable from the ESP32-S3 | Vendor says it acts on the secondary SoC; this firmware never reads it | #199 shared input work |
| PSRAM mode and timing as fitted | Vendor declares 8 MB octal-capable; effective config is a separate question | #203 |
| Board revision silkscreen of the owned unit | Cannot be inferred from any source used here | #193 inventory |

`#189` owns the release-blocking hardware checklist; this record does not discharge any of it.

---

## Related documents

Each of these owns its own subject matter and defers identity to this file.

- [HARDWARE_PINS.md](HARDWARE_PINS.md) — GPIO assignments as the firmware sets them
- [COLORTEST_HELLOWORLD.md](COLORTEST_HELLOWORLD.md) — RGB565 byte order and LVGL colour format
- [cst816d.md](cst816d.md) — touch controller integration notes
- [encoder.md](encoder.md) — quadrature decoding notes
- [battery.md](battery.md) — battery monitoring circuit
- [drv2605.md](drv2605.md) — haptic driver
- [../DISPLAY.md](../DISPLAY.md) — display subsystem and LVGL integration
- [../DUAL_CHIP_ARCHITECTURE.md](../DUAL_CHIP_ARCHITECTURE.md) — the two-SoC hardware and its unused secondary
- [../../howto/PORTING.md](../../howto/PORTING.md) — porting guidance
- [../../meta/decisions/2026-07-30_DECISION_TARGET_IDENTITY_PROVENANCE.md](../../meta/decisions/2026-07-30_DECISION_TARGET_IDENTITY_PROVENANCE.md) — why this record exists and how it is enforced
