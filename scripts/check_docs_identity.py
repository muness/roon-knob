#!/usr/bin/env python3
"""Phrase tripwire for the known round-target identity contradictions.

Issue #211. Offline, standard library only, no network, no repository writes.

Two jobs:

1.  Scan Markdown across the repository for the specific identity contradictions
    that #211 corrected: wrong product name, AMOLED panel, 16.7M/24-bit colour
    depth, absent or always-on backlight, the stale 50 % backlight-duty figure,
    SH8601 named as the panel's driver IC, module-package claims, an encoder push
    button, and an unqualified touch-variant part.

2.  Assert the canonical record itself still has its provenance structure, still
    carries the corrected values, and is still linked from every document that
    would otherwise be tempted to restate identity.

What this check is, and what it is not
--------------------------------------

Every rule below is a regular expression over normalised prose.  This is a
**phrase tripwire**, not a semantic or factual check.  Its demonstrated recall,
for the single-physical-line renderings in the fixtures, is exactly two things:

*   the wordings that were actually present in this repository before #211, each
    pinned by a rule probe and a fixture; and
*   the paraphrases listed in ``scripts/fixtures/docs_identity/paraphrases.tsv``,
    which are authored independently of the rule probes so that "the rule matches
    its own example" cannot be mistaken for recall.

The paraphrases are independently authored but **fitted**, not held out: the
rules were widened until all 15 were reported.  The wrap figures below therefore
measure sensitivity on fitted data, not general recall on unseen wording.

Outside those two sets the check makes **no claim**.  Matching is per physical
line: breaking a guarded phrase across lines can make it pass, and the
``CONTROLLER_CLAIM``, ``BACKLIGHT_DUTY`` and ``ENCODER_BUTTON`` rules have no
single-token clause that survives every such break.  A sufficiently different
sentence will also pass: prose that never names the guarded token ("the screen
makes its own light"), a colour depth given only as a hex range, a claim carried
in an image, a diagram, a translated spec table, or a file this scan does not read.

A reproducible audit estimate applies Python ``textwrap.wrap`` to each of the 15
paraphrases at every starting column, using initial indentation to represent that
column, then tests its designated rule against each physical output line.  At 88
columns, 120 of 1,320 offsets (9.1 %) are missed: ``BACKLIGHT_DUTY`` 20/88,
``CONTROLLER_CLAIM`` 49/264, ``ENCODER_BUTTON`` 40/264, ``COLOR_DEPTH`` 7/176,
``PANEL_AMOLED`` 4/88, and zero for the other rules.  At 100 columns the same 120
misses are 8.0 % of 1,500 offsets.  This is a sensitivity baseline, not a recall
guarantee; #213 owns the formal audit and any mechanism change.
So a relapse is *not* guaranteed to become a CI failure -- only a relapse in a
known or tested wording is.  Human review of identity claims is still required;
this check exists to make the cheap relapses loud and to make any widening of the
exemptions a reviewed change to this file rather than a documentation edit.

**The rule count is not a coverage number.**  Each rule below is an alternation of
several clauses, and the per-rule fixture proves that *one* clause fires -- not
that every clause does.  Nothing here claims that the number of rules equals the
number of covered alternation branches, and no such equality is asserted or
tested anywhere.  Individual clauses are pinned only where a paraphrase in the
corpus happens to exercise them; an unexercised clause is untested, and adding a
clause does not add evidence of recall.

Scan scope -- stated as mechanisms, because counts drift
-------------------------------------------------------

*   Every file whose name ends in ``.md``, at any depth below the root, is
    scanned.  That includes ``.github/**/*.md``: ``.github/RELEASE_TEMPLATE.md``
    ships release prose that describes the target, so it is documentation for
    this purpose.
*   There is **no** dot-directory rule.  A directory is skipped only when its
    exact name appears in ``SKIP_DIRS`` (version control, interpreter caches,
    build output, vendored dependencies), so a future ``.notes/`` or ``.docs/``
    is scanned rather than silently exempt.
*   ``SKIP_RELPATHS`` skips the fixture corpus by path, because that corpus
    exists to contain the guarded contradictions.
*   ``SKIP_FILES`` skips bot-managed files by exact root-level path.

Within a scanned file, two constructs are *not* prose and are therefore not
scanned.  Both of them can hide the rest of a document, so both fail loudly
rather than absorbing it:

*   **Fenced code.**  Fences are matched CommonMark-shaped: an opener is a run of
    at least three backticks or tildes indented no more than three spaces, and a
    closer must use the *same character*, be *at least as long* as the opener, and
    carry nothing but whitespace after it.  So a backtick fence cannot be closed
    by tildes, a three-backtick line cannot close a four-backtick opener, and a
    ```` ```code``` ```` span on its own line is inline code rather than a fence.
    Everything between opener and closer is exempt -- quoting the guarded wording
    as source is legitimate.  An opener that is never closed leaves the tail of
    the file unscanned, so it is reported as a ``STRUCTURE`` failure naming the
    file and the opening line.
*   **HTML comments.**  Comment bodies are stripped before scanning, spanning
    lines when the comment does.  A ``<!--`` with no matching ``-->`` would
    swallow every following line, so it too is reported as a ``STRUCTURE``
    failure naming the file and the opening line.

Neither case can end in ``RK-IDENT-OK``: a malformed fence or comment is a
structural finding whose exit code outranks a contradiction, precisely because it
is the state in which a contradiction cannot be seen.

**Indented code is not one of those two constructs: it is scanned as prose.**  A
four-space indented block is code to CommonMark, and it is deliberately *not*
exempt here.  Recognising it would mean tracking list and blockquote context to
tell an indented code block from a continuation line, and an indentation-only
exemption would exempt the reflowed middle of a paragraph -- including every
indented rendering the wrap audit above uses to represent a starting column.  So a
quotation of source that is indented rather than fenced *is* reported as a claim,
and the remedy is to fence it.  Because that is the one exemption a reader expects
and does not get, ``QUOTING_NOTE`` is printed with the violation lines of every
report that has any (``--quiet`` gates it with them), carried in the JSON report,
and rendered in the CI step summary, so the state is discoverable from the output
rather than only from here.
``scripts/fixtures/docs_identity/indented_code_scanned.md`` pins it.

Source anchors -- freshness of a citation, not proof of a fact
--------------------------------------------------------------

The canonical record's ``Repository-observed`` rows are claims *about this
repository's source*, each citing a file.  Prose rules cannot notice when the
cited source moves out from under the prose: the documentation stays word for
word correct-looking while the thing it was read from is gone.  ``SOURCE_ANCHORS``
pins the stable, load-bearing, positively quotable ones as explicit
(file, expected tokens) assertions -- the QSPI pin mapping and its bus/panel-IO
bindings, the GPIO 47 backlight pin with its LEDC channel, the
``CONFIG_RK_BACKLIGHT_NORMAL`` duty assignment and the Kconfig stanza behind it,
the init array's declaration, its ``0x36`` terminator and the ``vendor_config``
binding that carries it, the ``esp_lcd_new_panel_sh8601`` call and the
``bits_per_pixel`` of the config it is given, the encoder A/B pin definitions with
the ``gpio_config``/``gpio_get_level`` sites that use them, the ``0x15`` touch
address and the 7-byte register read, the unpinned ``esp_lcd_sh8601`` dependency,
and the zone picker's click registration, emitter, handler and default label
literal.  A missing file or a missing token is a named ``STRUCTURE`` finding,
because it is a state in which the record's citation can no longer be checked.

**Tokens must carry their own relationship.**  A token is matched
whitespace-insensitively (every run of whitespace in the token matches any run in
the file, so realignment and reflow do not break it) and with word-boundary
guards at each end (so ``0x15`` does not match ``0x152`` and ``GPIO_NUM_8`` does
not match ``GPIO_NUM_80``).  That is what makes it safe to pin whole lines and
whole blocks, and pinning them is the point: a *split* pair of tokens such as
``PIN_NUM_BK_LIGHT`` and ``((gpio_num_t)47)`` is satisfied by a file in which the
name and the number have drifted apart onto different lines, so it would report a
fresh citation for a relationship that no longer exists.  Every token therefore
states the relationship it is cited for -- ``#define PIN_NUM_BK_LIGHT
((gpio_num_t)47)``, not the two halves separately -- and ``--self-check`` rejects a
token that is a bare identifier or bare literal for exactly that reason.

**What an anchor does and does not establish.**  A present token proves only that
the cited text is still in the cited file.  It is *not* evidence that the physical
board behaves that way, that the value is correct, or that the surrounding claim
is true -- board.md's provenance classes exist precisely because source text and
silicon are different kinds of evidence.  Conversely, a *missing* token means the
citation has drifted and must be re-read; it is **not** a finding that the claim is
false.  The finding wording says so, and the remedy is to re-derive the row from
the source, not to delete the row.

**Anchors do not validate the line numbers a row cites.**  The rows cite lines --
``platform_display_idf.c:55`` -- ``:61`` -- and an anchor checks *text*, never a
line number.  Insert forty lines above an anchored block and every token is still
present, every anchor still reports fresh, and every ``:NN`` in the Markdown is now
wrong.  Pure line movement is therefore *invisible* to this layer by construction.
Deliberately so: asserting line numbers here would turn every unrelated edit above
a cited line into a red documentation guard, which is the brittleness that gets a
check deleted.  Keeping the cited lines correct is human-review work, tracked in
`#213 <https://github.com/muness/roon-knob/issues/213>`_.

**The anchor count is not a coverage number.**  ``SOURCE_ANCHORS`` covers a
deliberate *subset* of the repository-observed rows.  ``anchors_fresh=N/N`` means
every anchor in the table is fresh -- it does **not** mean every repository-observed
row is anchored, and no equality between the two is asserted anywhere.  Rows stay
unanchored for two reasons.  First, **negative** rows: "no physical button is read
anywhere in the firmware" and "no 128 literal exists in the file and no code path
implements the stale 50 % claim" are absence claims, and no token assertion can
establish an absence -- a token's
presence would prove nothing about everywhere else, and its absence would prove
nothing at all.  Uniqueness rows ("the *only* emitter of ``UI_INPUT_MENU``") are
absence claims too.  Second, **derived or physical** rows: the 185-entry and
146-distinct-opcode counts come from reading the array, the demo-archive diff and
the ``difflib`` similarity figures come from files outside this repository, and no
row about the silicon is anchorable at all.  Those stay derived by human reading.

Rule-level suppressions are observable, not silent
--------------------------------------------------

Some rules carry an ``unless`` suppressor so that truthful qualified prose passes:
"RGB888 is a 24-bit pixel format" is not a claim about the panel, and "the
datasheet Waveshare links describes the CST816D" is not a claim about the fitted
part.  Every hit a suppressor swallows is *recorded* -- with file, line, rule id,
the matched text and the suppressor text that excused it -- and surfaced in the
JSON report (``suppressions``), in the summary counters (``suppressed=``), in an
informational ``RK-IDENT-SUPPRESSED`` line per hit, and in the CI step summary.
Suppressions do not change the exit code: a run whose only findings are
suppressions is still ``RK-IDENT-OK`` and still exit 0.  Their visibility is the
point -- a widened suppressor shows up as a growing suppression list in review
instead of as a quietly shrinking guard.  ``--self-check`` replays each rule's
``probe_exempt`` through the scanner and fails if the suppressed hit is not
recorded, so removing that observability cannot pass silently.

Exemptions are deliberately narrow, because a wide one is how the original
contradiction survived nine files of review:

*   ``SECTION_ALLOWANCES`` names individual *sections* of individual files where
    restating a superseded claim is the whole point of the prose.  There is no
    file-level *marker* opt-out -- no comment a document carries exempts that
    document -- and the canonical record's own current-fact sections are scanned
    like any other document, so mutating a live row in its identity table fails
    the check.  The one file-level skip is ``SKIP_FILES`` below, which is
    hard-coded here rather than assertable from a document.
*   ``rk-ident-allow-next-line`` covers exactly one following prose line, must
    name the guarded rule, must name the *other* target the line is about, and
    must carry a reason.  It cannot be used for the shipping target, cannot
    cover a line that does not mention the declared other target, cannot be
    stacked, and is reported if it stops suppressing anything.
*   ``SKIP_FILES`` names bot-managed files, currently only ``CHANGELOG.md``.

Exit contract -- every "could not determine" state is non-zero and named:

    0   RK-IDENT-OK           no guarded contradiction found in a known wording
    1   RK-IDENT-VIOLATION    a guarded contradiction is present
    2   RK-IDENT-STRUCTURE    canonical record, links, allowances or a source anchor
                              degraded, or a document's comment/fence structure hid
                              part of itself
    3   RK-IDENT-MISSING      a required file is absent, or nothing was scanned
    4   RK-IDENT-SELFCHECK    the checker's own rules failed self-verification
    64  RK-IDENT-USAGE        bad invocation

``RK-IDENT-SUPPRESSED`` is informational only and has no exit code of its own.

Precedence is MISSING > SELFCHECK > STRUCTURE > VIOLATION.  Outranked tokens are
still printed, so a structure failure never hides a contradiction.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

TOOL = "check_docs_identity"
VERSION = "2.6.0"

EXIT_OK = 0
EXIT_VIOLATION = 1
EXIT_STRUCTURE = 2
EXIT_MISSING = 3
EXIT_SELFCHECK = 4
EXIT_USAGE = 64

TOKEN_OK = "RK-IDENT-OK"
TOKEN_VIOLATION = "RK-IDENT-VIOLATION"
TOKEN_STRUCTURE = "RK-IDENT-STRUCTURE"
TOKEN_MISSING = "RK-IDENT-MISSING"
TOKEN_SELFCHECK = "RK-IDENT-SELFCHECK"
TOKEN_USAGE = "RK-IDENT-USAGE"
TOKEN_MARKER_ABUSE = "RK-IDENT-MARKER-ABUSE"
# Informational only: a suppression is a hit a rule's `unless` clause excused. It
# is printed and reported so the excuse is visible, and it never changes the exit
# code -- see the module docstring.
TOKEN_SUPPRESSED = "RK-IDENT-SUPPRESSED"

# Printed with every violation report, carried in the JSON report and rendered in
# the CI step summary. A four-space indented block is CommonMark code but is
# scanned here as prose -- see the module docstring -- so it is the one exemption a
# reader expects and does not get. The remedy travels with the finding instead of
# living only in documentation nobody reads while staring at a red run.
QUOTING_NOTE = (
    "quote source in a fenced code block (``` ... ```) if it is a quotation, not a "
    "claim: a four-space indented block is scanned as prose, not exempted as code, "
    "so indented source is reported here"
)

CANONICAL = "docs/esp/hw-reference/board.md"
ADR = "docs/meta/decisions/2026-07-30_DECISION_TARGET_IDENTITY_PROVENANCE.md"
DOCS_INDEX = "docs/README.md"

# The shipping target. An other-target allowance may not name it.
SHIPPING_TARGET = "ESP32-S3-Knob-Touch-LCD-1.8"

# Retired: the first cut of this checker exempted whole files via this marker,
# which meant the canonical record's own current facts were never scanned. The
# marker is now a violation wherever it appears, so the old escape cannot come
# back by copy-paste.
RETIRED_MARKER = "rk-ident-allow-file"

ALLOW_NEXT_NAME = "rk-ident-allow-next-line"
MIN_REASON_CHARS = 20

# Directories never scanned, by exact name at any depth.  This list is the *only*
# directory exclusion: `find_markdown` has no dot-directory rule, so `.github` is
# scanned like any other directory and a future `.notes/` cannot become exempt by
# accident.  Everything named here is either version control, an interpreter or
# tool cache, build output, or a vendored dependency tree -- never authored prose.
SKIP_DIRS = frozenset(
    {
        # version control
        ".git",
        # interpreter and tool caches
        "__pycache__",
        ".mypy_cache",
        ".pytest_cache",
        ".ruff_cache",
        ".venv",
        "venv",
        # build output
        "build",
        # vendored dependencies
        "managed_components",
        "node_modules",
        "dependencies",
    }
)

# Directories that must stay scannable.  `.github` held identity prose in
# `RELEASE_TEMPLATE.md` while an earlier cut of this checker excluded every
# dot-directory, so the documented scope and the real scope disagreed.  Self-check
# refuses to let that return.
MUST_SCAN_DIRS = (".github",)

# Directories never scanned, by path relative to the root.  The fixture corpus
# deliberately contains the guarded contradictions -- that is what it is for --
# so scanning it would make the check permanently red.
SKIP_RELPATHS = ("scripts/fixtures",)

# Bot-managed files, skipped by exact repo-relative path.
#
# CHANGELOG.md: `.github/workflows/changelog.yml` fires on `release: published`,
# writes `github.event.release.body` verbatim into CHANGELOG.md, and commits it
# straight to master. A release body is quoted history written by a bot, not a
# documentation claim about the board, and a violation arriving that way could
# not be fixed by a documentation edit -- master would simply be red. Named per
# file, root-level only (self-check enforces the absence of a path separator),
# and the fixture suite asserts that neither `docs/CHANGELOG.md` nor any
# similarly named file inherits the skip.
SKIP_FILES = ("CHANGELOG.md",)

# Documents that carry identity-adjacent subject matter and must therefore point
# at the canonical record instead of restating it.
REQUIRE_CANONICAL_LINK = (
    "README.md",
    DOCS_INDEX,
    "docs/dev/DEVELOPMENT.md",
    "docs/dev/IMPLEMENTATION_NOTES.md",
    "docs/esp/DISPLAY.md",
    "docs/esp/DUAL_CHIP_ARCHITECTURE.md",
    "docs/esp/ROTARY_ENCODER.md",
    "docs/esp/TOUCH_INPUT.md",
    "docs/esp/hw-reference/COLORTEST_HELLOWORLD.md",
    "docs/esp/hw-reference/HARDWARE_PINS.md",
    "docs/esp/hw-reference/RGB565_IMPLEMENTATION.md",
    "docs/esp/hw-reference/cst816d.md",
    "docs/esp/hw-reference/encoder.md",
    "docs/esp/hw-reference/touch.md",
    "docs/howto/PORTING.md",
)

# Headings the canonical record must keep, in this order, so its provenance
# separation cannot be flattened back into an undifferentiated spec sheet and so
# the one allowed historical section cannot be widened by deleting the heading
# that ends it.
CANONICAL_SECTIONS = (
    "## How to read this record",
    "## Identity summary",
    "## Vendor-declared facts",
    "## Repository-observed facts",
    "## Panel controller versus software driver",
    "## Release-artifact-observed facts",
    "## Historical claims, now superseded",
    "## Still requires physical inspection",
)

# Sections of the canonical record that state what is true *now*. These are
# scanned for contradictions like any other prose; self-check asserts none of
# them is ever granted a section allowance.
CANONICAL_CURRENT_FACT_SECTIONS = (
    "## Identity summary",
    "## Vendor-declared facts",
    "## Repository-observed facts",
    "## Panel controller versus software driver",
    "## Release-artifact-observed facts",
    "## Still requires physical inspection",
)

# Section-scoped allowances: file -> headings whose prose may restate superseded
# claims because recording them is the section's purpose. Anything outside these
# exact sections in these exact files is scanned.
SECTION_ALLOWANCES = {
    CANONICAL: ("## Historical claims, now superseded",),
    ADR: ("## Context",),
}

# A section allowance may only be granted to a section whose declared job is
# historical narration. Self-check pins the vocabulary so a future edit cannot
# quietly move an allowance onto a current-fact section.
ALLOWED_SECTION_WHITELIST = frozenset(
    {
        "## Historical claims, now superseded",
        "## Context",
    }
)

# The five provenance classes must all remain named in the legend.
CANONICAL_CLASSES = (
    "Vendor-declared",
    "Repository-observed",
    "Artifact-observed",
    "Historical",
    "Physically unverified",
)

# Corrected values whose disappearance would mean the record was gutted rather
# than merely reworded.
CANONICAL_VALUES = (
    "ESP32-S3-Knob-Touch-LCD-1.8",
    "ESP32-S3R8",
    "ESP32-U4WDH",
    "ST77916",
    "esp_lcd_sh8601",
    "262",
    "IPS LCD",
    "GPIO 47",
    "CONFIG_RK_BACKLIGHT_NORMAL",
)


# A token is matched whitespace-insensitively and with word-boundary guards. See
# the module docstring, "Tokens must carry their own relationship": the point of
# both is that a whole line or whole block can be pinned as one token, so the token
# states the *relationship* it is cited for instead of leaving it to a pair of
# independently-satisfiable fragments.
_TOKEN_WORD = re.compile(r"[0-9A-Za-z_]")

# A token that is only an identifier or only a literal cannot state a relationship,
# and is the shape that produces a split-token false positive. `--self-check`
# refuses it.
_BARE_TOKEN = re.compile(r"\A(?:[A-Za-z_][0-9A-Za-z_]*|0[xX][0-9A-Fa-f]+|[0-9]+)\Z")


def anchor_pattern(token):
    """Compile one expected token, or return ``None`` if it holds no text.

    Whitespace in the token matches any run of whitespace in the file, so the
    token survives realignment and reflow and may span lines -- which is what
    makes a multi-line block usable as a single relationship-preserving token.

    The gap between two of the token's whitespace-separated parts is matched
    *precisely* as follows, because "whitespace-insensitive" alone is ambiguous
    about whether a gap may close entirely:

    *   ``\\s+`` -- whitespace is **required** -- when the character before the
        gap and the character after it are both word characters
        (``[0-9A-Za-z_]``).  Dropping it there would fuse two identifiers or
        literals into one, so ``#define PIN_NUM_LCD_CS`` must never be satisfied
        by ``#definePIN_NUM_LCD_CS``.
    *   ``\\s*`` -- whitespace is **optional** -- when either side of the gap is
        punctuation.  A formatter may legitimately close such a gap, so
        ``SH8601_PANEL_IO_QSPI_CONFIG( PIN_NUM_LCD_CS,`` matches both the wrapped
        call site it was written from and a reflowed
        ``SH8601_PANEL_IO_QSPI_CONFIG(PIN_NUM_LCD_CS,``.  Without this, an
        untouched, perfectly accurate citation reports ``missing-token`` purely
        because someone unwrapped an argument list.

    Word-boundary guards at each end stop a token from matching a longer
    identifier or literal that merely starts with it (``0x15`` in ``0x152``).

    ``None`` means the token collapsed to nothing.  The matcher treats that as a
    miss rather than as a pattern that matches everywhere, so a degenerate token
    fails loudly; ``--self-check`` also names it.
    """
    parts = token.split()
    if not parts:
        return None
    pieces = [re.escape(parts[0])]
    for previous, part in zip(parts, parts[1:]):
        both_word = _TOKEN_WORD.match(previous[-1]) and _TOKEN_WORD.match(part[0])
        pieces.append((r"\s+" if both_word else r"\s*") + re.escape(part))
    body = "".join(pieces)
    lead = r"(?<![0-9A-Za-z_])" if _TOKEN_WORD.match(token.lstrip()[:1] or " ") else ""
    trail = r"(?![0-9A-Za-z_])" if _TOKEN_WORD.match(token.rstrip()[-1:] or " ") else ""
    return re.compile(lead + body + trail)


class SourceAnchor:
    """One repository-observed claim, pinned to the source text it was read from.

    ``path`` is repo-relative.  Each of ``tokens`` must still be found in it, by
    ``anchor_pattern`` -- whitespace-insensitively, word-boundary guarded, and
    written so the token itself carries the relationship the row cites.  ``claim``
    is the canonical-record row the anchor supports, quoted in the finding so a
    reader knows what to re-derive.

    The assertion is deliberately weak and deliberately explicit: it proves the
    citation is still fresh, not that the fact is true; it says nothing about the
    line numbers the row cites; and its failure means "re-read the source", not
    "the claim is false".  See the module docstring for why no anchor may encode a
    negative, uniqueness or absence claim, and why the anchor count is not a
    coverage figure.
    """

    __slots__ = ("id", "path", "tokens", "claim", "patterns")

    def __init__(self, anchor_id, path, tokens, claim):
        self.id = anchor_id
        self.path = path
        self.tokens = tuple(tokens)
        self.claim = claim
        self.patterns = tuple(anchor_pattern(token) for token in self.tokens)


# Appended to every anchor finding. The one thing a reader is most likely to
# assume this layer covers is the one thing it cannot cover.
ANCHOR_CAVEAT = (
    "note: an anchor checks text, never the line numbers the row cites, so pure "
    "line movement leaves a citation stale with every token still present -- that "
    "is human-review work tracked in #213"
)

# Printed in the JSON report so the coverage claim travels with the numbers.
ANCHOR_SCOPE = (
    "SOURCE_ANCHORS covers a deliberate subset of the canonical record's "
    "repository-observed rows: the stable, positively quotable ones. "
    "anchors_fresh=N/N means every anchor in the table is fresh -- NOT that every "
    "repository-observed row is anchored. Negative and uniqueness rows cannot be "
    "anchored (a token cannot establish an absence), derived counts and "
    "outside-the-repository diffs are not quotable text, and no row about the "
    "silicon is anchorable at all. No anchor validates the line numbers a row "
    "cites; keeping those correct is human review, tracked in #213."
)


# Stable, load-bearing, positively quotable source text behind the canonical
# record's `Repository-observed facts` table. Every token states the relationship
# it is cited for, as a whole line or whole block; see `anchor_pattern`.
SOURCE_ANCHORS = (
    SourceAnchor(
        "QSPI_PINS",
        "idf_app/main/platform_display_idf.c",
        (
            "#define PIN_NUM_LCD_CS ((gpio_num_t)14)",
            "#define PIN_NUM_LCD_PCLK ((gpio_num_t)13)",
            "#define PIN_NUM_LCD_DATA0 ((gpio_num_t)15)",
            "#define PIN_NUM_LCD_DATA1 ((gpio_num_t)16)",
            "#define PIN_NUM_LCD_DATA2 ((gpio_num_t)17)",
            "#define PIN_NUM_LCD_DATA3 ((gpio_num_t)18)",
            "#define PIN_NUM_LCD_RST ((gpio_num_t)21)",
            # The defines alone would be satisfied by seven dead macros, so the
            # sites that consume them are anchored too.
            ".sclk_io_num = PIN_NUM_LCD_PCLK,",
            ".data0_io_num = PIN_NUM_LCD_DATA0,",
            "SH8601_PANEL_IO_QSPI_CONFIG( PIN_NUM_LCD_CS,",
            ".reset_gpio_num = PIN_NUM_LCD_RST,",
            ".use_qspi_interface = 1,",
        ),
        "the QSPI pins are CS 14, PCLK 13, D0-D3 15/16/17/18 and RST 21, and those "
        "defines are what the SPI bus, the QSPI panel IO and the panel reset are "
        "configured with",
    ),
    SourceAnchor(
        "BACKLIGHT_GPIO47",
        "idf_app/main/platform_display_idf.c",
        (
            "#define PIN_NUM_BK_LIGHT ((gpio_num_t)47)",
            ".gpio_num = PIN_NUM_BK_LIGHT,",
            ".duty_resolution = LEDC_TIMER_8_BIT,",
            ".freq_hz = 5000,",
            # The resolution and frequency belong to the timer, the pin to the
            # channel. Without both halves of the timer<->channel link the anchor
            # would attribute one struct's values to the other struct's pin.
            ".timer_num = LEDC_TIMER_0,",
            ".timer_sel = LEDC_TIMER_0,",
        ),
        "the backlight is on GPIO 47 and is driven by an LEDC channel on that pin "
        "whose timer runs at 8-bit resolution and 5 kHz",
    ),
    SourceAnchor(
        "BACKLIGHT_DUTY_ASSIGNMENT",
        "idf_app/main/platform_display_idf.c",
        (".duty = CONFIG_RK_BACKLIGHT_NORMAL,",),
        "the initial backlight duty is the Kconfig value, not the figure in the stale comment",
    ),
    SourceAnchor(
        "BACKLIGHT_KCONFIG_DEFAULT",
        "idf_app/main/Kconfig.projbuild",
        # One block, not three tokens: `default 100` and `range 0 255` also appear
        # under sibling symbols, so only the stanza proves which symbol they belong
        # to.
        (
            "config RK_BACKLIGHT_NORMAL"
            ' int "Normal backlight brightness (0-255)"'
            " default 100"
            " range 0 255",
        ),
        "CONFIG_RK_BACKLIGHT_NORMAL itself declares default 100 within range 0 255",
    ),
    SourceAnchor(
        "PANEL_INIT_ARRAY",
        "idf_app/main/platform_display_idf.c",
        (
            "static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {",
            # Terminator: the 0x36 entry immediately followed by the array's
            # closing brace, which is what makes it the *last* entry.
            "{0x36, (uint8_t[]){0x00}, 1, 0}, };",
            ".init_cmds = lcd_init_cmds,",
            ".init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),",
            ".vendor_config = &vendor_config,",
        ),
        "a project-supplied lcd_init_cmds array is declared, its final entry before "
        "the closing brace is {0x36, {0x00}, 1, 0}, and it is handed to the panel "
        "through vendor_config (the 185-entry and 146-distinct-opcode counts are "
        "derived by reading the array, not anchored here)",
    ),
    SourceAnchor(
        "PANEL_COMPONENT_BINDING",
        "idf_app/main/platform_display_idf.c",
        (
            "const esp_lcd_panel_dev_config_t panel_config = {",
            ".bits_per_pixel = 16,",
            "esp_lcd_new_panel_sh8601(s_io_handle, &panel_config, &s_panel_handle)",
        ),
        "the panel handle is created by esp_lcd_new_panel_sh8601 from panel_config, "
        "the one panel dev config in this file, which sets bits_per_pixel = 16",
    ),
    SourceAnchor(
        "ENCODER_PINS",
        "idf_app/main/platform_input_idf.c",
        (
            "#define ENCODER_GPIO_A GPIO_NUM_8",
            "#define ENCODER_GPIO_B GPIO_NUM_7",
            ".pin_bit_mask = (1ULL << ENCODER_GPIO_A),",
            ".pin_bit_mask = (1ULL << ENCODER_GPIO_B),",
            "gpio_get_level(ENCODER_GPIO_A)",
            "gpio_get_level(ENCODER_GPIO_B)",
        ),
        "encoder channel A is GPIO 8 and channel B is GPIO 7, and both are configured "
        "as inputs and sampled by gpio_get_level (this anchors the two pins that ARE "
        "read; the row's claim that no other GPIO is read is an absence claim and is "
        "not anchored, because no token can establish one)",
    ),
    SourceAnchor(
        "TOUCH_I2C_ADDRESS",
        "idf_app/components/i2c_bsp/settings.h",
        ("#define TOUCH_ADDR 0x15",),
        "the touch controller is addressed at I2C 0x15",
    ),
    SourceAnchor(
        "TOUCH_DEVICE_BINDING",
        "idf_app/components/i2c_bsp/i2c_bsp.c",
        ("dev_cfg.device_address = TOUCH_ADDR;",),
        "TOUCH_ADDR is the address the touch device handle is opened with",
    ),
    SourceAnchor(
        "TOUCH_SEVEN_BYTE_READ",
        "idf_app/components/lcd_touch_bsp/lcd_touch_bsp.c",
        (
            "uint8_t data[7] = {0};",
            "i2c_read_buff(disp_touch_dev_handle, 0x00, data, 7);",
        ),
        "touch state is a raw 7-byte register read from register 0x00 rather than a "
        "CST816-specific component",
    ),
    SourceAnchor(
        "DRIVER_VERSION_UNPINNED",
        "idf_app/main/idf_component.yml",
        # One block: `version: '*'` on its own would be satisfied by any dependency
        # in the file becoming unpinned, which is a different fact.
        ("esp_lcd_sh8601: version: '*' public: true",),
        "the esp_lcd_sh8601 dependency itself is declared unpinned, as version '*'",
    ),
    SourceAnchor(
        "ZONE_PICKER_TOUCH_ENTRY",
        "common/ui.c",
        (
            "lv_obj_add_event_cb(header, zone_label_event_cb, LV_EVENT_CLICKED, NULL);",
            "static void zone_label_event_cb(lv_event_t *e) {",
            "s_input_cb(UI_INPUT_MENU);",
        ),
        "the zone header registers zone_label_event_cb for LV_EVENT_CLICKED and that "
        "callback emits UI_INPUT_MENU (the row's claim that it is the *only* emitter, "
        "and that no encoder input opens the picker, are uniqueness and absence claims "
        "and are not anchored)",
    ),
    SourceAnchor(
        "ZONE_PICKER_HANDLER",
        "common/bridge_client.c",
        (
            "if (event == UI_INPUT_MENU) {",
            'cfg->zone_id : "Tap here to select zone",',
        ),
        "bridge_client handles UI_INPUT_MENU, and the default zone label is literally "
        '"Tap here to select zone" when no zone id is configured',
    ),
)


class Rule:
    """One guarded contradiction.

    ``probe`` must match the pattern and ``antiprobe`` must not.  A rule with an
    ``unless`` suppressor must also supply ``probe_exempt``: a line that matches
    the pattern *and* the suppressor.  ``--self-check`` asserts all of it, plus
    that the suppressor does not swallow the rule's own probe, so neither half of
    a rule can rot into a no-op unnoticed.
    """

    def __init__(self, rule_id, pattern, message, probe, antiprobe, unless=None, probe_exempt=None):
        self.id = rule_id
        self.pattern = re.compile(pattern, re.IGNORECASE)
        self.message = message
        self.probe = probe
        self.antiprobe = antiprobe
        self.unless = re.compile(unless, re.IGNORECASE) if unless else None
        self.probe_exempt = probe_exempt


RULES = (
    Rule(
        "PRODUCT_NAME",
        # Governance trigger: revise or retire before a second shipping target is
        # documented; this rule guards today's target, not permanent product policy.
        r"\btouch[\s_-]*amoled\b",
        "wrong product name; the target is the ESP32-S3-Knob-Touch-LCD-1.8",
        probe='the Waveshare ESP32-S3 Touch AMOLED 1.8" board',
        antiprobe="the Waveshare ESP32-S3-Knob-Touch-LCD-1.8 board",
    ),
    Rule(
        "PANEL_AMOLED",
        # Governance trigger: revise or retire before a second shipping target is
        # documented; a future target may truthfully carry this panel technology.
        # The acronym, plus the two spelled-out forms of it. Spelling it out was a
        # tested way past the acronym-only rule.
        r"(\bamoled\b"
        r"|\bactive[\s-]*matrix\s+organic\s+(light[\s-]*emitting\s+diode|led)"
        r"|\borganic\s+light[\s-]*emitting\s+diode\s+(panel|display|screen)"
        r"|\borganic\s+led\s+(panel|display|screen))",
        "the panel is an IPS LCD, not an AMOLED; see the canonical record",
        probe="360x360 round AMOLED over QSPI",
        antiprobe="360x360 round IPS LCD over QSPI",
    ),
    Rule(
        "COLOR_DEPTH",
        # 16.7M, its digit expansion, and the bit-depth form that implies it. The
        # bit-depth clause is scoped to colour words so that a legitimate note
        # about a 24-bit pixel format can say so and be exempted below.
        r"(16\.7\s*(m\b|million)"
        r"|\b16[,\s]?777[,\s]?216\b"
        r"|\b24[\s-]*bit\s+(true[\s-]*)?colou?r"
        r"|\bcolou?r\s+depth\b[^.\n]{0,24}\b24[\s-]*bit\b"
        r"|\btrue[\s-]*colou?r\s+(panel|display|screen)\b)",
        "vendor declares 262K colours (18-bit), not 16.7M / 24-bit",
        probe="1.8 inch round display, 360x360, 16.7M colors",
        antiprobe="1.8 inch round display, 360x360, 262K colors",
        # RGB888 really is a 24-bit pixel format, and the repository discusses
        # pixel formats. Talking about the format is not a claim about the panel.
        unless=r"(rgb888|rgb24|pixel\s+format|framebuffer\s+format|superseded|historical)",
        probe_exempt="RGB888 is a 24-bit colour pixel format; the panel still declares 262K colours",
    ),
    Rule(
        "NO_BACKLIGHT",
        # "no backlight" in its common qualifier forms, the self-illumination
        # claims that imply it, and the "so there is nothing to dim" corollary.
        r"(\bno\s+(?:\w+\s+){0,2}backlight\b"
        r"|backlight[^.\n]{0,40}always\s+on"
        r"|\bself[-\s]*(emitting|emissive|lit|illuminated|illuminating)\b"
        r"|backlight\s+control\s+(is\s+)?not\s+needed"
        r"|\bno\s+(?:\w+\s+){0,2}(dimming|brightness)\s+(control|adjustment)"
        r"|\bno\s+dimming\b"
        r"|brightness\s+is\s+fixed"
        r"|(dimming|brightness)[^.\n]{0,30}\b(is\s+)?not\s+(possible|supported|available)\b)",
        "there is a PWM backlight on GPIO 47; the firmware drives it",
        probe="No PWM backlight control needed (AMOLED is self-emitting)",
        antiprobe="Backlight, LEDC PWM, 8-bit duty on GPIO 47",
    ),
    Rule(
        "BACKLIGHT_DUTY",
        # The figure as a percent sign, as words, and spelled "per cent".
        r"((\b50\s*%|\b50\s+per\s?cent\b|\bfifty[\s-]*per\s?cent\b)[^.\n]{0,40}(duty|brightness)"
        r"|(duty|brightness)[^.\n]{0,40}(\b50\s*%|\b50\s+per\s?cent\b|\bfifty[\s-]*per\s?cent\b))",
        "the initial backlight duty is CONFIG_RK_BACKLIGHT_NORMAL, whose Kconfig default is "
        "100/255 (about 39%); the 50% figure is a stale source comment",
        probe="Backlight on GPIO 47, started at 50 % duty",
        antiprobe="Backlight on GPIO 47, initial duty CONFIG_RK_BACKLIGHT_NORMAL (100/255)",
        unless=r"\b(stale|superseded|historical|comment)\b",
        probe_exempt=(
            "The stale comment says the backlight starts at 50 % duty; "
            "the configured value is 100/255."
        ),
    ),
    Rule(
        "CONTROLLER_CLAIM",
        # SH8601 asserted as the physical part: as a driver/panel IC (either word
        # order, with room for an intervening clause), as silicon/a die, or
        # attributed to Sitronix.
        #
        # The long-distance clauses anchor on `\bsh8601`, which cannot match inside
        # `esp_lcd_sh8601` (no word boundary after an underscore).  Naming the
        # software component in prose is the corrected wording; a bare SH8601 near
        # "driver IC" is the assertion.  The two original short-distance clauses
        # keep their unanchored form, so `esp_lcd_sh8601 controller` still trips.
        r"(sh8601[\s_-]*(amoled|lcd\b|display\s+controller|controller\b)"
        r"|\bsh8601[^.\n]{0,60}\b(display|panel|driver)\s+(ic|controller)\b"
        r"|\b(display|panel|driver)\s+(ic|controller)\b[^.\n]{0,60}\bsh8601"
        r"|\bsh8601[^.\n]{0,30}\b(silicon|die|part\s+number)\b"
        r"|\b(silicon|die|part\s+number)\b[^.\n]{0,30}\bsh8601"
        r"|\bsh8601[^.\n]{0,40}\bsitronix\b"
        r"|\bsitronix\b[^.\n]{0,40}\bsh8601"
        r"|sh8601[^.\n]{0,24}expects)",
        "SH8601 is the software component name; the vendor declares an ST77916 driver IC",
        probe="| Display controller | SH8601 | QSPI |",
        antiprobe="| Panel driver component | esp_lcd_sh8601 | QSPI |",
    ),
    Rule(
        "MODULE_CLAIM",
        # WROOM with any or no separators, and the bare WROOM-1 package name, so
        # "ESP32S3 WROOM 1" and "ESP32-S3 WROOM1" are not free passes.
        r"(\besp32[\s_-]*(s3)?[\s_-]*wroom(?:[\s_-]*\d+)?\b"
        r"|\bwroom[\s_-]*1\b)",
        "module package is unverified; name the vendor-declared SoC (ESP32-S3R8 / ESP32-U4WDH)",
        probe="ESP32-S3 | ESP32-S3-WROOM-1 (R8) | BLE 5.0 only",
        antiprobe="Primary | ESP32-S3R8 (8 MB PSRAM) | BLE 5.0 only",
    ),
    Rule(
        "ENCODER_BUTTON",
        # Governance trigger: revise or retire before firmware gains a real press
        # gesture; this rule guards a currently phantom input, not future policy.
        # The original push-button wordings, plus press/click/push of the knob or
        # dial in either word order, plus a shaft switch given an active verb.
        r"(encoder\s+with\s+(a\s+)?push[\s_-]*button"
        r"|\b(press|click|push|depress)(es|ed|ing)?\s+(?:in\s+)?(?:the\s+|a\s+|your\s+)?(knob|dial)\b"
        r"|\b(knob|dial)\s+(press|click|push)(es|ed)?\b"
        r"|\bencoder\s+press\b"
        r"|encoder\s+(shaft\s+)?is\s+pressable"
        r"|\bshaft\s+switch\b[^.\n]{0,30}\b(select|selects|open|opens|confirm|confirms"
        r"|toggle|toggles|trigger|triggers|acts|is\s+wired|is\s+read)\b)",
        "no firmware reads a physical button; a shaft switch is physically unverified",
        probe="| **Press the knob** | Open zone picker |",
        antiprobe="| **Tap the zone name** | Open zone picker |",
        # A sentence that says the mechanism is unconfirmed, or that the firmware
        # reads no button, is the corrected wording -- not a relapse.
        unless=r"(unconfirmed|unverified|reads\s+no\s|no\s+button\s+is\s+read|superseded)",
        probe_exempt="Whether the encoder shaft is pressable at all is unconfirmed on the owned unit",
    ),
    Rule(
        "TOUCH_VARIANT",
        # A variant-suffixed part number is itself the assertion, so the bare form
        # is guarded; "CST816" with no suffix stays free, because the family name
        # is the corrected wording.
        r"\bcst816\s*[dst]\b",
        "the fitted touch variant is unverified; the vendor declares the CST816 family, so say "
        "CST816 family unless the sentence is explicitly about a datasheet, marking or variant",
        probe="Implementation: CST816D via I2C, integrated with LVGL",
        antiprobe="Implementation: CST816-family part via I2C, integrated with LVGL",
        # Datasheet, marking and variant discussion is legitimate: the vendor
        # really does link a CST816D datasheet, and the open question is named in
        # the canonical record's physical-inspection list.
        unless=r"(datasheet|marking|variant|family|unverified|superseded|cst816 vs)",
        probe_exempt="The datasheet Waveshare links describes the CST816D touch controller registers",
    ),
)


# --------------------------------------------------------------------------- #
# text normalisation
# --------------------------------------------------------------------------- #

# A candidate fence line: up to three spaces of indent, then a run of at least
# three backticks or tildes, then the info string (opener) or trailing text
# (closer). Whether it opens, closes or is merely content is decided by `Fence`,
# because a `^\s{0,3}(```|~~~)` toggle let tildes close backticks and let a short
# run close a long one -- and either way the rest of the file went unscanned.
_FENCE_LINE = re.compile(r"^ {0,3}(?P<run>`{3,}|~{3,})(?P<info>.*)$")
_INLINE_CODE = re.compile(r"`([^`]*)`")
_LINK_TARGET = re.compile(r"\]\([^)]*\)")
_AUTOLINK = re.compile(r"<[^>\s]*>")
_HTML_COMMENT_OPEN = re.compile(r"<!--")
_HTML_COMMENT_CLOSE = re.compile(r"-->")
_HEADING = re.compile(r"^(#{1,6})\s")

# A Markdown link whose *label* is a filename is a cross-reference, not a claim:
# `[cst816d.md](cst816d.md)` names a document, it does not assert a fitted part.
# The link target is already stripped; this strips the label in that one shape.
_LINK_LABEL_FILE = re.compile(
    r"\[\s*[\w./-]+\.(?:c|h|cpp|hpp|py|sh|md|ya?ml|json|txt|pdf|zip|bin|htm|html)\s*\]"
)

# A backtick span counts as quoted source -- and so as something other than an
# assertion -- only when it carries a positive code signal. `esp_lcd_sh8601`,
# `0x36`, `common/ui.c:389` and `board.md` are code. `AMOLED`, `SH8601`,
# `16.7 M` and `ESP32-S3-WROOM-1` are product claims wearing backticks: their
# contents stay in the prose stream so the rules still see them.
_CODE_SIGNAL = re.compile(
    r"(_"
    r"|\(\)"
    r"|->|::"
    r"|[{};=]"
    r"|^0x[0-9A-Fa-f]"
    r"|/"
    r"|\.(?:c|h|cpp|hpp|py|sh|md|ya?ml|json|txt|pdf|zip|bin|htm|html)\b)"
)

# One or more rule ids, comma-separated: a single line can legitimately trip more
# than one rule (a product whose name contains the panel technology trips both),
# and stacking two markers is not allowed. Every id listed must actually be
# suppressed, so the list cannot be padded.
_ALLOW_NEXT = re.compile(
    r"^\s*<!--\s*" + re.escape(ALLOW_NEXT_NAME) + r":\s*"
    r"(?P<rules>[A-Z][A-Z0-9_]*(?:\s*,\s*[A-Z][A-Z0-9_]*)*)\s+"
    r"target=\"(?P<target>[^\"]+)\"\s+"
    r"reason=\"(?P<reason>[^\"]+)\"\s*-->\s*$"
)

# Both markers only count inside an HTML comment, which is the only form that
# could ever have silenced the guard. Prose *about* the markers -- this file's own
# documentation, and the decision record's explanation of them -- is ordinary
# text and must not be reported.
_RETIRED_MARKER_USE = re.compile(r"<!--[^\n]*" + re.escape(RETIRED_MARKER))
_ALLOW_NEXT_USE = re.compile(r"<!--[^\n]*" + re.escape(ALLOW_NEXT_NAME))


def _despan(match):
    body = match.group(1)
    return " " if _CODE_SIGNAL.search(body) else " %s " % body


def normalise(line):
    """Strip code spans that are really code, link targets and autolinks."""
    line = _INLINE_CODE.sub(_despan, line)
    # Targets first, then filename labels: stripping the label would otherwise
    # destroy the `](` that identifies the target.
    line = _LINK_TARGET.sub("](-)", line)
    line = _LINK_LABEL_FILE.sub("[-]", line)
    line = _AUTOLINK.sub(" ", line)
    return line


UNTERMINATED_FENCE = "unterminated code fence"
UNTERMINATED_COMMENT = "unterminated HTML comment"


class Fence:
    """CommonMark-shaped fenced-code tracking.

    A fence closes only on the *same* character, with a run *at least as long* as
    the opener, and with nothing but whitespace after it.  The first cut of this
    checker toggled a boolean on any ``^\\s{0,3}(```|~~~)`` line, so a backtick
    fence could be "closed" by tildes and a three-backtick line could "close" a
    four-backtick opener.  Either mistake leaves the tracker's idea of the file out
    of step with Markdown's, and the practical effect is the same in both
    directions: a stretch of real prose is treated as code and never scanned.

    Valid Markdown behaviour is preserved: an opener may carry an info string, a
    longer closer is accepted, tilde fences may contain backtick fences and vice
    versa, and a ```` ```code``` ```` line -- three backticks whose info string
    itself contains a backtick -- is inline code, not a fence.

    ``lineno`` is the line of an opener that is still open, so an unterminated
    fence can be reported against the line that opened it.
    """

    __slots__ = ("char", "length", "lineno")

    def __init__(self):
        self.char = None
        self.length = 0
        self.lineno = 0

    @property
    def is_open(self):
        return self.char is not None

    def feed(self, lineno, raw):
        """Consume one line; return True when it is fence syntax, not prose."""
        match = _FENCE_LINE.match(raw)
        if match is None:
            return False
        run = match.group("run")
        info = match.group("info")
        if self.char is None:
            if run[0] == "`" and "`" in info:
                # Not a fence at all: a backtick fence's info string may not
                # contain a backtick, so this is an inline code span.
                return False
            self.char = run[0]
            self.length = len(run)
            self.lineno = lineno
            return True
        if run[0] == self.char and len(run) >= self.length and not info.strip():
            self.char = None
            self.length = 0
            self.lineno = 0
            return True
        # Same shape, wrong character or too short, or carrying trailing text:
        # Markdown treats this as content of the open fence, and so do we. The
        # opener stays open, so if nothing ever closes it the caller reports it.
        return False


class Allowance:
    __slots__ = ("lineno", "rules", "target", "reason", "covers", "problem", "used")

    def __init__(self, lineno, rules, target, reason):
        self.lineno = lineno
        self.rules = rules
        self.target = target
        self.reason = reason
        self.covers = None
        self.problem = None
        self.used = set()


def analyse(text):
    """Split a document into prose records, allowances and retired markers.

    Returns ``(records, allowances, retired, malformed, defects)`` where
    ``records`` is a list of ``(lineno, prose, section)`` triples.  ``section`` is
    the nearest enclosing ``##`` heading (``###`` and deeper inherit it, ``#``
    resets it), so a section allowance covers a section and not a whole file.

    ``defects`` holds ``(lineno, message)`` pairs for structural states in which
    the scan could not see the whole document: a code fence or an HTML comment
    that is opened and never closed.  Both hide every following line, so they are
    surfaced as structure failures rather than silently absorbing the tail.
    """
    records = []
    allowances = []
    retired = []
    malformed = []
    defects = []

    known_rules = {rule.id for rule in RULES}
    section = None
    fence = Fence()
    in_comment = False
    comment_lineno = 0
    pending = None

    for lineno, raw in enumerate(text.splitlines(), start=1):
        stripped = raw.strip()

        # Fence syntax is only fence syntax outside a comment: a `<!--` that is
        # still open owns every following line until it closes.
        if not in_comment:
            if fence.feed(lineno, raw):
                if pending is not None:
                    pending.problem = "allowance is followed by a code fence, not by prose"
                    pending = None
                continue
            if fence.is_open:
                continue

            if _RETIRED_MARKER_USE.search(raw):
                retired.append(lineno)
            if _ALLOW_NEXT_USE.search(raw):
                match = _ALLOW_NEXT.search(raw)
                if not match:
                    malformed.append(
                        (
                            lineno,
                            'malformed allowance; the exact form is '
                            '<!-- %s: RULE_ID target="..." reason="..." -->' % ALLOW_NEXT_NAME,
                        )
                    )
                else:
                    rules = tuple(
                        part.strip() for part in match.group("rules").split(",") if part.strip()
                    )
                    allowance = Allowance(
                        lineno,
                        rules,
                        match.group("target").strip(),
                        match.group("reason").strip(),
                    )
                    unknown = [r for r in rules if r not in known_rules]
                    if unknown:
                        allowance.problem = "allowance names unknown rule(s) %s" % ", ".join(
                            repr(r) for r in unknown
                        )
                    elif len(set(rules)) != len(rules):
                        allowance.problem = "allowance repeats a rule id"
                    elif SHIPPING_TARGET.lower() in allowance.target.lower():
                        allowance.problem = (
                            "allowance names the shipping target %r; this escape exists only for "
                            "truthful facts about other boards" % allowance.target
                        )
                    elif len(allowance.reason) < MIN_REASON_CHARS:
                        allowance.problem = (
                            "allowance reason is shorter than %d characters" % MIN_REASON_CHARS
                        )
                    allowances.append(allowance)
                    if pending is not None:
                        pending.problem = (
                            "allowances cannot be stacked; each covers exactly one prose line"
                        )
                    pending = allowance if allowance.problem is None else None
                    continue

        line = raw
        if in_comment:
            end = _HTML_COMMENT_CLOSE.search(line)
            if not end:
                continue
            line = line[end.end():]
            in_comment = False
        while True:
            start = _HTML_COMMENT_OPEN.search(line)
            if not start:
                break
            end = _HTML_COMMENT_CLOSE.search(line, start.end())
            if not end:
                line = line[: start.start()]
                in_comment = True
                comment_lineno = lineno
                break
            line = line[: start.start()] + " " + line[end.end():]

        # Read the heading from the comment-stripped text, never from `stripped`.
        # `section` is what grants a `SECTION_ALLOWANCES` exemption, so a heading
        # that only exists inside an HTML comment must neither open an allowed
        # section nor close one -- it is not visible prose.
        visible = line.strip()
        heading = _HEADING.match(visible)
        if heading:
            depth = len(heading.group(1))
            if depth == 1:
                section = None
            elif depth == 2:
                section = visible

        prose = normalise(line)
        if not prose.strip():
            if pending is not None and stripped:
                pending.problem = "allowance is not immediately followed by a prose line"
                pending = None
            continue

        records.append((lineno, prose, section))
        if pending is not None:
            if pending.target.lower() not in prose.lower():
                pending.problem = (
                    "allowance declares target %r but the line it covers does not mention it"
                    % pending.target
                )
            else:
                pending.covers = lineno
            pending = None

    if pending is not None:
        pending.problem = "allowance has no following prose line to cover"

    # Anything still open at end of file swallowed the rest of the document. Say
    # so: a scan that could not read the tail must not be able to report OK.
    if in_comment:
        defects.append(
            (
                comment_lineno,
                "%s opened here and never closed; every following line was hidden "
                "from the scan" % UNTERMINATED_COMMENT,
            )
        )
    if fence.is_open:
        defects.append(
            (
                fence.lineno,
                "%s opened here with %d '%s' and never closed by a run of at least that "
                "many of the same character on a line of its own; every following line was "
                "treated as code" % (UNTERMINATED_FENCE, fence.length, fence.char),
            )
        )

    return records, allowances, retired, malformed, defects


def rule_hits(records, allowed_sections=(), suppressed=None):
    """Yield ``(lineno, rule, match)`` for every live contradiction in records.

    A rule-level ``unless`` suppressor stops a hit from being a violation; it does
    not make the hit invisible.  When ``suppressed`` is a list, every excused hit
    is appended to it as a dict carrying the rule id, the line, the matched text
    and the suppressor text that excused it, so a suppression can be counted and
    read in review instead of living only in this file's regexes.  Only live
    contradictions are *yielded*, so callers that ask "does this section still
    restate something guarded?" are unaffected.
    """
    for lineno, prose, section in records:
        if section in allowed_sections:
            continue
        for rule in RULES:
            match = rule.pattern.search(prose)
            if not match:
                continue
            if rule.unless is not None:
                excuse = rule.unless.search(prose)
                if excuse is not None:
                    if suppressed is not None:
                        suppressed.append(
                            {
                                "rule": rule.id,
                                "line": lineno,
                                "match": match.group(0).strip(),
                                "suppressor": excuse.group(0).strip(),
                            }
                        )
                    continue
            yield lineno, rule, match


# --------------------------------------------------------------------------- #
# discovery
# --------------------------------------------------------------------------- #


def _skipped_relpath(root, dirpath):
    rel = os.path.relpath(dirpath, root).replace(os.sep, "/")
    return any(rel == skip or rel.startswith(skip + "/") for skip in SKIP_RELPATHS)


def find_markdown(root):
    found = []
    for dirpath, dirnames, filenames in os.walk(root):
        # Exact-name exclusion only. No dot-directory rule: see SKIP_DIRS.
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
        if _skipped_relpath(root, dirpath):
            dirnames[:] = []
            continue
        for name in sorted(filenames):
            if not name.lower().endswith(".md"):
                continue
            abs_path = os.path.join(dirpath, name)
            rel = os.path.relpath(abs_path, root).replace(os.sep, "/")
            if rel in SKIP_FILES:
                continue
            found.append(rel)
    return sorted(found)


def read(root, rel):
    try:
        with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return None


# --------------------------------------------------------------------------- #
# checks
# --------------------------------------------------------------------------- #


def self_check():
    """Prove the rule table and the exemption tables are live."""
    failures = []
    if not RULES:
        failures.append("rule table is empty")
    seen = set()
    for rule in RULES:
        if rule.id in seen:
            failures.append("%s: duplicate rule id" % rule.id)
        seen.add(rule.id)
        if not rule.probe or not rule.antiprobe:
            failures.append("%s: missing probe or antiprobe" % rule.id)
            continue
        if not rule.pattern.search(rule.probe):
            failures.append("%s: probe does not match its own pattern" % rule.id)
        if rule.pattern.search(rule.antiprobe):
            failures.append("%s: antiprobe matches its own pattern" % rule.id)
        # A probe stripped of code spans must still match, otherwise the rule
        # could never fire on a real document.
        if not rule.pattern.search(normalise(rule.probe)):
            failures.append("%s: probe does not survive prose normalisation" % rule.id)
        if rule.unless is None:
            if rule.probe_exempt:
                failures.append("%s: has probe_exempt but no unless pattern" % rule.id)
        else:
            if not rule.probe_exempt:
                failures.append("%s: has an unless pattern but no probe_exempt" % rule.id)
            else:
                if not rule.pattern.search(rule.probe_exempt):
                    failures.append("%s: probe_exempt does not match the rule pattern" % rule.id)
                if not rule.unless.search(rule.probe_exempt):
                    failures.append("%s: unless pattern does not suppress probe_exempt" % rule.id)
            if rule.unless.search(rule.probe):
                failures.append("%s: unless pattern also suppresses the rule's own probe" % rule.id)

    # Suppression observability is asserted, not merely documented. Each rule's own
    # `probe_exempt` is a hit its suppressor swallows, so replaying it through the
    # scanner must produce no violation for that rule *and* exactly one recorded
    # suppression carrying the evidence. If the recording is narrowed or removed,
    # this fails instead of quietly shrinking what review can see.
    suppressing = [rule for rule in RULES if rule.unless is not None]
    if not suppressing:
        failures.append("no rule carries a suppressor, so suppression reporting is unproven")
    for rule in suppressing:
        if not rule.probe_exempt:
            continue  # already reported above
        recorded = []
        replay = [(1, normalise(rule.probe_exempt), None)]
        if any(hit[1].id == rule.id for hit in rule_hits(replay, (), recorded)):
            failures.append("%s: probe_exempt is still reported as a violation" % rule.id)
        mine = [entry for entry in recorded if entry.get("rule") == rule.id]
        if not mine:
            failures.append(
                "%s: a suppressed hit is not recorded; suppressions must stay observable" % rule.id
            )
            continue
        if not mine[0].get("suppressor"):
            failures.append("%s: recorded suppression carries no suppressor evidence" % rule.id)
        if not mine[0].get("match") or not mine[0].get("line"):
            failures.append("%s: recorded suppression carries no matched text or line" % rule.id)

    if not REQUIRE_CANONICAL_LINK:
        failures.append("canonical-link list is empty")
    if not CANONICAL_SECTIONS or not CANONICAL_CLASSES or not CANONICAL_VALUES:
        failures.append("canonical structure expectations are empty")

    # Current-fact sections are never exemptible. An empty list here would make the
    # loop below vacuous, so it is checked first.
    if not CANONICAL_CURRENT_FACT_SECTIONS:
        failures.append("no canonical current-fact sections are declared")
    for section in CANONICAL_CURRENT_FACT_SECTIONS:
        if section not in CANONICAL_SECTIONS:
            failures.append("current-fact section %r is not a declared canonical section" % section)
        if section in SECTION_ALLOWANCES.get(CANONICAL, ()):
            failures.append("current-fact section %r must never carry a section allowance" % section)

    known_rules = {rule.id for rule in RULES}
    for rel, sections in SECTION_ALLOWANCES.items():
        if not sections:
            failures.append("%s: empty section allowance" % rel)
        for section in sections:
            if section not in ALLOWED_SECTION_WHITELIST:
                failures.append(
                    "%s: %r is not in the historical-section whitelist" % (rel, section)
                )
        if rel in SKIP_FILES or any(rel.startswith(skip + "/") for skip in SKIP_RELPATHS):
            failures.append("%s: has a section allowance but is not scanned at all" % rel)

    # The documented scope and the real scope must agree. An earlier cut excluded
    # every dot-directory while claiming a repo-wide scan; that is the bug this
    # assertion exists to prevent from returning.
    if not MUST_SCAN_DIRS:
        failures.append("no must-scan directories are declared")
    for name in MUST_SCAN_DIRS:
        if name in SKIP_DIRS:
            failures.append("%s must stay scannable but appears in SKIP_DIRS" % name)
    for name in SKIP_DIRS:
        if "/" in name or os.sep in name:
            failures.append("%r: directory skips are by bare name, not by path" % name)

    for rel in SKIP_FILES:
        if "/" in rel:
            failures.append("%s: file skips are root-level only, to keep them narrow" % rel)
        if rel in (CANONICAL, ADR, DOCS_INDEX):
            failures.append("%s: a required document may not be skipped" % rel)

    # The source-anchor table is an assertion about files outside the Markdown scan,
    # so its own shape is checked here: an empty table, a duplicate id, an absolute
    # or escaping path, an empty token, a bare token that cannot state a
    # relationship, or an anchor that names no claim would all make a freshness
    # finding unactionable or vacuous.
    if not SOURCE_ANCHORS:
        failures.append("source-anchor table is empty")
    seen_anchors = set()
    for anchor in SOURCE_ANCHORS:
        if anchor.id in seen_anchors:
            failures.append("%s: duplicate source-anchor id" % anchor.id)
        seen_anchors.add(anchor.id)
        if not anchor.path:
            failures.append("%s: source anchor has no path" % anchor.id)
        elif anchor.path.startswith("/") or ".." in anchor.path.split("/"):
            failures.append(
                "%s: source-anchor path %r must be repo-relative and must not escape the root"
                % (anchor.id, anchor.path)
            )
        if not anchor.tokens:
            failures.append("%s: source anchor asserts no tokens, so it can never fail" % anchor.id)
        seen_tokens = set()
        for token in anchor.tokens:
            collapsed = " ".join(token.split())
            if not collapsed:
                failures.append("%s: source anchor carries an empty expected token" % anchor.id)
                continue
            if collapsed in seen_tokens:
                failures.append(
                    "%s: source anchor repeats the expected token %r, which adds no assertion"
                    % (anchor.id, token)
                )
            seen_tokens.add(collapsed)
            # A bare identifier or bare literal is the split-token shape: two of
            # them can both be present while the relationship between them is gone.
            # See the module docstring, "Tokens must carry their own relationship".
            if _BARE_TOKEN.match(collapsed):
                failures.append(
                    "%s: expected token %r is a bare identifier or literal, so it cannot state "
                    "the relationship the row cites and would be satisfied by a file in which "
                    "that relationship has drifted apart; pin the whole line or block instead"
                    % (anchor.id, token)
                )
        if not anchor.claim or not anchor.claim.strip():
            failures.append(
                "%s: source anchor names no claim, so a freshness finding could not say what "
                "to re-derive" % anchor.id
            )

    if RETIRED_MARKER == ALLOW_NEXT_NAME:
        failures.append("the retired marker and the live marker must differ")
    if not known_rules:
        failures.append("no rule ids available for allowance validation")

    return failures


def scan_violations(root, files):
    """Scan every file.

    Returns ``(violations, marker_problems, structure_problems, suppressions)``:

    *   ``violations`` -- live guarded contradictions.
    *   ``marker_problems`` -- abuse of the allowance markers, which is an attempt
        to hide a contradiction and so ranks with one.
    *   ``structure_problems`` -- pre-formatted strings for states in which the
        scan could not read a whole document (unterminated comment or fence) or in
        which an allowance has stopped suppressing anything.
    *   ``suppressions`` -- every hit a rule's ``unless`` clause excused, so the
        excuse is observable rather than implicit.
    """
    violations = []
    marker_problems = []
    structure_problems = []
    suppressions = []

    for rel in files:
        text = read(root, rel)
        if text is None:
            continue
        records, allowances, retired, malformed, defects = analyse(text)
        allowed_sections = SECTION_ALLOWANCES.get(rel, ())

        for lineno, detail in defects:
            structure_problems.append("%s:%d: %s" % (rel, lineno, detail))

        for lineno in retired:
            marker_problems.append(
                {
                    "file": rel,
                    "line": lineno,
                    "message": "carries the retired %s marker; file-level exemptions were "
                    "replaced by section allowances and %s" % (RETIRED_MARKER, ALLOW_NEXT_NAME),
                }
            )
        for lineno, detail in malformed:
            marker_problems.append({"file": rel, "line": lineno, "message": detail})
        for allowance in allowances:
            if allowance.problem:
                marker_problems.append(
                    {"file": rel, "line": allowance.lineno, "message": allowance.problem}
                )
        live = [a for a in allowances if a.problem is None and a.covers is not None]

        excused = []
        for lineno, rule, match in rule_hits(records, allowed_sections, excused):
            covering = None
            for allowance in live:
                if allowance.covers == lineno and rule.id in allowance.rules:
                    covering = allowance
                    break
            if covering is not None:
                covering.used.add(rule.id)
                continue
            violations.append(
                {
                    "rule": rule.id,
                    "file": rel,
                    "line": lineno,
                    "match": match.group(0).strip(),
                    "message": rule.message,
                }
            )

        for hit in excused:
            entry = {"file": rel}
            entry.update(hit)
            suppressions.append(entry)

        for allowance in live:
            idle = [r for r in allowance.rules if r not in allowance.used]
            if idle:
                structure_problems.append(
                    "%s:%d: allowance lists %s but suppresses nothing for %s; narrow or delete it"
                    % (rel, allowance.lineno, ",".join(allowance.rules), ",".join(idle))
                )

    return violations, marker_problems, structure_problems, suppressions


def check_source_anchors(root):
    """Verify the source text behind the canonical record's repository-observed rows.

    Returns ``(failures, inventory)``.  ``failures`` are pre-formatted ``STRUCTURE``
    strings: a cited file that is absent, or a cited file that no longer carries an
    expected literal token.  ``inventory`` is every anchor with its status, so the
    JSON report shows what was checked and not only what failed.

    The finding wording is deliberate.  A present token proves the citation is
    fresh, not that the claim is physically true; a missing token proves the
    citation is stale, not that the claim is false.  The remedy in both directions
    is to re-read the source and re-derive the row.  Both findings carry
    ``ANCHOR_CAVEAT``, because the failure a reader will assume is covered -- a
    cited line *number* going stale -- is the one this layer cannot see.
    """
    failures = []
    inventory = []

    for anchor in SOURCE_ANCHORS:
        entry = {
            "id": anchor.id,
            "path": anchor.path,
            "tokens": list(anchor.tokens),
            "claim": anchor.claim,
        }
        text = read(root, anchor.path)
        if text is None:
            failures.append(
                "source anchor %s: cited file %s is absent, so the canonical record's claim "
                "that %s can no longer be checked against the source it was read from; "
                "re-derive the row or update the anchor (absence of the file is not evidence "
                "the claim is false; %s)" % (anchor.id, anchor.path, anchor.claim, ANCHOR_CAVEAT)
            )
            entry["status"] = "missing-file"
            entry["missing"] = list(anchor.tokens)
            inventory.append(entry)
            continue

        missing = [
            token
            for token, pattern in zip(anchor.tokens, anchor.patterns)
            if pattern is None or not pattern.search(text)
        ]
        if missing:
            failures.append(
                "source anchor %s: %s no longer contains %s, so the canonical record's claim "
                "that %s cites text that has moved or changed; re-read the source and "
                "re-derive the row (a missing token means the citation is stale, not that "
                "the claim is false; %s)"
                % (
                    anchor.id,
                    anchor.path,
                    ", ".join(repr(token) for token in missing),
                    anchor.claim,
                    ANCHOR_CAVEAT,
                )
            )
            entry["status"] = "missing-token"
            entry["missing"] = missing
        else:
            entry["status"] = "ok"
            entry["missing"] = []
        inventory.append(entry)

    return failures, inventory


def _section_records(records, heading):
    return [rec for rec in records if rec[2] == heading]


def check_structure(root, files):
    """Canonical record integrity, links and allowance hygiene."""
    failures = []
    missing = []

    if not files:
        missing.append("no Markdown files found under %s" % root)

    canonical = read(root, CANONICAL)
    if canonical is None:
        missing.append("canonical record absent: %s" % CANONICAL)
    adr = read(root, ADR)
    if adr is None:
        missing.append("decision record absent: %s" % ADR)

    if canonical is not None:
        cursor = -1
        for section in CANONICAL_SECTIONS:
            found = canonical.find(section, cursor + 1)
            if found < 0:
                if section in canonical:
                    failures.append(
                        "%s: required section %r is out of order" % (CANONICAL, section)
                    )
                else:
                    failures.append("%s: missing required section %r" % (CANONICAL, section))
            else:
                cursor = found
        for klass in CANONICAL_CLASSES:
            if klass not in canonical:
                failures.append("%s: provenance class %r not named" % (CANONICAL, klass))
        # A superseded value in the exempt Historical section must not satisfy
        # the live-record sentinel.  Keep this check on the raw current-fact
        # sections so code-like values (for example Kconfig symbols) remain
        # visible even though normalise() removes them from prose scanning.
        current_lines = []
        current_section = None
        fence = Fence()
        for lineno, raw in enumerate(canonical.splitlines(), start=1):
            if fence.feed(lineno, raw):
                continue
            heading = None if fence.is_open else _HEADING.match(raw.strip())
            if heading is not None:
                depth = len(heading.group(1))
                if depth == 1:
                    current_section = None
                elif depth == 2:
                    current_section = raw.strip()
            if current_section in CANONICAL_CURRENT_FACT_SECTIONS:
                current_lines.append(raw)
        current_facts = "\n".join(current_lines)
        for value in CANONICAL_VALUES:
            if value not in current_facts:
                failures.append("%s: corrected value %r no longer present" % (CANONICAL, value))
        if os.path.basename(ADR) not in canonical:
            failures.append("%s: does not link the decision record" % CANONICAL)

    index = read(root, DOCS_INDEX)
    if index is not None and os.path.basename(ADR) not in index:
        failures.append("%s: does not link the decision record" % DOCS_INDEX)

    # Section allowances must be real and must still be earning their keep: an
    # allowance over a section that no longer restates anything is a hole.
    for rel, sections in SECTION_ALLOWANCES.items():
        text = read(root, rel)
        if text is None:
            missing.append("file with a section allowance is absent: %s" % rel)
            continue
        records = analyse(text)[0]
        for section in sections:
            headings = [rec for rec in records if rec[1].strip() == section]
            if not headings:
                failures.append("%s: allowed section %r is absent" % (rel, section))
                continue
            if len(headings) > 1:
                failures.append("%s: allowed section %r appears more than once" % (rel, section))
            in_section = _section_records(records, section)
            if not any(True for _ in rule_hits(in_section)):
                failures.append(
                    "%s: allowed section %r restates nothing guarded; the allowance is dead "
                    "and must be removed" % (rel, section)
                )

    canonical_name = os.path.basename(CANONICAL)
    link_pattern = re.compile(r"\]\(([^)\s]+)")
    for rel in REQUIRE_CANONICAL_LINK:
        text = read(root, rel)
        if text is None:
            missing.append("required document absent: %s" % rel)
            continue
        linked = False
        for match in link_pattern.finditer(text):
            target = match.group(1).strip("<>").split("#", 1)[0].split("?", 1)[0]
            if not target or "://" in target:
                continue
            if target.startswith("/"):
                candidate = os.path.normpath(target.lstrip("/"))
            else:
                candidate = os.path.normpath(os.path.join(os.path.dirname(rel), target))
            if candidate == CANONICAL and os.path.isfile(os.path.join(root, candidate)):
                linked = True
                break
        if not linked:
            failures.append("%s: no Markdown link to %s" % (rel, canonical_name))

    return failures, missing


# --------------------------------------------------------------------------- #
# entry point
# --------------------------------------------------------------------------- #


def default_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def build_parser():
    parser = argparse.ArgumentParser(
        prog=TOOL,
        description="Guard the canonical round-target identity record (issue #211).",
        epilog=(
            "This is a phrase tripwire and source-text freshness check, not semantic or "
            "factual proof. A green result does not replace human review of identity claims; "
            "source anchors do not validate cited line numbers, negative claims, derived "
            "counts, or physical hardware truth."
        ),
    )
    parser.add_argument("--root", default=None, help="repository root (default: parent of scripts/)")
    parser.add_argument("--report", default=None, help="write a JSON report to this path")
    parser.add_argument("--list-rules", action="store_true", help="print rule ids and exit 0")
    parser.add_argument(
        "--list-anchors",
        action="store_true",
        help="print 'id<TAB>path' for every source anchor and exit 0",
    )
    parser.add_argument(
        "--self-check",
        action="store_true",
        help="verify the checker's own rules, then continue with the scan",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help=(
            "suppress the per-hit suppression and violation lines, and the fenced-block "
            "quoting note printed with them; the summary token and "
            "the MISSING, SELFCHECK, MARKER-ABUSE and STRUCTURE lines are still printed"
        ),
    )
    return parser


def main(argv):
    parser = build_parser()
    try:
        args = parser.parse_args(argv)
    except SystemExit as exc:
        # argparse uses SystemExit(0) for --help. That is successful usage, not the
        # malformed-invocation contract below; preserve the help text it already
        # printed and return success to the caller.
        if exc.code == 0:
            return EXIT_OK
        print("%s: bad invocation" % TOKEN_USAGE)
        return EXIT_USAGE

    for name in ("root", "report"):
        value = getattr(args, name)
        if value is not None and not value.strip():
            print("%s: --%s given an empty value" % (TOKEN_USAGE, name))
            return EXIT_USAGE

    if args.list_rules:
        for rule in RULES:
            print(rule.id)
        return EXIT_OK

    # The fixture suite reads this to build self-contained temporary roots that
    # carry exactly the cited source files and nothing else.
    if args.list_anchors:
        for anchor in SOURCE_ANCHORS:
            print("%s\t%s" % (anchor.id, anchor.path))
        return EXIT_OK

    root = os.path.abspath(args.root) if args.root else default_root()
    if not os.path.isdir(root):
        print("%s: root is not a directory: %s" % (TOKEN_MISSING, root))
        return EXIT_MISSING

    selfcheck_failures = self_check() if args.self_check else []

    files = find_markdown(root)
    violations, marker_problems, scan_structure, suppressions = scan_violations(root, files)
    structure_failures, missing = check_structure(root, files)
    anchor_failures, anchors = check_source_anchors(root)
    structure_failures = structure_failures + scan_structure + anchor_failures

    for entry in missing:
        print("%s: %s" % (TOKEN_MISSING, entry))
    for entry in selfcheck_failures:
        print("%s: %s" % (TOKEN_SELFCHECK, entry))
    for entry in marker_problems:
        print(
            "%s: %s:%d: %s"
            % (TOKEN_MARKER_ABUSE, entry["file"], entry["line"], entry["message"])
        )
    for entry in structure_failures:
        print("%s: %s" % (TOKEN_STRUCTURE, entry))
    if not args.quiet:
        # Informational, and printed before the violations so a reader sees what
        # the guard chose *not* to report. These never affect the exit code.
        for entry in suppressions:
            print(
                "%s: %s %s:%d: %r suppressed by %r"
                % (
                    TOKEN_SUPPRESSED,
                    entry["rule"],
                    entry["file"],
                    entry["line"],
                    entry["match"],
                    entry["suppressor"],
                )
            )
        for entry in violations:
            print(
                "%s: %s %s:%d: %r -- %s"
                % (
                    TOKEN_VIOLATION,
                    entry["rule"],
                    entry["file"],
                    entry["line"],
                    entry["match"],
                    entry["message"],
                )
            )
        # Printed once with the violation lines it annotates, not once per hit: the
        # remedy is the same for all of them, and a paragraph repeated per finding
        # is the kind of noise readers learn to skip. It is still part of the
        # violation report -- same token, printed immediately after the hits -- so a
        # reader who sees a violation sees why an indented quotation became one.
        if violations:
            print("%s: %s" % (TOKEN_VIOLATION, QUOTING_NOTE))

    # Marker abuse is a contradiction-class failure: it is an attempt to hide one.
    violation_count = len(violations) + len(marker_problems)

    if missing:
        exit_code, token = EXIT_MISSING, TOKEN_MISSING
    elif selfcheck_failures:
        exit_code, token = EXIT_SELFCHECK, TOKEN_SELFCHECK
    elif structure_failures:
        exit_code, token = EXIT_STRUCTURE, TOKEN_STRUCTURE
    elif violation_count:
        exit_code, token = EXIT_VIOLATION, TOKEN_VIOLATION
    else:
        exit_code, token = EXIT_OK, TOKEN_OK

    # `rules` is the size of the rule table, not a branch-coverage figure: see the
    # module docstring. `suppressed` is informational and does not gate the exit.
    # `anchors_fresh` is how many anchors still carry every expected token, out of
    # the size of the anchor table; a shortfall is already counted in `structure`.
    # It is NOT a coverage figure over the repository-observed rows -- see
    # ANCHOR_SCOPE and the module docstring.
    print(
        "%s: files=%d rules=%d violations=%d structure=%d missing=%d selfcheck=%d "
        "suppressed=%d anchors_fresh=%d/%d"
        % (
            token,
            len(files),
            len(RULES),
            violation_count,
            len(structure_failures),
            len(missing),
            len(selfcheck_failures),
            len(suppressions),
            sum(1 for a in anchors if a["status"] == "ok"),
            len(anchors),
        )
    )

    if args.report:
        report = {
            "tool": TOOL,
            "version": VERSION,
            "root": root,
            "files_scanned": files,
            "rules": [{"id": r.id, "message": r.message} for r in RULES],
            # Every anchor, not only the failing ones: a reader can see which
            # citations were checked. Failures also appear in `structure_failures`,
            # which is what gates the exit code. `source_anchor_scope` travels with
            # them so the count cannot be read as coverage over the R rows.
            "source_anchors": anchors,
            "source_anchor_scope": ANCHOR_SCOPE,
            "violations": violations,
            # Travels with the violations so the step summary and any other reader
            # of the report states the indented-code consequence from the checker
            # rather than restating it independently.
            "quoting_note": QUOTING_NOTE,
            "marker_abuse": marker_problems,
            "suppressions": suppressions,
            "structure_failures": structure_failures,
            "missing": missing,
            "selfcheck_failures": selfcheck_failures,
            "token": token,
            "exit_code": exit_code,
        }
        try:
            with open(args.report, "w", encoding="utf-8") as handle:
                json.dump(report, handle, indent=2, sort_keys=True)
                handle.write("\n")
        except OSError as exc:
            print("%s: could not write report %s: %s" % (TOKEN_USAGE, args.report, exc))
            return EXIT_USAGE

    return exit_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
