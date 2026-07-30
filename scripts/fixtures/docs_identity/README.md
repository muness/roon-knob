# `check_docs_identity` fixtures

These files deliberately contain the identity contradictions that
[`scripts/check_docs_identity.py`](../../check_docs_identity.py) exists to reject. The checker
skips `scripts/fixtures` for exactly that reason — see `SKIP_RELPATHS` — and
`scripts/test_check_docs_identity.sh` asserts that the skip is real and that the same files are
still scanned anywhere else.

| File | Purpose |
|---|---|
| `violation_<RULE_ID>.md` | Must make rule `<RULE_ID>` fire. One per rule; the harness reads the rule list from `--list-rules`, so adding a rule without a fixture fails the suite. |
| `clean_all.md` | Every corrected wording in one file. Must produce **zero** violations, proving the rules are specific rather than matching any mention of the subject. |
| `code_context_exempt.md` | The guarded phrases appear only inside a fenced block, a link target, or an autolink. Must produce zero violations. |
| `code_context_bare.md` | The same phrases in prose. Must produce violations — this is what stops the code exemption from swallowing everything. |
| `indented_code_scanned.md` | The guarded phrases inside a **four-space indented** block. Must produce violations: indented code is CommonMark code but is scanned here as prose, so it is the one exemption a reader expects and does not get. The suite also asserts the violation output, the JSON report (`quoting_note`) and the step summary all name the remedy — fence the quotation — and that the same lines inside a fence are exempt, so the remedy is a real one. |
| `fence_valid.md` | Well-formed fences only: an info string on the opener, a tilde fence containing a backtick fence, a four-backtick fence closed by five, and a three-space-indented fence. Guarded wordings inside them must stay exempt, so this must produce zero violations. |
| `fence_mismatched.md` | A backtick fence "closed" by tildes, with a contradiction after it. Markdown keeps the fence open, so the checker must report an **unterminated code fence** structure failure naming the opening line — never `RK-IDENT-OK`. |
| `fence_short_closer.md` | A four-backtick fence "closed" by three, with a contradiction after it. Same requirement: a named unterminated-fence structure failure, because a closer must be at least as long as its opener. |
| `comment_closed.md` | One-line, multi-line and mid-line HTML comments, all properly closed, containing guarded wordings. Must produce zero violations, and the prose after the closing marker must still be scanned. |
| `comment_unterminated.md` | An HTML comment opener with no closing marker, with a contradiction after it. Must be reported as an **unterminated HTML comment** structure failure naming the opening line — never `RK-IDENT-OK`. |
| `suppressed_hits.md` | Truthful qualified prose that trips a rule and is then excused by that rule's `unless` clause. Must exit 0 **and** must appear in `suppressions` with file, line, rule id, matched text and the suppressor that excused it, so suppression stays observable. |
| `code_identifiers.md` | Backtick spans that carry a code signal (underscore, call syntax, statement punctuation, hex literal, path, filename extension). Must produce zero violations. |
| `backtick_claims.md` | Bare product, panel, controller and module claims wrapped in backticks. Must produce violations: backticks alone are not a code signal, so they no longer buy an exemption. |
| `other_target_marked.md` | A truthful claim about a **different** board, carrying a well-formed `rk-ident-allow-next-line` allowance that lists both rules the line trips. Must produce zero violations. |
| `other_target_unmarked.md` | The same truthful sentence with no allowance. Must produce a violation, so the escape is doing real work. |
| `other_target_second_line.md` | A valid allowance followed by a covered line **and** an uncovered one. Must produce a violation for the second line only. |
| `other_target_unrelated_line.md` | An allowance whose covered line never mentions the declared other target. Must be reported as marker abuse. |
| `other_target_shipping.md` | An allowance naming the shipping target. Must be reported as marker abuse. |
| `other_target_no_reason.md` | An allowance with a token reason. Must be reported as marker abuse. |
| `retired_marker.md` | The retired file-level `rk-ident-allow-file` comment. Must be reported as marker abuse, and the contradiction it tried to hide must still be reported. |
| `paraphrases.tsv` | `RULE_ID`↹sentence. Each sentence is planted alone in a throwaway tree and must be reported for that rule. None is a probe, an antiprobe, or a wording this repository ever used — this file is the checker's only evidence of recall beyond the wordings it was written from. |
| `paraphrases_clean.tsv` | `why`↹sentence. Truthful prose that shares vocabulary with a guarded phrase. Each must produce **zero** violations, so strengthening a rule cannot quietly turn it into a topic ban. |
| `report_all_groups.json` | A synthetic `--report` payload carrying `violations`, `marker_abuse`, `structure_failures`, `missing`, `selfcheck_failures` and `suppressions` at once. Drives the tests for [`scripts/summarize_docs_identity.py`](../../summarize_docs_identity.py); the `marker_abuse` rows are the shape that used to crash the workflow's inline renderer with a `KeyError`, and the `suppressions` rows are the informational group, which is rendered but must not count as a finding. |

The paraphrase recall claim is scoped to the single-physical-line renderings in the corpus. The
harness also plants `CONTROLLER_CLAIM`, `BACKLIGHT_DUTY`, and `ENCODER_BUTTON` phrases split
across two lines and requires each to pass, pinning physical-line wrapping as a known blind spot
rather than silently treating it as coverage.

Wording here is written independently of the checker's own `probe`/`antiprobe` strings, so the
suite is not merely asserting that the regexes match themselves. That independence is the whole
value of `paraphrases.tsv`: a rule that matches only its own probe passes the per-rule fixture
loop and still catches nothing a reviewer would write.

## Two things with no fixture file here

**Source anchors** are checked against the repository's *real* sources, so there is nothing to
fixture. `SOURCE_ANCHORS` asserts that each file cited by the canonical record's
repository-observed rows still carries its expected tokens. The harness asks the checker for the
list (`--list-anchors`) and copies exactly those files into each throwaway root, which is what
keeps a temporary tree self-contained without dragging in the firmware tree — add an anchor and the
roots pick it up with no edit here.

Each token is a whole line or a whole block that states the relationship the row cites, matched
whitespace-insensitively and word-boundary guarded. Four discriminating cases pin that:

| Case | Mutation | Requirement |
|---|---|---|
| stale token | rewrite the `CONFIG_RK_BACKLIGHT_NORMAL` duty assignment | named non-zero `STRUCTURE` finding, exactly one anchor |
| absent file | delete `idf_app/main/idf_component.yml` | named non-zero `STRUCTURE` finding, exactly one anchor |
| broken relationship | repoint encoder A to `GPIO_NUM_9`, leaving `GPIO_NUM_8` in a trailing comment | must still fail — a split `#define ENCODER_GPIO_A` + `GPIO_NUM_8` pair would not |
| prefix | widen `TOUCH_ADDR 0x15` to `0x155` | must fail — a token may not be satisfied by a longer literal starting with it |

Whitespace flexibility is bounded on both sides, and each bound is pinned by a case that must
stay green or must fail:

| Case | Mutation | Requirement |
|---|---|---|
| realignment | collapse the padding in `#define ENCODER_GPIO_A    GPIO_NUM_8` | must stay green — pinning whole lines is only reasonable if reformatting does not break them |
| punctuation join | unwrap `SH8601_PANEL_IO_QSPI_CONFIG(` + `PIN_NUM_LCD_CS,` onto one line, **both** with and without a space after `(` | must stay green — at a punctuation boundary the gap may close entirely, so unwrapping an argument list is not a stale citation |
| word fusion | fuse `#define PIN_NUM_BK_LIGHT` into `#definePIN_NUM_BK_LIGHT` | must fail — whitespace between two *word* lexemes stays mandatory, or two identifiers fused into one would satisfy the token |

`--self-check` additionally refuses an anchor with no tokens and one whose token is a bare
identifier or literal, since both are ways this layer rots into decoration.

Three limits are asserted in the wording of the findings and in the report, not just documented:

- an anchor proves a **citation is fresh**, never that a fact is true, so no finding may be worded
  as evidence the documented claim is false;
- an anchor **does not check the line numbers** the rows cite (`platform_display_idf.c:55` – `:61`).
  Pure line movement leaves every one of those stale with every token still present and the guard
  still green. The suite deliberately asserts no line numbers — doing so would make every unrelated
  edit above a cited line a red documentation guard. Keeping the citations correct is human review,
  tracked in [#213](https://github.com/muness/roon-knob/issues/213);
- the anchor count is **not coverage**. The table covers a subset of the repository-observed rows.
  Negative rows ("no physical button is read anywhere") and uniqueness rows ("the *only* emitter of
  `UI_INPUT_MENU`") are absence claims that no token can establish; derived counts, diffs against
  the vendor archive, and anything about the silicon are not quotable repository text. The suite
  asserts a floor on the anchor table and the presence of specific anchors — never an equality with
  the number of rows.

**The suppression fingerprint** is a constant in `scripts/test_check_docs_identity.sh`
(`EXPECTED_SUPPRESSION_FINGERPRINT`): one `rule|file|suppressor|count` line per distinct triple,
sorted. Line numbers and matched prose are excluded on purpose, so a moved line or a reworded
sentence does not churn it. What it *does* catch is one suppressor being swapped for another, which
a scalar total could not see at an unchanged count — and, because of the trailing count, an
*additional* suppressed hit inside a triple the baseline already lists. Without that count an
existing baseline line is a standing licence: further sentences in the same file could take the same
excuse from the same rule and nothing would move.

Those three properties are proven against a **fixed synthetic** suppression set, not against this
repository's live one, and the distinction matters. A control derived from the live baseline fails
the moment the repository legitimately gains or loses a suppression, so one honest documentation edit
would report both "the baseline moved" (true, and the intended friction) and "the fingerprint no
longer ignores line numbers" (false, and a diagnostic aimed at the wrong thing). With synthetic rows,
a legitimate live change fails exactly one assertion with exactly one cause.

That failure is review friction, not a runtime failure of truthful prose: suppressions never change
the checker's exit code, and a run whose only findings are suppressions is still `RK-IDENT-OK`. The
failure message prints a diff of pinned versus actual, names the command to run, and says what to
establish in review before updating the constant.

Neither of these is wired into `scripts/ci_sanity.sh`; the guard's local route is the direct
command named in `AGENTS.md` and `docs/dev/DEVELOPMENT.md`, and the suite proves that command works
on a host with no ESP-IDF.
