# Decision: one provenance-classed target identity record, enforced by a host check

**Date:** 2026-07-30
**Status:** Accepted
**Issue:** [#211](https://github.com/muness/roon-knob/issues/211) · Parent
[#193](https://github.com/muness/roon-knob/issues/193) · Program
[#201](https://github.com/muness/roon-knob/issues/201) · Feeds
[#203](https://github.com/muness/roon-knob/issues/203),
[#117](https://github.com/muness/roon-knob/issues/117),
[#190](https://github.com/muness/roon-knob/issues/190)

## Context

The repository described its only shipping target inconsistently. `README.md` named the
Waveshare **ESP32-S3-Knob-Touch-LCD-1.8**; `docs/esp/hw-reference/board.md` named an
"ESP32-S3 Touch AMOLED 1.8″", claimed 16.7 M colours, and stated that no backlight control
was needed — while the firmware configures an LEDC PWM backlight on GPIO 47. Several
documents called `SH8601` the display controller, which is the name of a *software
component*, not of the driver IC the vendor declares. `DEVELOPMENT.md` advertised an encoder
push button that no code reads, and `README.md` advertised a knob press that no code
implements.

The first cut of this correction reproduced the same failure in miniature: it stated that the
firmware drives the backlight at 50 % duty, which is what a stale comment at
`platform_display_idf.c:456` says, not what the code does — the channel configuration at `:471`
reads `CONFIG_RK_BACKLIGHT_NORMAL`, whose Kconfig default is 100 of 255, about 39 %, and which
`docs/dev/KCONFIG.md` had documented correctly all along. That is the origin of decision 8
below: source comments are not evidence.

The previous position was to correct nothing until the physical device had been inventoried.
That kept a demonstrably wrong record published, and it conflated two different problems:
facts already settled by primary vendor sources, this repository's own code, and immutable
release binaries; versus facts that genuinely need the device in hand.

## Decision

1. **`docs/esp/hw-reference/board.md` is the single canonical identity record.** No second
   `TARGET_IDENTITY.md` is created. A competing file would have reproduced the original
   failure in a new location. This single-record invariant is enforced by human review, not
   by `check_docs_identity.py`: the checker catches known contradictory wording, while
   `REQUIRE_CANONICAL_LINK` is a hand-maintained list and does not discover a new, truthful
   identity-bearing document automatically.

2. **Every fact in it carries a provenance class** — vendor-declared, repository-observed,
   release-artifact-observed, historical/superseded, or physically unverified. Unverified
   facts are listed explicitly with what blocks them, and none is treated as verified
   anywhere.

3. **Other documents keep only the facts they own and link to the canonical record.** Pin
   tables, driver mechanics, colour-format notes, and porting steps stay where they are;
   independent restatements of product identity do not.

4. **Driver wording is fixed to what the source supports.** The project *replaces* the
   `esp_lcd_sh8601` component's vendor-specific init array; the component still sends
   standard DCS `MADCTL`/`COLMOD` setup *before* that array; the project's array overwrites
   `MADCTL` (its last entry is `{0x36, {0x00}}`) while `COLMOD` remains governed by
   `panel_dev_config.bits_per_pixel`. The project's 185-entry array is byte-for-byte
   Waveshare's own sequence for this product and is structurally an ST77916-family sequence
   (0.8928 opcode-sequence similarity to Espressif's `esp_lcd_st77916` SPI default, longest
   equal run 175) with **zero** opcode overlap with the SH8601 component's own default. That
   corroborates the vendor's ST77916 declaration; it is explicitly **not** a claim about
   physical silicon.

5. **Artifact evidence is recorded, not acted on.** The v2.5.1 flash-size disagreement
   (merged header nibble `0x3` = 8 MB, application header nibble `0x4` = 16 MB), the exact
   SHA-256s, and the application descriptor are recorded with reproducible commands. **No
   geometry decision is made here** — that is #203's, and #203's 16 MB direction still
   depends on physical flash verification.

6. **A host-only checker is a tripwire for the corrections, repo-wide, with scoped
   exemptions.** `scripts/check_docs_identity.py` (standard library only, no network) scans
   Markdown across the repository for the known product-name, panel-type, colour-depth,
   backlight-absence, backlight-duty, controller, module-package, encoder-button and
   touch-variant contradictions, and verifies that the canonical record keeps its provenance
   structure and that identity-bearing documents still link to it. It runs in its own
   workflow, `.github/workflows/docs-identity.yml`.

   **What it is.** Nine regular expressions over normalised prose. For the single-physical-line
   renderings in the fixtures, its demonstrated recall is exactly two sets: the wordings that
   were actually in this repository before #211, each pinned by a rule probe and a fixture; and
   the paraphrases in
   `scripts/fixtures/docs_identity/paraphrases.tsv`, authored outside the checker so that "the
   regex matches its own example" cannot be mistaken for coverage. That corpus exists because a
   dissent pass wrote fifteen ordinary-prose restatements of these same contradictions and the
   first rule table caught only three. Quoting the shapes it missed, as strings rather than as
   claims — the guard reads prose, so these are fenced deliberately:

   ```text
   spelled-out AMOLED ("active-matrix organic LED")
   the colour count as digits (16777216) and as a bit depth ("24-bit colour")
   self-illumination as a reason there is nothing to dim
   the encoder press as "click the dial" / "push the dial in" / "shaft switch selects"
   SH8601 as silicon, or attributed to Sitronix
   the module package with separators dropped (ESP32S3 WROOM 1)
   the duty figure written in words (fifty percent)
   the touch variant as a bare part number (CST816S)
   ```

   The rules were widened until all fifteen are reported, and each sentence is now an individual
   test. The corpus is therefore independently authored but **fitted**, not held out: the wrap
   figures below measure sensitivity on fitted data, not general recall on unseen wording. Issue
   #213 requires held-out wording before #205 may make the check required.

   **Nine is a rule count, not a coverage number.** Each rule is an alternation of several
   clauses, and the per-rule fixture proves that *one* clause fires. Nothing here claims that the
   number of rules equals the number of covered alternation branches, and neither the checker nor
   the fixture suite asserts such an equality. A clause is pinned only where a paraphrase in the
   corpus happens to exercise it; adding a clause adds reach but no evidence of reach.

   **What it is not.** It is not a semantic or factual check, and a relapse is **not**
   guaranteed to become a CI failure. Only a relapse in a known or tested wording is. Prose that
   never names a guarded token ("the screen makes its own light"), a colour depth given only as a
   hex range, a claim carried in an image, a diagram, a translated spec table or a fenced code
   example, and any file this scan does not read, all pass. Matching is per physical line, so a
   guarded phrase broken across lines can also pass; `CONTROLLER_CLAIM`, `BACKLIGHT_DUTY` and
   `ENCODER_BUTTON` have no single-token clause that survives every such break. The fixture suite
   pins all three blind spots as current behaviour so #213 must flip explicit expectations when it
   closes the gap. Human review of identity claims remains required. The
   check's job is to make the cheap relapses loud and to make any widening of the exemptions a
   reviewed change to a Python file rather than a documentation edit.

   A reproducible audit estimate applies Python `textwrap.wrap` to each of the 15 paraphrases at
   every starting column, using initial indentation to represent that column, then tests its
   designated rule against each physical output line. At 88 columns, 120 of 1,320 offsets (9.1 %)
   are missed: `BACKLIGHT_DUTY` 20/88, `CONTROLLER_CLAIM` 49/264, `ENCODER_BUTTON` 40/264,
   `COLOR_DEPTH` 7/176, `PANEL_AMOLED` 4/88, and zero for the other rules. At 100 columns the same
   120 misses are 8.0 % of 1,500 offsets. This is fitted-data sensitivity, not a recall guarantee;
   #213 owns the formal audit and any mechanism change.

   **Scan scope, stated as mechanisms rather than counts, because counts drift.** Every
   non-exempt file ending in `.md` at any depth is scanned, including `.github/**/*.md` — `.github/RELEASE_TEMPLATE.md`
   ships release prose that describes the target, so it is documentation for this purpose. There
   are two content-specific exclusions: contradiction fixtures under `scripts/fixtures/`, which
   must contain guarded wording, and the bot-managed root `CHANGELOG.md` described below. There
   is no dot-directory rule: a directory is skipped only when its exact name appears in
   `SKIP_DIRS` (version control, interpreter and tool caches, build output, vendored
   dependencies), so a future `.notes/` is scanned rather than silently exempt. An earlier cut
   excluded every dot-directory while claiming a repo-wide scan; `--self-check` now fails if
   `.github` is put back into `SKIP_DIRS`, and the fixture suite pins both the `.github` scan and
   the exact list of excluded directory names.

   **Within a file, two constructs are not prose, and neither may hide the rest of the
   document.** Fenced code is exempt — quoting a superseded wording as source is legitimate — and
   HTML comment bodies are stripped before scanning. Both are things that *end*, and the first cut
   of the checker did not check that they did. Fences were a boolean toggled by any
   `^\s{0,3}(```|~~~)` line, so tildes could "close" a backtick fence and a three-character run
   could "close" a four-character one; a comment opener with no closing marker swallowed every
   following line. In both cases the tail of the file was silently unscanned and the run could
   still be green. Fences are now matched CommonMark-shaped — a closer must use the same
   character, be at least as long as the opener, and carry nothing after it — and an opener left
   unclosed at end of file, of either kind, is a `STRUCTURE` failure naming the file and the
   opening line. Structure outranks violation in the exit precedence for exactly this reason: a
   document that hid its own tail is the state in which a contradiction cannot be seen, so it must
   not be reportable as clean. Valid Markdown is unaffected: info strings, longer closers, tilde
   fences containing backtick fences, indented fences and inline `` `code` `` spans on their own
   line all keep working, and `fence_valid.md` plus `comment_closed.md` pin that they do.

   **Indented code is not one of those two constructs: it is scanned as prose, and the reporting says
   so.** A four-space indented block is code to CommonMark and is deliberately not exempt here.
   Recognising it would mean tracking list and blockquote context to tell a code block from a
   continuation line, and an indentation-only exemption would exempt the reflowed middle of a
   paragraph — including every indented rendering the wrap audit above uses to represent a starting
   column, and those renderings are the corpus this guard's only recall evidence rests on. So the gap
   is kept. What was
   missing was discoverability: an author who indented a quotation of a superseded wording got a
   violation on a line they had marked as code, with nothing in the output explaining why. Every
   violation report now carries the remedy — quote source in a fenced block, because an indented
   block is scanned as prose — printed once with the violation lines, carried in the JSON report as
   `quoting_note`, and rendered beneath the violations in the CI step summary.
   `indented_code_scanned.md` pins both halves: the indented block is reported, and the same lines
   inside a fence are not, so the remedy is one that works.

   **Rule-level suppressions are observable.** Some rules carry an `unless` clause so that
   truthful qualified prose passes: a note that RGB888 is a 24-bit pixel format is not a claim
   about the panel, and a reference to the datasheet the vendor links is not a claim about the
   fitted part. That escape used to be invisible — it lived only in the regex, so widening one
   shrank the guard with no trace in a report. Every hit a suppressor excuses is now recorded with
   file, line, rule id, matched text and the suppressor text that excused it, and surfaced in the
   JSON report (`suppressions`), in the summary counters (`suppressed=`), in one informational
   `RK-IDENT-SUPPRESSED` line per hit, and under its own heading in the CI step summary. The exit
   code is deliberately unchanged: a run whose only findings are suppressions is still
   `RK-IDENT-OK` and still exit 0, because making qualified prose fail is the failure mode this
   escape exists to prevent. Visibility is the whole mechanism — a widened suppressor shows up as
   a longer suppression list in review. `--self-check` replays each rule's `probe_exempt` through
   the scanner and fails if the suppressed hit is not recorded, so removing the observability is
   an exit-4 self-check failure rather than a quiet narrowing.

   **The suppression set is pinned as a fingerprint, not as a scalar total.** Visibility only works if
   someone looks, and a longer list in a step summary is easy to scroll past. The fixture suite
   therefore pins the repository's suppression *set* as an exact fingerprint — one
   `rule|file|suppressor|count` line per distinct triple — and fails in either direction.

   The shape of that fingerprint is the decision. A scalar total was rejected on review: it is
   blind to the change most worth seeing, one suppressor being swapped for another at an unchanged
   total, which is exactly what a widening looks like once someone also removes a suppressed line.
   Line numbers and matched prose are excluded for the opposite reason: they churn on every edit
   above a suppressed line and on every rewording, and a baseline that fails for reasons nobody
   cares about gets updated by reflex instead of read.

   The per-triple **count** is the third term, and it closes a hole the first two left open. A
   fingerprint of distinct triples alone treats an already-listed line as a standing licence: once
   `TOUCH_VARIANT|docs/esp/hw-reference/cst816d.md|datasheet` is in the baseline, a second, third and
   fourth sentence in that file may take the same excuse from the same rule and nothing moves. That
   is a widening in the only currency that matters — how much prose the guard has stopped reading —
   and it was invisible. With the count, every additional suppressed hit fails and the diff says how
   many. The count is not a reintroduction of the excluded terms: it says how many hits a triple
   excuses, never which lines or which words.

   So the fingerprint watches which suppressor excuses which rule in which file, and how often, and
   nothing else. The suite proves all three properties — a one-for-one substitution fails at an
   unchanged total, an additional hit inside an already-pinned triple fails on the count, and
   shifting every line number while replacing every matched string changes nothing.

   Those three proofs run against a **fixed synthetic** suppression set rather than the live one,
   which is itself a correction made on review. Derived from the live baseline, the control asserting
   "line numbers and prose are ignored" compares the live fingerprint against the pinned constant —
   so the moment the repository legitimately gains or loses a suppression, one honest documentation
   edit reports two failures: "the baseline moved", which is true and is the intended friction, and
   "the fingerprint no longer ignores line numbers", which is false and points a reader at the wrong
   mechanism. A false diagnostic attached to the expected failure is how a baseline gets updated
   without being read. Synthetic rows keep one cause to one failure.

   That failure is deliberately **review friction, not a runtime failure of truthful prose**: the
   checker itself still exits 0 on a run whose only findings are suppressions, and no suppressed
   line is being called untruthful. It means "a suppressor is now excusing a different set of
   lines; say in review which, or narrow it". The failure message prints a diff of pinned against
   actual, names the command to run, and says what to establish in review before the constant is
   updated.

   **Source anchors keep the repository-observed citations fresh.** The `R` rows are claims about
   this repository's *source*, each citing a file — and a prose rule cannot notice when the cited
   source moves out from under the prose, which leaves documentation that is word-for-word
   correct-looking and no longer connected to anything. `SOURCE_ANCHORS` pins the stable,
   load-bearing, positively quotable ones as explicit (file, expected tokens) assertions: the QSPI
   pin mapping with the bus, panel-IO and reset sites that consume it; the GPIO 47 backlight pin
   with its LEDC channel, 8-bit resolution and 5 kHz timer; the `CONFIG_RK_BACKLIGHT_NORMAL` duty
   assignment and the Kconfig stanza behind it; the init array's declaration, its `{0x36, {0x00},
   1, 0}` terminator and the `vendor_config` binding that carries it; the
   `esp_lcd_new_panel_sh8601` call and the `bits_per_pixel` of the config it is handed; the encoder
   A/B pin definitions with the `gpio_config` and `gpio_get_level` sites that use them; the `0x15`
   touch address and the 7-byte register read; the unpinned `esp_lcd_sh8601` dependency; and the
   zone picker's click registration, emitter, handler and default label literal. A cited file that
   is absent, or one that no longer carries an expected token, is a named non-zero `STRUCTURE`
   finding, and the full inventory with each anchor's status is in the JSON report's
   `source_anchors` so a reader sees what was checked rather than only what failed.

   **A token must state the relationship it is cited for.** The first cut split facts into
   fragments — `PIN_NUM_BK_LIGHT` and `((gpio_num_t)47)` as separate tokens — and that is a false
   positive waiting to happen: both survive a file in which the name and the number have drifted
   apart, so the guard would report a fresh citation for a relationship that no longer exists.
   Every token is now a whole line or a whole block that carries the relationship itself. To make
   that safe, matching is whitespace-insensitive (so realignment and reflow do not break a pinned
   line, and a multi-line stanza can be one token) and word-boundary guarded (so `0x15` does not
   match `0x152`). `--self-check` refuses a token that is a bare identifier or bare literal, which
   is the shape the fragments had, so the table cannot slide back. The fixture suite pins all of
   it: a repointed encoder pin that leaves the old value in a trailing comment must fail, a widened
   `0x155` must fail, and a realigned line must not.

   **What an anchor is worth, stated precisely.** A present token proves the *citation* is still
   fresh. It is **not** evidence that the board behaves that way, that the value is right, or that
   the surrounding claim is true — the provenance classes exist because source text and silicon are
   different kinds of evidence, and a token check cannot collapse that distinction. In the other
   direction a missing token proves the citation is **stale**, not that the claim is false; the
   remedy is to re-read the source and re-derive the row, and the finding says so in those words.

   **An anchor does not check the line numbers a row cites.** The `R` rows cite lines —
   `platform_display_idf.c:55` – `:61` — and an anchor checks text. Insert forty lines above an
   anchored block and every token is still present, every anchor still reports fresh, and every
   `:NN` in the Markdown is now wrong. Line-citation drift is therefore **invisible to this layer
   by construction**, and that is a choice rather than an oversight: asserting line numbers would
   turn every unrelated edit above a cited line into a red documentation guard, which is how a
   check earns deletion. Keeping the cited lines correct stays human review, and is #213's. The
   checker's docstring, the finding wording, the JSON report, the step summary, the workflow
   commentary and the fixture notes all say so, because a green run invites exactly the opposite
   assumption.

   **The anchor count is not coverage.** `anchors_fresh=N/N` means every anchor in the table is
   fresh; it does **not** mean every `R` row is anchored, and no equality between the two is
   asserted anywhere — the fixture suite pins a floor and specific anchor ids, never a match
   against the number of rows. Rows stay unanchored for two reasons. **Negative and uniqueness
   claims** cannot be anchored at all: "no physical button is read anywhere in the firmware",
   "no `128` literal exists in the file and no code path implements the stale 50 % claim", and
   "`zone_label_event_cb` is the *only* emitter of
   `UI_INPUT_MENU`" are assertions about absence, and a token's presence would say nothing about
   everywhere else while its absence would say nothing at all. **Derived and external facts** are
   not quotable repository text: the 185-entry and 146-distinct-opcode counts come from reading the
   array, the demo-archive diff and the `difflib` similarity figures come from files outside this
   repository, and no row about the silicon is anchorable in principle. Those rows stay derived by
   human reading, and the anchors covering their neighbours must not be mistaken for covering them.

   **The guard is reachable locally as a direct command.** `python3
   scripts/check_docs_identity.py --self-check` is host-only — standard library, no ESP-IDF, no
   network, no writes — and is named in both `AGENTS.md` and `docs/dev/DEVELOPMENT.md`, which is
   where a contributor or an agent looks before editing documentation. A guard whose only home is a
   workflow is one contributors meet for the first time in a red pull request.

   It is deliberately not wired into `scripts/ci_sanity.sh`. An earlier cut put it at the top of
   that script and described it as the local gate; that description did not survive review.
   `ci_sanity.sh` runs `cmake` and then `idf.py build`, so it cannot run on a host without a
   toolchain — which is precisely the host this check is for — and no workflow in this repository
   invokes it, so calling it "the local gate" overstated what it is. The fixture suite runs the
   direct command instead, and pins the phrase both documents use, so the state of that script and
   the state of the documentation cannot disagree in either direction without a failure.

   The canonical record exercises this mechanism rather than relying on a synthetic example:
   its repository-observed row quotes the **stale comment's** 50 % duty claim before deriving the
   real value from Kconfig. `BACKLIGHT_DUTY` therefore exempts explicit stale/historical/comment
   attribution while still rejecting the same duty claim without that provenance language.

   There is **no arbitrary file-level marker exemption**: no comment, marker or front-matter
   key a document can carry will exempt that document, which is why `rk-ident-allow-file` was
   retired and is now reported as marker abuse. The one file-level skip that does exist is not
   a marker at all — it is the hard-coded, bot-managed `SKIP_FILES` entry described below,
   editable only by changing a Python file under review. Three narrow escapes exist, and each
   is reported when it stops doing work:

   - **Section allowances.** Named per file *and per heading*: the canonical record's
     "Historical claims, now superseded" section and this record's "Context" section. The
     canonical record's current-fact sections — identity summary, vendor-declared,
     repository-observed, panel-versus-component, artifact-observed, physically-unverified —
     are scanned like any other prose, so mutating a live identity row fails CI. The checker's
     self-check refuses to let an allowance be attached to a current-fact section, and a
     section allowance that no longer suppresses anything is a structure failure.
   - **`rk-ident-allow-next-line`.** For truthful statements about *other* boards. It covers
     exactly one following prose line, must name the guarded rule, must name the other target,
     must carry a reason of at least 20 characters, may not name the shipping target, and is
     rejected if the line it covers does not mention the declared target. It cannot be stacked
     and cannot reach a second line. Every use is visible in the diff.
   - **`SKIP_FILES`.** Currently only root `CHANGELOG.md`, because
     `.github/workflows/changelog.yml` writes GitHub release bodies verbatim into that file
     and commits them straight to `master`. A violation arriving that way could not be fixed
     by a documentation edit. The skip is by exact root-level path; the fixture suite asserts
     that `docs/CHANGELOG.md` and similarly named files are still scanned.

   **The rules are not permanent product policy.** Before a second shipping target or a real
   encoder-press input is documented, the relevant rules and clean fixtures must be revised or
   retired so truthful product/input prose does not accumulate line markers. Likewise, the
   hand-maintained required-document/link list — including the root `README.md`, where a reader
   first meets the product — intentionally turns a rename or removal into a
   Python edit. Required links are resolved relative to the referring document and must point to
   the existing canonical path; a dead path that merely ends in `board.md` fails. That coupling
   is the cost of making disappearance fail loudly. Issue
   [#213](https://github.com/muness/roon-knob/issues/213) owns that governance work — revising or
   retiring the product/input rules and their clean fixtures before a second target or a real
   encoder-press input is documented — and blocks #205 from making the workflow required. The
   fail-loud handling of unterminated comments and fences and the observability of `unless`
   suppressions are **no longer deferred to #213**: both ship here, described above and pinned by
   fixtures, because each of them was a way for the guard to report green on a document it had
   not read.

   The workflow is **advisory today** and is a safe candidate for
   [#205](https://github.com/muness/roon-knob/issues/205) to make required on `master`. Until
   then a red run is visible on the pull request but does not block a merge. It is a safe
   candidate specifically because it **always reports**: there is no `paths:` filter, so it runs
   on every pull request and every push to `master` rather than being *skipped* on changes that
   touch no Markdown. A required check that can be skipped is a required check that can be
   bypassed, which is why the path filters an earlier cut carried were removed. The job stays
   host-only — no ESP-IDF container, no artifacts, `contents: read` only, and its own
   `concurrency` group — so requiring it adds one short job and no coupling to
   `docker.yml` or to unmerged release-config work in PR #204.

   **Advisory red decays**, and that is a real cost rather than a neutral state: a check nobody
   has to pass is a check people learn to scroll past, and this one is deliberately loud. No review
   date is recorded here. An earlier draft set one; it was removed on review, because a date in a
   Markdown file and a workflow comment enforces nothing, expires without anyone noticing, and
   reads afterwards as a commitment that was quietly dropped. Whether this job becomes a required
   status check or is retired on its merits is tracked in
   [#213](https://github.com/muness/roon-knob/issues/213), which is also what gates #205 — a
   tracked issue can be scheduled, reassigned and closed; a sentence cannot.

   The workflow's step summary is rendered by `scripts/summarize_docs_identity.py`, not by an
   inline heredoc. The heredoc version indexed every detail row as `row["rule"]`, which is true
   of `violations` and false of `marker_abuse`, so the first marker-abuse finding replaced the
   summary with a `KeyError` — on exactly the runs that needed a summary. The renderer is now a
   file the fixture suite exercises against a synthetic report carrying all five finding groups
   plus the informational `suppressions` group at once, and it always exits 0 so it can never mask
   the checker's own exit code. Findings and informational rows are rendered separately, so a
   suppression is visible without reading as a failure and a clean run that merely excused
   something still carries the "nothing found in a known or tested wording" note.

   The report contract is documented alongside the workflow step that produces it: `token` and
   `exit_code` agree with the step's status; `violations`, `marker_abuse`, `structure_failures`,
   `missing` and `selfcheck_failures` are findings; `suppressions` is informational; and `rules`
   is the size of the rule table rather than a coverage figure.

7. **Public compatibility advice is corrected by evidence, not posted unilaterally.** The
   existing #117 reply describes the target as an "SH8601 (QSPI)" display, which decision 4
   shows is the software component's name rather than the vendor-declared driver IC.

   No draft reply is committed to this tree, and nothing has been posted to #117. The proposed
   wording will be included **in the pull request description** for review before that PR is
   marked ready, so it is read in the same place as the evidence it rests on. The PR description
   remains durable GitHub review metadata after merge; deliberately not committing the draft
   avoids shipping an unapproved public response as repository documentation. **Posting it to
   the issue requires explicit maintainer confirmation**, and this record does not grant it.

   The reply must rest only on facts this record establishes: the vendor declares an ST77916
   driver IC and the CST816 touch family, the panel is a 360×360 round IPS LCD, and the SoC is
   an ESP32-S3R8. Both surviving blockers must be stated rather than glossed — the GPIO pin
   mapping for any other board is unknown to this repository, and this firmware requires a
   rotary encoder that boards without one do not provide. Earlier guess-shaped language about
   what "should probably work" does not survive; where a fact is unverified the reply says so.

8. **Repository-observed facts are derived from values and behaviour, never from source
   comments.** Comments drift; the backlight-duty error described in Context is what that drift
   looks like when it reaches a provenance record. Every `R` row in the canonical record cites a value, a call, or
   the absence of a call — the row about the firmware reading no physical button cites the two
   `gpio_config()` and four `gpio_get_level()` call sites and the absence of any other, not the
   comment at `platform_input_idf.c:31` that happens to say the same thing. Where a stale
   comment exists it is named as stale.

9. **Physical and vendor invariants are revisited once #189 produces evidence.** Everything in
   the canonical record is vendor-declared, repository-observed or artifact-observed; nothing
   is measured on an owned device. When
   [#189](https://github.com/muness/roon-knob/issues/189)'s hardware checklist produces
   readings — `flash-id`, panel and touch markings, module package, encoder shaft, second
   encoder, PSRAM mode, board revision — the physically-unverified table is re-derived against
   them, and any vendor declaration they contradict is demoted to a superseded claim by the
   same process used here. Until then no `U` fact may be restated as verified anywhere.

## Consequences

**Good.** #203 and #193 inherit an attributable baseline instead of a contradiction. Porting
advice stops sending contributors after the wrong driver IC. The checker turns a relapse *in a
known or tested wording* into a CI failure instead of a later discovery — which is a smaller
claim than "makes a relapse a CI failure", and the smaller claim is the true one. Novel phrasings
still get through; see decision 6 for what is and is not covered, and review identity claims on
their merits rather than trusting a green check.

**Costs.** The checker guards phrasing, so a legitimate future statement of the superseded panel
technology, the superseded colour-depth figure, or the software component named as a driver IC
will fail CI unless it is quoted as source or carries a line allowance. Widened rules widen that
cost: the colour-depth rule now also reads bit depths and the encoder rule now also reads
click/push gestures on the knob or dial, so a sentence about a *different* board needs the
one-line marker where it previously needed nothing. The counterweight is
`scripts/fixtures/docs_identity/paraphrases_clean.tsv`, which pins fourteen truthful sentences
that share vocabulary with a guarded phrase and must stay accepted, so a rule cannot drift into a
topic ban unnoticed. Backticks alone no
longer buy an exemption: a span is treated as quoted code only when it carries a code signal
(an underscore, call syntax, statement punctuation, a hex literal, a path, a filename
extension), so `esp_lcd_sh8601` and `0x36` are code while a bare product or panel name in
backticks is still an assertion. Widening any exemption is a reviewed change to the checker,
not a documentation edit. That is deliberate friction: the failure mode being prevented is an
unqualified assertion.

Source anchors add a second, narrower coupling in the same spirit: an intentional refactor of
`platform_display_idf.c`, `platform_input_idf.c`, the touch BSP, `Kconfig.projbuild`,
`idf_component.yml`, `common/ui.c` or `common/bridge_client.c` that *changes* an anchored line turns
a documentation guard red. Anchoring whole lines widens that surface compared with fragments — a
rename or a reformat of the initialiser now trips it where two independent substrings would have
survived — and whitespace-insensitive matching is what buys most of it back, since realignment and
reflow do not trip anything. It is still friction on firmware work, and the fix is a small edit to
`SOURCE_ANCHORS` alongside the re-derived row, not a suppression. Anchors are limited to stable,
positively quotable text for the same reason; anything churn-prone stays out.

The compensating limit is that **moving** an anchored line costs nothing, which is also the layer's
blind spot: the cited line numbers go stale in silence. That trade is stated above and everywhere
the check reports, and it is a real gap, not a solved problem.

**Truth-preserving anchor false positives, and the standing rule for them.** Because an anchor
matches text, an edit that changes *how* a fact is written while leaving the fact intact fails the
anchor. These are false positives about the claim and true reports about the citation, and they are
predictable enough to name:

- **The same value written as an expression.** `.freq_hz = 5000,` rewritten as `.freq_hz = 5 * 1000,`
  is the same 5 kHz timer; the token misses. (A rewrite that also *changes* the value — `50 * 1000`,
  which is 50 kHz — is a genuine finding, and the row's "5 kHz" would then be wrong. The two look
  identical to the guard, which is exactly why the remedy is to re-read rather than to re-green.)
- **A reordered multi-line stanza.** `BACKLIGHT_KCONFIG_DEFAULT` pins
  `config RK_BACKLIGHT_NORMAL` with its `int`, `default 100` and `range 0 255` as one block, because
  `default 100` and `range 0 255` also appear under sibling symbols and only the stanza proves which
  symbol they belong to. Swapping `default` and `range` — Kconfig does not care — breaks the token
  while changing nothing about the symbol.
- Same class, different surface: renaming an anchored macro, splitting a designated initialiser
  across lines in a way whitespace-insensitivity cannot absorb, or replacing a literal with a
  `#define` that evaluates to it.

**The standing rule is: re-derive the row from the source and rewrite the token to the new text.
Never delete an anchor merely to go green.** Deleting the anchor is the one repair that removes the
evidence instead of refreshing it, and it is cheap, local and invisible in a passing run — which is
what makes it worth naming here rather than trusting to instinct. A row whose citation cannot be
re-derived is a row to re-derive or retire in the canonical record, not an anchor to drop.

Shrinkage of the anchor table is **already visible**, and deliberately visible as a floor rather than
as coverage: the fixture suite asserts at least thirteen anchors and asserts four named ids
(`QSPI_PINS`, `PANEL_INIT_ARRAY`, `ZONE_PICKER_TOUCH_ENTRY`, `ZONE_PICKER_HANDLER`) are present, so
removing an anchor fails the suite until someone also edits the floor — a diff line that has to be
defended, exactly like updating the suppression fingerprint. That floor says nothing about how many
repository-observed rows exist or are covered; it says the table has not quietly shrunk. The two
claims are different, and only the second is made.

Pinning the suppression fingerprint is friction of the same kind, and is the sharper of the two:
adding one legitimately qualified sentence to the documentation will fail the fixture suite until
someone updates a constant — including when it takes an excuse the baseline already lists, since the
per-triple count moves. Excluding line numbers and matched prose keeps that from firing on
unrelated edits, but a genuine new suppression still stops the suite. That is the cost of making an
invisible widening visible at all, and it is a review conversation rather than a claim about the
prose.

The local route is a direct command, `python3 scripts/check_docs_identity.py --self-check`, which
costs a contributor the memory of one line — or a glance at `AGENTS.md` or
`docs/dev/DEVELOPMENT.md`. It is not wired into `scripts/ci_sanity.sh`, so nothing runs it
automatically before a commit; that is the honest cost of not pretending a toolchain-dependent
script is a host-only gate.

The required-document/link list also couples an intentional document rename or removal to the
checker. That is deliberate while there is one shipping target: disappearance cannot look like
success. It must be revisited with the product/input rules under #213 before multi-target or
encoder-press support makes those assumptions false.

The retired `rk-ident-allow-file` marker is now itself a reported violation wherever it appears,
so the earlier file-level escape cannot return by copy-paste.

**Deliberately not decided here.** Flash geometry and size headroom (#203); managed-component
pinning and a committed dependency lock (#203); target-profile schema, runtime mismatch
enforcement, OTA policy, and web-flasher gating (#190, #200); the release-blocking hardware
checklist (#189); multi-target and encoder-press rule governance, and the fixtures that must be
revised with it (#213); required status checks on `master` (#205).

### What this does to #213 — a part, not a close

Source anchors and the suppression fingerprint deliver **part** of
[#213](https://github.com/muness/roon-knob/issues/213): some source-freshness checking, and
suppression governance with a reviewable baseline. They do **not** satisfy #213 and must not be
taken as closing it. #213 still owns, unchanged:

- **Line-citation drift.** Anchors check text, never the `:NN` a row cites, so pure line movement
  leaves citations stale with the guard green. Nothing here addresses it.
- **Unanchored `R` rows.** Negative and uniqueness claims, derived counts, diffs against the vendor
  archive, and every physical fact stay human-derived. The anchor table is a subset by design.
- **Anchor false positives are maintenance, not #213 work.** A truth-preserving rewrite of an
  anchored line — an expression form of the same value, a reordered Kconfig stanza — fails the anchor
  while the fact stands. The standing rule in *Consequences* applies now and needs nothing from #213:
  re-derive the row, rewrite the token, never delete the anchor to go green. It is listed here only
  so nobody defers a five-minute repair to an open issue, and so nobody reads "the anchor was
  removed" as "#213 will get to it".
- **Branch and alternation evidence.** Each rule is an alternation and each fixture proves one
  clause fires; the rule count is still not a coverage number, and unexercised clauses are still
  untested.
- **Multi-target fixtures.** Everything here assumes one shipping target. A second target needs the
  fixtures and the rules revised together.
- **Input-rule evolution.** The encoder/button rules encode "this firmware reads no button". A real
  encoder-press input makes truthful prose fail until those rules are revised or retired.
- **Required-document rename governance.** The hand-maintained required-document and link lists
  still turn an intentional rename or removal into a Python edit.
- **Whether this workflow becomes required or is retired**, which also gates #205.

The two things that were previously deferred to #213 and *are* discharged here are the fail-loud
handling of unterminated comments and fences, and the observability of `unless` suppressions.

## Alternatives rejected

- **A new `TARGET_IDENTITY.md`.** Rejected: two identity files is the defect, not the fix.
- **Wait for physical inventory.** Rejected: it leaves a wrong record published and blocks
  #203 on facts #203 does not need.
- **Call `esp_lcd_sh8601` a transport shim.** Rejected as overstated in the other direction —
  the component performs real DCS setup before the vendor array.
- **Prose-only correction with no check.** Rejected: the contradiction had already survived
  many edits across nine files, which is the signature of something diff review does not
  catch.
- **A file-level exemption for the two records that quote superseded claims.** Rejected on
  review: it left the canonical record's own current facts unscanned, which is the one file
  where a relapse matters most. Replaced by per-section allowances plus a per-line
  other-target marker.
- **Narrowing the scan to `docs/**`.** Rejected: identity claims live in root `README.md`,
  `AGENTS.md`, `CLAUDE.md`, `.github/RELEASE_TEMPLATE.md` and analysis notes too. The scan is
  repo-wide, with one named bot-managed exception.
- **Triggering the workflow on `paths: ['**/*.md', ...]`.** Rejected on review: a filtered job is
  *skipped*, not green, on pull requests that touch nothing it matches, and a required status
  check that can be skipped can be bypassed. The job is short and host-only, so it runs
  unconditionally instead.
- **Excluding every dot-directory from the scan.** Rejected: it made the documented scope and the
  real scope disagree, and it silently exempted `.github/RELEASE_TEMPLATE.md`, which is release
  prose about the target. Directories are now excluded only by exact name.
- **Rendering the CI step summary from a heredoc in the workflow.** Rejected: CI-only code is
  untested code, and this particular heredoc crashed with a `KeyError` on the first marker-abuse
  finding. It is a tested script now.
- **Treating any fence-shaped line as a fence toggle, and letting an unclosed comment or fence
  absorb the rest of a file.** Rejected: it made the widest exemption the checker has depend on a
  boolean that Markdown does not agree with, and both shapes let a document report green on prose
  the scan never read. Fences are matched CommonMark-shaped and an unclosed opener of either kind
  is a structure failure naming its line.
- **Making an unterminated comment or fence a `VIOLATION` rather than a `STRUCTURE` failure.**
  Rejected: the defect is that the scan could not see the document, not that a particular sentence
  was found in it, and `STRUCTURE` is the rank that already means "could not determine". It also
  outranks `VIOLATION`, which is the correct precedence for a state that hides violations.
- **Leaving `unless` suppressions implicit, or making a suppressed hit fail.** Both rejected. Left
  implicit, widening a suppressor shrinks the guard with no trace in any report; made a failure,
  truthful qualified prose — a pixel-format note, a datasheet reference — could not be written at
  all. Suppressions are recorded and reported, and the exit code stays 0.
- **Anchoring the canonical record's negative rows** — "no physical button is read anywhere",
  "no 128 literal exists in the file". Rejected: an absence cannot be established by asserting a
  token. Requiring a token would prove nothing about the rest of the tree, and its disappearance
  would prove nothing at all, so the anchor would be theatre attached to the rows that most need
  real derivation. Those rows stay human-derived, and the checker's docstring says why.
- **Treating a source anchor as evidence the fact is true.** Rejected as exactly the conflation
  this record exists to prevent: source text and physical silicon are different provenance classes,
  and a `grep` cannot promote one to the other. An anchor asserts only that the citation is still
  fresh, and its failure wording says "re-read the source", never "the claim is false".
- **Parsing the anchored sources instead of matching tokens.** Rejected: a C or Kconfig parser is a
  large dependency and a large maintenance surface for a check whose job is to notice that a cited
  line changed. Tokens are legible in review.
- **Splitting a whitespace-sensitive line into the fragments that do not move.** Rejected on review,
  having been the first cut. `PIN_NUM_BK_LIGHT` and `((gpio_num_t)47)` as two tokens are both
  satisfied by a file in which the name and the number no longer describe each other, so the anchor
  would certify a relationship it had not checked. Whole lines and whole blocks are pinned instead,
  and matching is made whitespace-insensitive so alignment is not what the assertion rests on.
  `--self-check` now refuses a bare identifier or literal as a token.
- **Asserting the line numbers the `R` rows cite.** Rejected: every unrelated edit above a cited
  line would turn a documentation guard red, and a guard that fails for reasons unrelated to its
  subject gets deleted or ignored. The consequence — line-citation drift is invisible here — is
  stated in the checker, the findings, the report, the summary, the workflow and the fixtures rather
  than papered over, and remains #213's.
- **Reporting `anchors_fresh` as though it were coverage of the `R` rows.** Rejected as the same
  overstatement the rule count already had to have removed from it. The anchor table is a subset;
  negative, uniqueness, derived and physical rows cannot be anchored. The report carries the
  checker's own scope note, the step summary renders it beneath the count, and the fixture suite
  asserts a floor rather than an equality.
- **Pinning the suppression *total*, or making a suppression fail the checker.** Both rejected. A
  scalar total cannot see one suppressor swapped for another at an unchanged total, which is what a
  widening looks like once a suppressed line is also removed; making a suppression fail would mean
  truthful qualified prose could not be written at all. An exact `rule|file|suppressor|count`
  fingerprint is pinned in the fixture suite instead — excluding line numbers and matched prose so it
  does not churn, and counting per triple so an already-listed excuse is not a licence for further
  hits — so the consequence is a review conversation and the checker's own exit contract is
  unchanged.
- **Deriving the fingerprint's shape proofs from the live suppression set.** Rejected on review,
  having been the first cut. The control proving the fingerprint ignores line numbers and matched
  prose compared the live fingerprint against the pinned constant, so a legitimate new suppression
  failed it too — reporting a false "the fingerprint is broken" alongside the true "the baseline
  moved". The proofs run against a fixed synthetic set now, so one cause produces one failure.
- **Exempting four-space indented blocks as code.** Rejected: telling an indented code block from a
  continuation line needs list and blockquote context, and an indentation-only exemption would exempt
  the reflowed middle of a paragraph — including the indented renderings the wrap audit uses, which
  are the guard's only recall evidence. Indented blocks stay scanned, and the cost is paid in
  discoverability instead: every violation report names the remedy, and a fixture pins that the remedy
  works.
- **Leaving the guard reachable only from CI.** Rejected: contributors would first meet it as a red
  pull request. The local route is the direct command, named in `AGENTS.md` and
  `docs/dev/DEVELOPMENT.md`. No global or per-user agent instruction was added — the repository's own
  files are the right scope.
- **Wiring the guard into `scripts/ci_sanity.sh` and calling that the local gate.** Rejected on
  review, having also been the first cut. That script's next step is `idf.py build`, so it cannot
  run on a host without ESP-IDF — the host this check exists for — and no workflow invokes it, so
  "the local gate" was a claim about a script that is not demonstrably live. The fixture suite pins
  the direct command and the phrase both documents use instead.
- **Recording a calendar date for reviewing the advisory status.** Rejected: a date in a Markdown
  file and a workflow comment enforces nothing and expires unnoticed, leaving a commitment that
  reads as abandoned. #213 tracks it, because an issue can be scheduled and closed.
- **Extend an existing workflow.** Rejected: `.github/workflows/docker.yml` is owned by open
  PR [#204](https://github.com/muness/roon-knob/pull/204), and a docs check has no reason to
  depend on a firmware container.
