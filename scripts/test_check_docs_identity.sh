#!/usr/bin/env bash
# Host tests for scripts/check_docs_identity.py (issue #211).
#
# Deterministic, offline, no network, no writes inside the repository: every
# mutation happens in a throwaway copy of the repository's Markdown under
# $TMPDIR.
#
# The suite is built so it cannot pass vacuously:
#   * every rule reported by --list-rules must have a fixture that makes it fire;
#   * a file containing all the corrected wordings must produce zero violations;
#   * the code exemption is proven not to swallow prose, and backticks alone are
#     proven not to be an exemption;
#   * the canonical record's *current-fact* sections are proven to be scanned, by
#     mutating a live row of its identity table;
#   * the section allowance is proven to be section-scoped, not file-scoped, and a
#     heading that exists only inside an HTML comment is proven to neither clear it
#     nor grant it -- section state is read from visible text;
#   * every branch of the other-target line allowance is proven, in both
#     directions;
#   * the CHANGELOG.md skip is proven to be exactly one root-level file;
#   * fenced code is exempt only when the fence is well formed: an unterminated,
#     mismatched or short-closed fence, and an unterminated HTML comment, are all
#     proven to FAIL loudly instead of absorbing the rest of the file;
#   * rule-level `unless` suppressions are proven observable in the report, the
#     counters and the step summary, and removing that observability is proven to
#     fail --self-check, and the repository's suppression *set* is pinned as an
#     exact rule|file|suppressor|count fingerprint -- whose shape is proven against a
#     fixed synthetic set, not the live one, to discriminate a one-for-one
#     substitution at unchanged total, to discriminate an additional suppressed hit
#     inside an already-pinned triple, and to stay independent of line numbers and
#     matched prose -- so a widened suppressor becomes review friction;
#   * a four-space indented block is proven to be scanned as prose rather than
#     exempted as code, and the violation output, the JSON report and the step
#     summary are proven to name the remedy (fence the quotation), because that is
#     the one exemption a reader expects and does not get;
#   * source anchors are proven discriminating in both directions -- mutating one
#     expected token and deleting one cited source file each produce a named,
#     non-zero STRUCTURE finding, and neither is worded as proof the claim is false;
#   * anchor tokens are proven relationship-preserving: splitting one whole-line
#     token into independently-satisfiable halves is proven to fail --self-check,
#     and a token is proven not to match a longer identifier or literal;
#   * anchor whitespace flexibility is proven bounded on both sides: a punctuation
#     join may close entirely (the QSPI macro call is pinned both spaced and
#     unspaced on one line) while whitespace between two word lexemes stays
#     mandatory, so fusing two identifiers is proven not to satisfy a token;
#   * --quiet is proven to gate only the per-hit suppression and violation lines,
#     and its help text is pinned to that behaviour;
#   * the step renderer is proven to answer --help with argparse's help rather than
#     with its "invoked incorrectly" summary, while still naming a real bad
#     invocation, and to exit 0 in both cases;
#   * the guard is proven reachable locally as a direct host-only command that both
#     AGENTS.md and docs/dev/DEVELOPMENT.md name;
#
# What the suite does NOT assert, deliberately:
#   * that the cited *line numbers* in the canonical record are correct. Anchors
#     check text, so pure line movement leaves a citation stale with every token
#     present. Asserting line numbers here would make every unrelated edit above a
#     cited line a red documentation guard. That is human review, tracked in #213;
#   * that the anchor table covers every repository-observed row. It covers a
#     subset; negative, uniqueness, derived and physical rows are not anchorable.
#   * an empty tree, a deleted canonical record and a gutted one must all FAIL;
#   * the checker's own --self-check is proven live by breaking a copy of it.
#
# The suite deliberately does *not* assert that the rule count equals the number of
# covered alternation branches. Each rule is an alternation; the per-rule fixture
# proves one clause fires. Individual clauses are pinned only by paraphrases.tsv.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECKER="$REPO/scripts/check_docs_identity.py"
FIXTURES="$REPO/scripts/fixtures/docs_identity"
BOARD="docs/esp/hw-reference/board.md"
ADR="docs/meta/decisions/2026-07-30_DECISION_TARGET_IDENTITY_PROVENANCE.md"

PY="${PYTHON:-python3}"

# The repository's current suppression *set*, pinned as an exact fingerprint: one
# `rule|file|suppressor|count` line per distinct triple, sorted by Python's
# codepoint order (not the shell's, so the locale cannot move a line).
#
# The trailing count is load-bearing. Without it, a triple that already appears in
# the baseline is a standing licence: a second, third and fourth sentence in the
# same file could take the same excuse from the same rule and the fingerprint would
# not move, which is a widening of exactly the kind this baseline exists to make
# visible. With it, every additional suppressed hit in an existing triple fails,
# and the diff says how many.
#
# Line numbers and matched prose are still deliberately excluded, and the count is
# not a substitute for them: it says how many hits a triple excuses, never which
# lines or which words. They churn on every unrelated edit above a suppressed line
# and on every rewording of the sentence, so including them would make this
# baseline fail constantly for reasons that are not the thing being watched. What IS
# being watched is which suppressor excuses which rule in which file, and how often
# -- so swapping one suppressor for another, moving a suppressed hit to a different
# file, or adding one to a triple already listed all fail. A scalar total could see
# none of the three.
#
# See the "the suppression fingerprint is pinned" block near the end of this file
# for what to do when it fails.
EXPECTED_SUPPRESSION_FINGERPRINT="$(cat <<'EOF'
BACKLIGHT_DUTY|docs/esp/hw-reference/board.md|stale|1
BACKLIGHT_DUTY|docs/meta/decisions/2026-07-30_DECISION_TARGET_IDENTITY_PROVENANCE.md|stale|1
TOUCH_VARIANT|docs/esp/TOUCH_INPUT.md|datasheet|1
TOUCH_VARIANT|docs/esp/hw-reference/board.md|marking|1
TOUCH_VARIANT|docs/esp/hw-reference/cst816d.md|Datasheet|1
TOUCH_VARIANT|docs/esp/hw-reference/cst816d.md|datasheet|1
EOF
)"

# fingerprint <report.json> -> sorted `rule|file|suppressor|count` lines on stdout
#
# One line per distinct triple, carrying the number of suppressed hits it covers.
# `line` and `match` are never read, which is what keeps the baseline quiet on line
# movement and rewording; the count is what keeps it loud on an extra hit.
fingerprint() {
  "$PY" -c '
import collections, json, sys
rows = json.load(open(sys.argv[1])).get("suppressions", [])
counts = collections.Counter(
    "%s|%s|%s" % (r.get("rule"), r.get("file"), r.get("suppressor")) for r in rows
)
for key in sorted(counts):
    print("%s|%d" % (key, counts[key]))
' "$1" 2>/dev/null
}

PASS=0
FAIL=0
WORK="$(mktemp -d "${TMPDIR:-/tmp}/rk-ident-tests.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# Source files the checker's SOURCE_ANCHORS cite, asked of the checker rather than
# duplicated here, so adding an anchor cannot leave the temporary roots behind.
# `fresh_tree` copies exactly these and nothing else from idf_app/, which keeps a
# throwaway root self-contained without dragging in the firmware tree.
ANCHOR_PATHS="$("$PY" "$CHECKER" --list-anchors 2>/dev/null | cut -f2 | sort -u)"

ok() { PASS=$((PASS + 1)); printf '  ok   %s\n' "$1"; }
bad() { FAIL=$((FAIL + 1)); printf '  FAIL %s\n' "$1"; }

# run <root> [extra args...] -> sets RUN_OUT and RUN_RC
run() {
  local root="$1"
  shift
  RUN_OUT="$("$PY" "$CHECKER" --root "$root" "$@" 2>/dev/null)"
  RUN_RC=$?
}

# expect_rc <label> <want_rc> <root> [extra args...]
expect_rc() {
  local label="$1" want="$2" root="$3"
  shift 3
  run "$root" "$@"
  if [ "$RUN_RC" -eq "$want" ]; then
    ok "$label (exit $want)"
  else
    bad "$label: wanted exit $want, got $RUN_RC"
    printf '%s\n' "$RUN_OUT" | sed 's/^/       | /'
  fi
}

# expect_out <label> <needle>  (inspects the last run)
expect_out() {
  local label="$1" needle="$2"
  if printf '%s' "$RUN_OUT" | grep -qF -- "$needle"; then
    ok "$label"
  else
    bad "$label: output did not contain '$needle'"
    printf '%s\n' "$RUN_OUT" | sed 's/^/       | /'
  fi
}

expect_no_out() {
  local label="$1" needle="$2"
  if printf '%s' "$RUN_OUT" | grep -qF -- "$needle"; then
    bad "$label: output unexpectedly contained '$needle'"
    printf '%s\n' "$RUN_OUT" | sed 's/^/       | /'
  else
    ok "$label"
  fi
}

# fresh_tree <name> -> echoes a path holding a pristine copy of the scanned tree
#
# Self-contained: the scanned Markdown, plus exactly the source files SOURCE_ANCHORS
# cites (asked of the checker, not hard-coded), and nothing else. Without the
# anchor files every throwaway root would report a wall of stale-citation structure
# failures that have nothing to do with the case under test.
fresh_tree() {
  local dest="$WORK/$1" src
  rm -rf "$dest"
  mkdir -p "$dest"
  cp -R "$REPO/docs" "$dest/docs"
  cp "$REPO/README.md" "$dest/README.md"
  while IFS= read -r src; do
    [ -n "$src" ] || continue
    mkdir -p "$dest/$(dirname "$src")"
    cp "$REPO/$src" "$dest/$src"
  done <<< "$ANCHOR_PATHS"
  printf '%s' "$dest"
}

# rewrite <file> <sed script>  -- edit in place inside a throwaway tree
rewrite() {
  local target="$1" script="$2"
  sed "$script" "$target" > "$target.tmp" && mv "$target.tmp" "$target"
}

echo "== baseline =="

expect_rc "real repository docs are accepted" 0 "$REPO" --self-check
expect_out "baseline prints the OK token" "RK-IDENT-OK"
expect_no_out "baseline reports no violations" "RK-IDENT-VIOLATION"
expect_no_out "baseline reports no marker abuse" "RK-IDENT-MARKER-ABUSE"

BASE="$(fresh_tree baseline)"
expect_rc "pristine copy of the doc tree is accepted" 0 "$BASE"

echo "== the scan is repo-wide, not docs-only =="

ROOTLEVEL="$(fresh_tree rootlevel)"
cp "$FIXTURES/violation_PANEL_AMOLED.md" "$ROOTLEVEL/AGENTS.md"
expect_rc "a contradiction in a root-level non-docs file is caught" 1 "$ROOTLEVEL"
expect_out "the root-level file is named" "AGENTS.md"

NESTED="$(fresh_tree nested)"
mkdir -p "$NESTED/web/notes"
cp "$FIXTURES/violation_CONTROLLER_CLAIM.md" "$NESTED/web/notes/panel.md"
expect_rc "a contradiction outside docs/ is caught" 1 "$NESTED"
expect_out "the nested file is named" "web/notes/panel.md"

echo "== .github is scanned, and the exclusions are exactly the named ones =="

# The documented scope said "every Markdown file in the repository" while the
# implementation excluded every dot-directory, so .github/RELEASE_TEMPLATE.md --
# release prose that describes the target -- was never read. Pin the real file.
SCOPEREPORT="$WORK/scope.json"
run "$REPO" --self-check --report "$SCOPEREPORT"
if [ -s "$SCOPEREPORT" ] && "$PY" -c "
import json,sys
d=json.load(open(sys.argv[1]))
assert '.github/RELEASE_TEMPLATE.md' in d['files_scanned'], [f for f in d['files_scanned'] if f.startswith('.')]
" "$SCOPEREPORT" 2>/dev/null; then
  ok ".github/RELEASE_TEMPLATE.md is in the scanned file list"
else
  bad ".github/RELEASE_TEMPLATE.md was not scanned"
fi

GH="$(fresh_tree github_scanned)"
mkdir -p "$GH/.github/ISSUE_TEMPLATE"
cp "$FIXTURES/violation_PANEL_AMOLED.md" "$GH/.github/RELEASE_TEMPLATE.md"
expect_rc "a contradiction in .github/RELEASE_TEMPLATE.md is caught" 1 "$GH"
expect_out "the .github file is named" ".github/RELEASE_TEMPLATE.md"

GHNEST="$(fresh_tree github_nested)"
mkdir -p "$GHNEST/.github/ISSUE_TEMPLATE"
cp "$FIXTURES/violation_CONTROLLER_CLAIM.md" "$GHNEST/.github/ISSUE_TEMPLATE/bug.md"
expect_rc "a contradiction nested under .github is caught" 1 "$GHNEST"
expect_out "the nested .github file is named" ".github/ISSUE_TEMPLATE/bug.md"

# No dot-directory rule at all: a directory is exempt only by exact name.
DOTDIR="$(fresh_tree dot_directory)"
mkdir -p "$DOTDIR/.notes"
cp "$FIXTURES/violation_PANEL_AMOLED.md" "$DOTDIR/.notes/scratch.md"
expect_rc "an unlisted dot-directory is still scanned" 1 "$DOTDIR"
expect_out "the dot-directory file is named" ".notes/scratch.md"

# ... and the named exclusions really do exclude. All of them at once, so a
# missing entry shows up as a failure rather than as a passing subset.
EXCL="$(fresh_tree exclusions)"
for d in build node_modules managed_components dependencies __pycache__ .venv venv \
         .mypy_cache .pytest_cache .ruff_cache; do
  mkdir -p "$EXCL/$d/nested"
  cp "$FIXTURES/violation_PANEL_AMOLED.md" "$EXCL/$d/nested/scratch.md"
done
expect_rc "every named excluded directory is excluded, at depth" 0 "$EXCL"

echo "== a link label that is a filename is a reference, not a claim =="

LABEL="$(fresh_tree link_label)"
{
  printf '# Cross references\n\n'
  printf -- '- [cst816d.md](../esp/hw-reference/cst816d.md) - integration notes\n'
} > "$LABEL/docs/scratch.md"
expect_rc "a filename link label does not trip TOUCH_VARIANT" 0 "$LABEL"

LABELPROSE="$(fresh_tree link_label_prose)"
{
  printf '# Not a reference\n\n'
  printf 'The fitted touch controller is a CST816D on this unit.\n'
} > "$LABELPROSE/docs/scratch.md"
expect_rc "the same variant asserted in prose is still rejected" 1 "$LABELPROSE"
expect_out "prose variant assertion names TOUCH_VARIANT" "RK-IDENT-VIOLATION: TOUCH_VARIANT"

echo "== the CHANGELOG skip is exactly one root-level file =="

CHANGELOG="$(fresh_tree changelog)"
cp "$FIXTURES/violation_PANEL_AMOLED.md" "$CHANGELOG/CHANGELOG.md"
expect_rc "bot-managed root CHANGELOG.md is skipped" 0 "$CHANGELOG"

CHANGEELSE="$(fresh_tree changelog_elsewhere)"
cp "$FIXTURES/violation_PANEL_AMOLED.md" "$CHANGEELSE/docs/CHANGELOG.md"
expect_rc "docs/CHANGELOG.md does not inherit the skip" 1 "$CHANGEELSE"
expect_out "docs/CHANGELOG.md is named" "docs/CHANGELOG.md"

CHANGENEAR="$(fresh_tree changelog_lookalike)"
cp "$FIXTURES/violation_PANEL_AMOLED.md" "$CHANGENEAR/CHANGELOG_NOTES.md"
expect_rc "a similarly named root file does not inherit the skip" 1 "$CHANGENEAR"
expect_out "the lookalike file is named" "CHANGELOG_NOTES.md"

echo "== fixture corpus is not scanned, and the skip is scoped =="

FIXSCAN="$(fresh_tree fixscan)"
mkdir -p "$FIXSCAN/scripts/fixtures/docs_identity"
cp "$FIXTURES"/violation_*.md "$FIXSCAN/scripts/fixtures/docs_identity/"
expect_rc "contradictions under scripts/fixtures are skipped" 0 "$FIXSCAN"

FIXELSE="$(fresh_tree fixelsewhere)"
mkdir -p "$FIXELSE/docs/fixtures"
cp "$FIXTURES/violation_PANEL_AMOLED.md" "$FIXELSE/docs/fixtures/scratch.md"
expect_rc "the same file elsewhere is still scanned" 1 "$FIXELSE"

echo "== every rule has a fixture, and every fixture fires =="

RULES="$("$PY" "$CHECKER" --list-rules 2>/dev/null)"
RULE_RC=$?
if [ "$RULE_RC" -eq 0 ] && [ -n "$RULES" ]; then
  ok "--list-rules exits 0 and prints rule ids"
else
  bad "--list-rules failed (rc=$RULE_RC)"
fi

RULE_COUNT=0
while IFS= read -r rule; do
  [ -n "$rule" ] || continue
  RULE_COUNT=$((RULE_COUNT + 1))
  fixture="$FIXTURES/violation_${rule}.md"
  if [ ! -f "$fixture" ]; then
    bad "rule $rule has no fixture at scripts/fixtures/docs_identity/violation_${rule}.md"
    continue
  fi
  tree="$(fresh_tree "rule_$rule")"
  cp "$fixture" "$tree/docs/scratch.md"
  run "$tree"
  if [ "$RUN_RC" -eq 1 ] && printf '%s' "$RUN_OUT" | grep -qF "RK-IDENT-VIOLATION: $rule "; then
    ok "rule $rule fires on its fixture and exits 1"
  else
    bad "rule $rule did not fire on its fixture (rc=$RUN_RC)"
    printf '%s\n' "$RUN_OUT" | sed 's/^/       | /'
  fi
done <<< "$RULES"

# Population only. This is not a coverage number: each rule is an alternation and
# the loop above proves one clause of it fires, not all of them.
if [ "$RULE_COUNT" -ge 9 ]; then
  ok "rule table is populated ($RULE_COUNT rules; not a branch-coverage figure)"
else
  bad "expected at least 9 rules, found $RULE_COUNT"
fi

# Governance triggers belong beside the rules they constrain. Pin their unique
# text so a refactor cannot silently drop the revise/retire path that #213 relies on.
for trigger in \
  "this rule guards today's target, not permanent product policy" \
  "a future target may truthfully carry this panel technology" \
  "this rule guards a currently phantom input, not future policy"; do
  if grep -qF "$trigger" "$CHECKER"; then
    ok "rule-local governance trigger remains documented: $trigger"
  else
    bad "rule-local governance trigger disappeared: $trigger"
  fi
done

echo "== rules are specific, not blanket topic bans =="

CLEAN="$(fresh_tree clean_all)"
cp "$FIXTURES/clean_all.md" "$CLEAN/docs/scratch.md"
expect_rc "all corrected wordings together produce no violations" 0 "$CLEAN"

echo "== independent paraphrases are caught, one sentence at a time =="

# The point of this block: none of these sentences is a rule probe, a rule
# antiprobe, or a wording that ever appeared in this repository. A rule that only
# matches its own probe passes the per-rule fixture loop above and still has no
# recall, which is exactly what a dissent pass demonstrated. Each sentence is
# planted alone, so a failure names the sentence rather than the file.
PARA="$FIXTURES/paraphrases.tsv"
if [ ! -f "$PARA" ]; then
  bad "paraphrase corpus missing at scripts/fixtures/docs_identity/paraphrases.tsv"
else
  PARA_TREE="$(fresh_tree paraphrase)"
  PARA_N=0
  while IFS="$(printf '\t')" read -r rule sentence; do
    case "$rule" in ''|'#'*) continue ;; esac
    [ -n "$sentence" ] || continue
    PARA_N=$((PARA_N + 1))
    {
      printf '# Paraphrase %d\n\n' "$PARA_N"
      printf '%s\n' "$sentence"
    } > "$PARA_TREE/docs/scratch.md"
    run "$PARA_TREE"
    if [ "$RUN_RC" -eq 1 ] && printf '%s' "$RUN_OUT" | grep -qF "RK-IDENT-VIOLATION: $rule "; then
      ok "paraphrase caught by $rule: $sentence"
    else
      bad "paraphrase NOT caught by $rule (rc=$RUN_RC): $sentence"
      printf '%s\n' "$RUN_OUT" | sed 's/^/       | /'
    fi
  done < "$PARA"
  rm -f "$PARA_TREE/docs/scratch.md"
  if [ "$PARA_N" -ge 15 ]; then
    ok "paraphrase corpus is populated ($PARA_N sentences)"
  else
    bad "expected at least 15 paraphrases, found $PARA_N"
  fi
fi

echo "== physical-line wrapping is an explicit known limit =="

# The scanner records prose per physical line. Pin this current blind spot rather
# than implying the single-line paraphrase corpus covers hard-wrapped prose; #213
# owns changing the mechanism and flipping this expectation.
WRAPPED_CLAIM="$(fresh_tree wrapped_controller_claim)"
{
  printf '# Wrapped controller claim\n\n'
  printf 'The SH8601 is the display\n'
  printf 'controller for this board.\n'
} > "$WRAPPED_CLAIM/docs/scratch.md"
expect_rc "a controller claim split across physical lines remains outside current recall" \
  0 "$WRAPPED_CLAIM"
expect_no_out "the wrapped known limit is not presented as a caught controller claim" \
  "RK-IDENT-VIOLATION: CONTROLLER_CLAIM"

WRAPPED_DUTY="$(fresh_tree wrapped_backlight_duty)"
{
  printf '# Wrapped duty claim\n\n'
  printf 'The backlight starts at fifty percent\n'
  printf 'brightness.\n'
} > "$WRAPPED_DUTY/docs/scratch.md"
expect_rc "a backlight-duty claim split across physical lines remains outside current recall" \
  0 "$WRAPPED_DUTY"
expect_no_out "the wrapped known limit is not presented as a caught duty claim" \
  "RK-IDENT-VIOLATION: BACKLIGHT_DUTY"

WRAPPED_BUTTON="$(fresh_tree wrapped_encoder_button)"
{
  printf '# Wrapped encoder-button claim\n\n'
  printf 'Press the\n'
  printf 'knob to open the zone picker.\n'
} > "$WRAPPED_BUTTON/docs/scratch.md"
expect_rc "an encoder-button claim split across physical lines remains outside current recall" \
  0 "$WRAPPED_BUTTON"
expect_no_out "the wrapped known limit is not presented as a caught button claim" \
  "RK-IDENT-VIOLATION: ENCODER_BUTTON"

echo "== truthful prose that shares vocabulary stays clean =="

# The counterweight: strengthening a rule must not turn it into a topic ban.
CLEANPARA="$FIXTURES/paraphrases_clean.tsv"
if [ ! -f "$CLEANPARA" ]; then
  bad "clean paraphrase corpus missing at scripts/fixtures/docs_identity/paraphrases_clean.tsv"
else
  CLEAN_TREE="$(fresh_tree paraphrase_clean)"
  CLEAN_N=0
  while IFS="$(printf '\t')" read -r why sentence; do
    case "$why" in ''|'#'*) continue ;; esac
    [ -n "$sentence" ] || continue
    CLEAN_N=$((CLEAN_N + 1))
    {
      printf '# Clean paraphrase %d\n\n' "$CLEAN_N"
      printf '%s\n' "$sentence"
    } > "$CLEAN_TREE/docs/scratch.md"
    run "$CLEAN_TREE"
    if [ "$RUN_RC" -eq 0 ]; then
      ok "truthful prose accepted ($why)"
    else
      bad "truthful prose rejected ($why, rc=$RUN_RC): $sentence"
      printf '%s\n' "$RUN_OUT" | sed 's/^/       | /'
    fi
  done < "$CLEANPARA"
  rm -f "$CLEAN_TREE/docs/scratch.md"
  if [ "$CLEAN_N" -ge 10 ]; then
    ok "clean corpus is populated ($CLEAN_N sentences)"
  else
    bad "expected at least 10 clean sentences, found $CLEAN_N"
  fi
fi

echo "== the other-board escape survives the strengthened rules =="

# Strengthening the rules must not remove the one truthful way out. Same guarded
# vocabulary, a different board, one marker per line.
ESCAPE="$(fresh_tree strengthened_escape)"
{
  printf '# Comparison with other boards\n\n'
  printf '<!-- rk-ident-allow-next-line: PANEL_AMOLED target="ESP32-S3-AMOLED-1.91" reason="the 1.91 inch sibling really does ship an active-matrix organic LED panel" -->\n'
  printf 'The ESP32-S3-AMOLED-1.91 uses an active-matrix organic LED panel.\n\n'
  # Two rules on one line: the sibling's product name contains the panel
  # technology, so the marker must list both ids and both must really fire.
  printf '<!-- rk-ident-allow-next-line: PANEL_AMOLED, COLOR_DEPTH target="ESP32-S3-AMOLED-1.91" reason="the 1.91 inch sibling really is a 24-bit colour panel per its own datasheet" -->\n'
  printf 'The ESP32-S3-AMOLED-1.91 is a 24-bit colour display.\n\n'
  printf '<!-- rk-ident-allow-next-line: MODULE_CLAIM target="T-Display-S3" reason="that board genuinely carries the module package named here, unlike our target" -->\n'
  printf 'The T-Display-S3 carries an ESP32S3 WROOM 1 module.\n'
} > "$ESCAPE/docs/scratch.md"
expect_rc "marked other-board paraphrases still pass" 0 "$ESCAPE"

ESCAPESHIP="$(fresh_tree strengthened_escape_ship)"
{
  printf '# Not an escape for our own board\n\n'
  printf '<!-- rk-ident-allow-next-line: COLOR_DEPTH target="ESP32-S3-Knob-Touch-LCD-1.8" reason="attempting to use the other-board escape on the shipping target itself" -->\n'
  printf 'The ESP32-S3-Knob-Touch-LCD-1.8 is a 24-bit colour display.\n'
} > "$ESCAPESHIP/docs/scratch.md"
expect_rc "the escape still refuses the shipping target on a new rule clause" 1 "$ESCAPESHIP"
expect_out "shipping-target abuse is named for the new clause" "names the shipping target"

echo "== code context is exempt, prose is not =="

EXEMPT="$(fresh_tree code_exempt)"
cp "$FIXTURES/code_context_exempt.md" "$EXEMPT/docs/scratch.md"
expect_rc "guarded phrases inside fences and link targets are exempt" 0 "$EXEMPT"

BARE="$(fresh_tree code_bare)"
cp "$FIXTURES/code_context_bare.md" "$BARE/docs/scratch.md"
expect_rc "the same phrases as prose are rejected" 1 "$BARE"
expect_out "prose case names PANEL_AMOLED" "RK-IDENT-VIOLATION: PANEL_AMOLED"
expect_out "prose case names CONTROLLER_CLAIM" "RK-IDENT-VIOLATION: CONTROLLER_CLAIM"
expect_out "prose case names ENCODER_BUTTON" "RK-IDENT-VIOLATION: ENCODER_BUTTON"
expect_out "prose case names BACKLIGHT_DUTY" "RK-IDENT-VIOLATION: BACKLIGHT_DUTY"
expect_out "prose case names TOUCH_VARIANT" "RK-IDENT-VIOLATION: TOUCH_VARIANT"

echo "== an indented block is scanned as prose, and the output says so =="

# The deliberate gap in the code exemption. A four-space indented block is code to
# CommonMark and is NOT exempt here: recognising it would mean tracking list and
# blockquote context to tell a code block from a continuation line, and an
# indentation-only exemption would exempt the reflowed middle of a paragraph -- which
# is how the guarded wording travelled through this repository in the first place.
#
# Keeping that gap is only defensible if it is discoverable, so this asserts both
# halves: the block is reported, and the report names the remedy. An author who
# indented a quotation must not have to read the checker's docstring to find out why
# their quotation became a claim.
INDENTED="$(fresh_tree indented_code)"
cp "$FIXTURES/indented_code_scanned.md" "$INDENTED/docs/scratch.md"
expect_rc "a four-space indented block is scanned, not exempted" 1 "$INDENTED"
expect_out "the indented block names PANEL_AMOLED" "RK-IDENT-VIOLATION: PANEL_AMOLED"
expect_out "the indented block names NO_BACKLIGHT" "RK-IDENT-VIOLATION: NO_BACKLIGHT"
expect_out "the violation output tells the author to fence a quotation" \
  "quote source in a fenced code block"
expect_out "the violation output names the cause, not just the remedy" \
  "a four-space indented block is scanned as prose"

# The same text fenced is exempt, which is what makes the remedy a real one rather
# than advice that does not work.
INDENT_FENCED="$(fresh_tree indented_code_fenced)"
{
  printf '# Fencing the same block is the remedy\n\n```c\n'
  sed -n 's/^    //p' "$FIXTURES/indented_code_scanned.md"
  printf '```\n'
} > "$INDENT_FENCED/docs/scratch.md"
if ! grep -q 'AMOLED' "$INDENT_FENCED/docs/scratch.md"; then
  bad "could not build the fenced counterpart (the indented block did not transfer)"
else
  expect_rc "the same block inside a fence is exempt" 0 "$INDENT_FENCED"
  expect_no_out "the fenced counterpart prints no quoting note" "quote source in a fenced code block"
fi

# The note is not a rule message: it belongs to the run, travels in the report so the
# step summary can render it, and is printed once rather than per hit.
INDENT_REPORT="$WORK/indented_code_report.json"
run "$INDENTED" --report "$INDENT_REPORT"
if "$PY" -c '
import json, sys
d = json.load(open(sys.argv[1]))
note = d.get("quoting_note", "")
assert "fenced code block" in note, note
assert "indented" in note, note
assert d["violations"], "the fixture reported no violations"
' "$INDENT_REPORT" 2>/dev/null; then
  ok "the report carries the quoting note alongside the violations"
else
  bad "the report does not carry a usable quoting_note"
fi
NOTE_LINES="$(printf '%s\n' "$RUN_OUT" | grep -c "quote source in a fenced code block")"
if [ "$NOTE_LINES" -eq 1 ]; then
  ok "the quoting note is printed once, not once per hit"
else
  bad "the quoting note appeared $NOTE_LINES times, wanted exactly 1"
fi

echo "== backticks alone are not an exemption =="

IDENTS="$(fresh_tree code_identifiers)"
cp "$FIXTURES/code_identifiers.md" "$IDENTS/docs/scratch.md"
expect_rc "backtick spans carrying a code signal stay exempt" 0 "$IDENTS"

TICKS="$(fresh_tree backtick_claims)"
cp "$FIXTURES/backtick_claims.md" "$TICKS/docs/scratch.md"
expect_rc "backticked product and panel claims are rejected" 1 "$TICKS"
expect_out "backticked panel claim names PANEL_AMOLED" "RK-IDENT-VIOLATION: PANEL_AMOLED"
expect_out "backticked colour claim names COLOR_DEPTH" "RK-IDENT-VIOLATION: COLOR_DEPTH"
expect_out "backticked controller claim names CONTROLLER_CLAIM" "RK-IDENT-VIOLATION: CONTROLLER_CLAIM"
expect_out "backticked module claim names MODULE_CLAIM" "RK-IDENT-VIOLATION: MODULE_CLAIM"
expect_out "backticked product name names PRODUCT_NAME" "RK-IDENT-VIOLATION: PRODUCT_NAME"
expect_out "backticked backlight claim names NO_BACKLIGHT" "RK-IDENT-VIOLATION: NO_BACKLIGHT"

echo "== fenced code is exempt only when the fence is well formed =="

# The fence exemption is the widest one the checker has, so a fence the checker
# thinks is open when Markdown does not -- or the reverse -- silently exempts real
# prose. These cases pin both directions.
FENCEOK="$(fresh_tree fence_valid)"
cp "$FIXTURES/fence_valid.md" "$FENCEOK/docs/scratch.md"
expect_rc "well-formed fences of every shape stay exempt" 0 "$FENCEOK"
expect_no_out "a valid fence is not reported as unterminated" "unterminated code fence"

MISFENCE="$(fresh_tree fence_mismatched)"
cp "$FIXTURES/fence_mismatched.md" "$MISFENCE/docs/scratch.md"
expect_rc "a backtick fence closed by tildes is a structure failure" 2 "$MISFENCE"
expect_out "the mismatched fence is named" "RK-IDENT-STRUCTURE: docs/scratch.md:8: unterminated code fence"
expect_no_out "a mismatched fence can never report OK" "RK-IDENT-OK"

SHORTFENCE="$(fresh_tree fence_short_closer)"
cp "$FIXTURES/fence_short_closer.md" "$SHORTFENCE/docs/scratch.md"
expect_rc "a four-backtick fence closed by three is a structure failure" 2 "$SHORTFENCE"
expect_out "the short closer is named with the opening run length" \
  "unterminated code fence opened here with 4"
expect_no_out "a short-closed fence can never report OK" "RK-IDENT-OK"

# A bare unterminated opener, with nothing after it, is still reported: the defect
# is the structure, not the contradiction it happens to hide.
BAREFENCE="$(fresh_tree fence_bare_unterminated)"
{
  printf '# Unterminated fence with nothing after it\n\n'
  printf '```text\n'
  printf 'The block was never closed.\n'
} > "$BAREFENCE/docs/scratch.md"
expect_rc "an unterminated fence fails even with no contradiction behind it" 2 "$BAREFENCE"
expect_out "the bare unterminated fence is named" "docs/scratch.md:3: unterminated code fence"

# Tildes are symmetric: a tilde fence closed by backticks is equally unterminated.
TILDEFENCE="$(fresh_tree fence_tilde_mismatch)"
{
  printf '# Tilde fence closed by backticks\n\n'
  printf '~~~text\n'
  printf 'Quoted source.\n'
  printf '```\n\n'
  printf 'The display is a round AMOLED with no backlight control.\n'
} > "$TILDEFENCE/docs/scratch.md"
expect_rc "a tilde fence closed by backticks is a structure failure" 2 "$TILDEFENCE"
expect_out "the tilde fence is named with its character" "unterminated code fence opened here with 3 '~'"

echo "== an unterminated HTML comment cannot hide the tail =="

COMMENTOK="$(fresh_tree comment_closed)"
cp "$FIXTURES/comment_closed.md" "$COMMENTOK/docs/scratch.md"
expect_rc "closed comments remain an exemption" 0 "$COMMENTOK"
expect_no_out "a closed comment is not reported as unterminated" "unterminated HTML comment"

# ... and the prose after a closed comment is still scanned, so the exemption ends
# where the comment does.
COMMENTAFTER="$(fresh_tree comment_closed_then_prose)"
{
  printf '# A closed comment does not extend past its closing marker\n\n'
  printf '<!--\nAn earlier draft said the panel is an AMOLED.\n-->\n\n'
  printf 'The display is a round AMOLED with 16.7 million colours.\n'
} > "$COMMENTAFTER/docs/scratch.md"
expect_rc "prose after a closed comment is still scanned" 1 "$COMMENTAFTER"
expect_out "the line after the closed comment is named" \
  "RK-IDENT-VIOLATION: PANEL_AMOLED docs/scratch.md:7"

DANGLING="$(fresh_tree comment_unterminated)"
cp "$FIXTURES/comment_unterminated.md" "$DANGLING/docs/scratch.md"
expect_rc "an unterminated comment is a structure failure" 2 "$DANGLING"
expect_out "the dangling opener is named with its line" \
  "RK-IDENT-STRUCTURE: docs/scratch.md:7: unterminated HTML comment"
expect_no_out "a contradiction behind a dangling opener can never report OK" "RK-IDENT-OK"

echo "== rule-level suppressions are observable =="

# A suppressor exists so that truthful qualified prose passes. It must keep doing
# that -- and every hit it excuses must be visible, or widening one is invisible.
SUPPRESS="$(fresh_tree suppressed_hits)"
cp "$FIXTURES/suppressed_hits.md" "$SUPPRESS/docs/scratch.md"
SUPPREPORT="$WORK/suppressions.json"
run "$SUPPRESS" --report "$SUPPREPORT"
if [ "$RUN_RC" -eq 0 ]; then
  ok "truthful qualified prose still exits 0 (suppressions are not findings)"
else
  bad "qualified prose was rejected (rc=$RUN_RC)"
  printf '%s\n' "$RUN_OUT" | sed 's/^/       | /'
fi
expect_out "a suppression prints an informational token" "RK-IDENT-SUPPRESSED"
expect_out "the informational token names rule, file and line" \
  "RK-IDENT-SUPPRESSED: COLOR_DEPTH docs/scratch.md:12"
expect_out "the summary counter reports the suppression count" "suppressed="
expect_out "the run still reports OK" "RK-IDENT-OK"

if "$PY" -c "
import json,sys
d=json.load(open(sys.argv[1]))
rows=d['suppressions']
mine=[r for r in rows if r['file']=='docs/scratch.md']
assert len(mine)==4, mine
for r in mine:
    assert r['rule'] and r['line']>0 and r['match'] and r['suppressor'], r
ids={r['rule'] for r in mine}
assert ids=={'TOUCH_VARIANT','COLOR_DEPTH','ENCODER_BUTTON','BACKLIGHT_DUTY'}, ids
assert d['token']=='RK-IDENT-OK' and d['exit_code']==0
" "$SUPPREPORT" 2>/dev/null; then
  ok "every suppressed hit is in the report with file, line, rule and suppressor"
else
  bad "the suppression report was not usable"
fi

# The counterweight: the same sentences without their qualification must fail, so
# the suppressor is proven to be the reason they passed rather than a dead rule.
UNQUALIFIED="$(fresh_tree suppressed_unqualified)"
{
  printf '# The same claims without their qualification\n\n'
  printf 'The fitted touch controller is a CST816D.\n\n'
  printf 'The display is a 24-bit colour panel.\n\n'
  printf 'The encoder shaft is pressable.\n'
  printf 'The backlight starts at 50 %% duty.\n'
} > "$UNQUALIFIED/docs/scratch.md"
expect_rc "the same claims unqualified are violations" 1 "$UNQUALIFIED"
expect_out "unqualified variant claim is named" "RK-IDENT-VIOLATION: TOUCH_VARIANT"
expect_out "unqualified colour claim is named" "RK-IDENT-VIOLATION: COLOR_DEPTH"
expect_out "unqualified press claim is named" "RK-IDENT-VIOLATION: ENCODER_BUTTON"
expect_out "unqualified duty claim is named" "RK-IDENT-VIOLATION: BACKLIGHT_DUTY"

# Removing the observability is a --self-check failure, not a silent narrowing. The
# recording branch is inverted rather than deleted, so the patch is one line and
# the rest of the checker is untouched.
BLIND="$WORK/blind_suppressions.py"
sed 's|if suppressed is not None:|if suppressed is None:|' "$CHECKER" > "$BLIND"
if ! cmp -s "$CHECKER" "$BLIND"; then
  BLIND_OUT="$("$PY" "$BLIND" --root "$REPO" --self-check 2>/dev/null)"
  BLIND_RC=$?
  if [ "$BLIND_RC" -eq 4 ] && printf '%s' "$BLIND_OUT" | grep -qF "must stay observable"; then
    ok "dropping suppression recording fails --self-check (exit 4)"
  else
    bad "unobservable suppressions did not fail --self-check (rc=$BLIND_RC)"
    printf '%s\n' "$BLIND_OUT" | sed 's/^/       | /'
  fi
else
  bad "could not construct the blind-suppression case (patch did not apply)"
fi

run "$REPO" --self-check
if printf '%s' "$RUN_OUT" | grep -qE 'suppressed=[0-9]+'; then
  ok "the repository's own run prints its suppression count"
else
  bad "the repository run did not print a suppression count"
  printf '%s\n' "$RUN_OUT" | sed 's/^/       | /'
fi

# The load-bearing case for observability: a *widening* that self-check cannot
# catch, because it does not swallow the rule's own probe. Adding `touch` to
# TOUCH_VARIANT's suppressor turns a sentence the unpatched checker rejects into a
# sentence it accepts. That must not be silent -- the newly excused hit has to
# appear in the report as a suppression naming the widened suppressor.
WIDENED="$WORK/widened_unless.py"
sed 's@unless=r"(datasheet@unless=r"(touch|datasheet@' "$CHECKER" > "$WIDENED"
WIDETREE="$(fresh_tree widened_suppressor)"
printf '# Widening probe\n\nThe fitted touch controller is a CST816D on this unit.\n' \
  > "$WIDETREE/docs/scratch.md"
if cmp -s "$CHECKER" "$WIDENED"; then
  bad "could not construct the widened-suppressor case (patch did not apply)"
else
  # Direction one: the shipped checker rejects the sentence.
  expect_rc "the shipped suppressor does not excuse this sentence" 1 "$WIDETREE"
  # Direction two: the widened copy still passes --self-check (so self-check alone
  # is not the safety net) and now accepts the sentence...
  WIDEREPORT="$WORK/widened.json"
  WIDE_OUT="$("$PY" "$WIDENED" --root "$WIDETREE" --self-check --report "$WIDEREPORT" 2>/dev/null)"
  WIDE_RC=$?
  if [ "$WIDE_RC" -eq 0 ]; then
    ok "a widening that spares its own probe passes --self-check and accepts the sentence"
  else
    bad "widened suppressor did not accept the sentence (rc=$WIDE_RC)"
    printf '%s\n' "$WIDE_OUT" | sed 's/^/       | /'
  fi
  # ... but the newly excused hit is reported, so the widening is not silent.
  if "$PY" -c "
import json,sys
d=json.load(open(sys.argv[1]))
mine=[r for r in d['suppressions']
      if r['file']=='docs/scratch.md' and r['rule']=='TOUCH_VARIANT']
assert len(mine)==1, d['suppressions']
assert mine[0]['suppressor'].lower()=='touch', mine
assert mine[0]['match']=='CST816D', mine
" "$WIDEREPORT" 2>/dev/null; then
    ok "the widened suppressor's new exemption is visible in the report"
  else
    bad "a widened suppressor exempted a line invisibly"
    printf '%s\n' "$WIDE_OUT" | sed 's/^/       | /'
  fi
  if printf '%s' "$WIDE_OUT" | grep -qF "RK-IDENT-SUPPRESSED: TOUCH_VARIANT docs/scratch.md:3"; then
    ok "the widened suppressor's new exemption is printed in the run output"
  else
    bad "the widened exemption was not printed"
    printf '%s\n' "$WIDE_OUT" | sed 's/^/       | /'
  fi
fi

echo "== the canonical record's current facts are scanned =="

# The load-bearing test for the review finding that replaced the file-level
# exemption: mutate a live row of the identity table in the real board.md.
LIVEROW="$(fresh_tree live_row)"
rewrite "$LIVEROW/$BOARD" 's#| Panel technology | \*\*IPS LCD\*\*#| Panel technology | **round AMOLED**#'
if ! grep -q 'round AMOLED' "$LIVEROW/$BOARD"; then
  bad "could not mutate the live identity row (pattern did not apply)"
else
  expect_rc "an AMOLED row in the live identity table fails" 2 "$LIVEROW"
  expect_out "the mutated canonical row is named" "RK-IDENT-VIOLATION: PANEL_AMOLED $BOARD"
fi

# ... and the same for a repository-observed row, so the proof is not specific to
# one table.
LIVEROW2="$(fresh_tree live_row_r)"
rewrite "$LIVEROW2/$BOARD" 's#Initial backlight duty is#Initial backlight duty is 50 % duty, formerly#'
if ! grep -qF '50 % duty, formerly' "$LIVEROW2/$BOARD"; then
  bad "could not mutate the live R row (pattern did not apply)"
else
  expect_rc "a stale 50 % duty claim in a live R row fails" 1 "$LIVEROW2"
  expect_out "the mutated R row is named" "RK-IDENT-VIOLATION: BACKLIGHT_DUTY $BOARD"
fi

echo "== the section allowance is section-scoped, not file-scoped =="

# Moving the historical table's wording out of its allowed section must fail,
# even though it stays in the same file.
MOVED="$(fresh_tree moved_history)"
{
  cat "$BASE/docs/esp/hw-reference/HARDWARE_PINS.md"
  printf '\n## Notes\n\nPanel is a round AMOLED, as an earlier draft said.\n'
} > "$MOVED/docs/esp/hw-reference/HARDWARE_PINS.md"
expect_rc "the same historical wording in a non-allowed file fails" 1 "$MOVED"

RENAMED="$(fresh_tree renamed_section)"
rewrite "$RENAMED/$BOARD" 's/^## Historical claims, now superseded$/## Historical notes/'
expect_rc "renaming the allowed section fails" 2 "$RENAMED"
expect_out "the missing allowed section is named" "missing required section"

# An allowance over a section that no longer restates anything is a hole, so it is
# reported rather than left in place.
EMPTIED="$(fresh_tree emptied_section)"
"$PY" - "$EMPTIED/$ADR" <<'PY'
import re
import sys

path = sys.argv[1]
text = open(path, encoding="utf-8").read()
start = text.index("## Context")
end = text.index("## Decision")
# Keep the heading, replace everything it covers with prose that restates nothing
# guarded. The allowance over this section then has no work left to do.
text = text[:start] + "## Context\n\nNothing guarded is restated here any more.\n\n" + text[end:]
open(path, "w", encoding="utf-8").write(text)
PY
run "$EMPTIED"
if [ "$RUN_RC" -eq 2 ] && printf '%s' "$RUN_OUT" | grep -qF "the allowance is dead"; then
  ok "an allowed section that restates nothing is reported as a dead allowance (exit 2)"
else
  bad "dead section allowance was not reported (rc=$RUN_RC)"
  printf '%s\n' "$RUN_OUT" | sed 's/^/       | /'
fi

# Section state is read from the comment-stripped text, because a heading is only a
# heading if a reader can see it. `section` is what grants a SECTION_ALLOWANCES
# exemption, so invisible text must not be able to move it in either direction.
#
# Direction one -- it must not CLEAR the allowed section. This is the reachable
# defect: a line that closes a multi-line comment is still scanned for prose after
# the closing marker, so its *raw* text reaches the heading matcher. A `#` line
# ending in `-->` therefore used to reset `section` to None, silently revoking the
# historical allowance for every superseded row below it. Reading the visible text
# instead leaves the allowance intact, so this case must be green.
COMMENTRESET="$(fresh_tree comment_heading_reset)"
"$PY" - "$COMMENTRESET/$BOARD" <<'PY'
import sys

path = sys.argv[1]
text = open(path, encoding="utf-8").read()
old = "## Historical claims, now superseded\n"
if text.count(old) != 1:
    sys.exit(1)
# The `#` line is the comment's *closing* line, not an interior one: interior lines
# are skipped wholesale, so an interior heading would not exercise the matcher.
hidden = "<!-- an earlier draft's heading, kept only as a note\n# a heading that exists only inside this comment -->\n"
open(path, "w", encoding="utf-8").write(text.replace(old, old + "\n" + hidden, 1))
PY
if ! grep -qF '# a heading that exists only inside this comment -->' "$COMMENTRESET/$BOARD"; then
  bad "could not insert the comment-closing heading line (patch did not apply)"
else
  expect_rc "a '#' line inside an HTML comment does not clear the allowed section" \
    0 "$COMMENTRESET"
  expect_out "the section allowance survives an invisible heading" "RK-IDENT-OK"
  expect_no_out "no superseded row is reported once the allowance is intact" \
    "RK-IDENT-VIOLATION"
fi

# Direction two -- it must not GRANT one either. Hiding the real heading inside a
# comment removes the exemption: the rows it used to cover become ordinary
# current-fact prose and are reported, and the allowance that no longer suppresses
# anything is named. This holds however the heading is hidden, so it is a contract
# pin rather than a discriminator on any one code path -- its job is to make any
# future attempt to read section state from invisible text fail here.
COMMENTHEAD="$(fresh_tree comment_heading_hidden)"
"$PY" - "$COMMENTHEAD/$BOARD" <<'PY'
import sys

path = sys.argv[1]
text = open(path, encoding="utf-8").read()
old = "## Historical claims, now superseded\n"
if text.count(old) != 1:
    sys.exit(1)
open(path, "w", encoding="utf-8").write(
    text.replace(old, "<!-- hidden heading\n" + old.rstrip("\n") + " -->\n", 1)
)
PY
# The heading text is deliberately still present -- it is now inside a comment -- so
# the guard asserts the wrapper, not the absence of the line.
if ! grep -qF '## Historical claims, now superseded -->' "$COMMENTHEAD/$BOARD"; then
  bad "could not hide the historical heading inside a comment (patch did not apply)"
else
  run "$COMMENTHEAD"
  if [ "$RUN_RC" -ne 0 ] \
     && printf '%s' "$RUN_OUT" | grep -qF "RK-IDENT-VIOLATION: PANEL_AMOLED $BOARD"; then
    ok "a heading visible only inside a comment grants no exemption (exit $RUN_RC)"
  else
    bad "a commented historical heading still granted the section allowance (rc=$RUN_RC)"
    printf '%s\n' "$RUN_OUT" | sed 's/^/       | /'
  fi
  expect_no_out "a commented historical heading can never report OK" "RK-IDENT-OK"
fi

echo "== the other-target line allowance works, and only just far enough =="

MARKED="$(fresh_tree other_marked)"
cp "$FIXTURES/other_target_marked.md" "$MARKED/docs/scratch.md"
expect_rc "truthful other-board prose passes with the marker" 0 "$MARKED"

SAME_LINE="$(fresh_tree other_marker_same_line)"
{
  printf '# Marker must stand alone\n\n'
  printf 'The shipping round knob has an AMOLED panel. <!-- rk-ident-allow-next-line: PANEL_AMOLED target="ESP32-S3-AMOLED-1.91" reason="the following line truthfully describes a separate sibling product" -->\n'
  printf 'The ESP32-S3-AMOLED-1.91 is a separate AMOLED product.\n'
} > "$SAME_LINE/docs/scratch.md"
expect_rc "prose before an otherwise valid marker on the same line is rejected" 1 "$SAME_LINE"
expect_out "same-line marker misuse is named" "RK-IDENT-MARKER-ABUSE"
expect_out "prose before the same-line marker is still scanned" "RK-IDENT-VIOLATION: PANEL_AMOLED docs/scratch.md:3"

UNMARKED="$(fresh_tree other_unmarked)"
cp "$FIXTURES/other_target_unmarked.md" "$UNMARKED/docs/scratch.md"
expect_rc "the same prose without the marker fails" 1 "$UNMARKED"
expect_out "unmarked other-board prose names PANEL_AMOLED" "RK-IDENT-VIOLATION: PANEL_AMOLED"

SECOND="$(fresh_tree other_second_line)"
cp "$FIXTURES/other_target_second_line.md" "$SECOND/docs/scratch.md"
expect_rc "one marker cannot cover a second line" 1 "$SECOND"
expect_out "the uncovered second line is reported" "docs/scratch.md:8"
expect_no_out "the covered first line is not reported" "docs/scratch.md:7"

UNRELATED="$(fresh_tree other_unrelated)"
cp "$FIXTURES/other_target_unrelated_line.md" "$UNRELATED/docs/scratch.md"
expect_rc "a marker cannot silence a line that never mentions the declared target" 1 "$UNRELATED"
expect_out "the mismatched target is reported as abuse" "RK-IDENT-MARKER-ABUSE"
expect_out "the abuse message names the target mismatch" "does not mention it"
expect_out "the contradiction it tried to hide is still reported" "RK-IDENT-VIOLATION: PANEL_AMOLED"

SHIP="$(fresh_tree other_shipping)"
cp "$FIXTURES/other_target_shipping.md" "$SHIP/docs/scratch.md"
expect_rc "a marker naming the shipping target is rejected" 1 "$SHIP"
expect_out "shipping-target abuse is named" "names the shipping target"

NOREASON="$(fresh_tree other_no_reason)"
cp "$FIXTURES/other_target_no_reason.md" "$NOREASON/docs/scratch.md"
expect_rc "a marker with a token reason is rejected" 1 "$NOREASON"
expect_out "short reason is named" "reason is shorter than"

STACKED="$(fresh_tree other_stacked)"
{
  printf '# Stacked markers\n\n'
  printf '<!-- rk-ident-allow-next-line: PANEL_AMOLED target="ESP32-S3-AMOLED-1.91" reason="first of two stacked markers, which is not a supported form at all" -->\n'
  printf '<!-- rk-ident-allow-next-line: COLOR_DEPTH target="ESP32-S3-AMOLED-1.91" reason="second of two stacked markers, which is not a supported form at all" -->\n'
  printf 'The ESP32-S3-AMOLED-1.91 has an AMOLED panel and 16.7 million colours.\n'
} > "$STACKED/docs/scratch.md"
expect_rc "stacked markers are rejected" 1 "$STACKED"
expect_out "stacking is named" "cannot be stacked"

MALFORMED="$(fresh_tree other_malformed)"
{
  printf '# Malformed marker\n\n'
  printf '<!-- rk-ident-allow-next-line: PANEL_AMOLED -->\n'
  printf 'The ESP32-S3-AMOLED-1.91 has an AMOLED panel.\n'
} > "$MALFORMED/docs/scratch.md"
expect_rc "a marker missing target and reason is rejected" 1 "$MALFORMED"
expect_out "malformed marker is named" "malformed allowance"

UNKNOWNRULE="$(fresh_tree other_unknown_rule)"
{
  printf '# Unknown rule id\n\n'
  printf '<!-- rk-ident-allow-next-line: NOT_A_RULE target="ESP32-S3-AMOLED-1.91" reason="naming a rule that does not exist, which must not silently pass review" -->\n'
  printf 'The ESP32-S3-AMOLED-1.91 has an AMOLED panel.\n'
} > "$UNKNOWNRULE/docs/scratch.md"
expect_rc "a marker naming an unknown rule is rejected" 1 "$UNKNOWNRULE"
expect_out "unknown rule id is named" "unknown rule"

IDLE="$(fresh_tree other_idle)"
{
  printf '# A marker that suppresses nothing\n\n'
  printf '<!-- rk-ident-allow-next-line: PANEL_AMOLED target="T-Display-S3" reason="left behind after the sentence it covered was rewritten to say nothing guarded" -->\n'
  printf 'The T-Display-S3 is a different board with a different panel entirely.\n'
} > "$IDLE/docs/scratch.md"
expect_rc "an allowance that suppresses nothing is a structure failure" 2 "$IDLE"
expect_out "the idle allowance is named" "suppresses nothing"

echo "== the retired file-level marker cannot come back =="

RETIRED="$(fresh_tree retired_marker)"
cp "$FIXTURES/retired_marker.md" "$RETIRED/docs/scratch.md"
expect_rc "the retired file-level marker is rejected" 1 "$RETIRED"
expect_out "retired marker is named" "retired rk-ident-allow-file marker"
expect_out "the contradiction it tried to hide is still reported" "RK-IDENT-VIOLATION: PANEL_AMOLED"

# Prose *about* the marker is not a use of it: the decision record explains both
# markers in running text and must stay clean.
run "$BASE"
expect_no_out "prose that merely names the markers is not reported" "RK-IDENT-MARKER-ABUSE"

echo "== absence never reads as satisfied =="

expect_rc "an empty tree fails rather than passing vacuously" 3 "$WORK/empty_tree_does_not_exist"
expect_out "missing root is named" "RK-IDENT-MISSING"

EMPTY="$WORK/empty"
mkdir -p "$EMPTY"
expect_rc "a tree with no Markdown at all fails" 3 "$EMPTY"
expect_out "empty tree reports MISSING" "RK-IDENT-MISSING"

NOCANON="$(fresh_tree no_canonical)"
rm -f "$NOCANON/$BOARD"
expect_rc "deleting the canonical record fails" 3 "$NOCANON"
expect_out "deleted canonical record is named" "canonical record absent"

NOADR="$(fresh_tree no_adr)"
rm -f "$NOADR/$ADR"
expect_rc "deleting the decision record fails" 3 "$NOADR"
expect_out "deleted decision record is named" "decision record absent"

echo "== canonical structure is enforced =="

GUTTED="$(fresh_tree gutted)"
grep -v '^## Still requires physical inspection' "$BASE/$BOARD" > "$GUTTED/$BOARD.tmp"
mv "$GUTTED/$BOARD.tmp" "$GUTTED/$BOARD"
expect_rc "removing a required section fails" 2 "$GUTTED"
expect_out "removed section is named" "missing required section"

REORDERED="$(fresh_tree reordered)"
"$PY" - "$BASE/$BOARD" "$REORDERED/$BOARD" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
text = open(src, encoding="utf-8").read()
a, b = "## Historical claims, now superseded", "## Still requires physical inspection"
text = text.replace(a, "@@TMP@@").replace(b, a).replace("@@TMP@@", b)
open(dst, "w", encoding="utf-8").write(text)
PY
run "$REORDERED"
if [ "$RUN_RC" -ne 0 ]; then
  ok "swapping two required sections fails (exit $RUN_RC)"
else
  bad "reordered canonical sections were accepted"
fi

DEVALUED="$(fresh_tree devalued)"
rewrite "$DEVALUED/$BOARD" 's/ST77916/REDACTED/g'
expect_rc "dropping a corrected value fails" 2 "$DEVALUED"
expect_out "dropped value is named" "corrected value"

DUTYGONE="$(fresh_tree duty_value_gone)"
rewrite "$DUTYGONE/$BOARD" 's/CONFIG_RK_BACKLIGHT_NORMAL/SOME_OTHER_SYMBOL/g'
expect_rc "dropping the backlight-duty symbol fails" 2 "$DUTYGONE"
expect_out "dropped duty symbol is named" "CONFIG_RK_BACKLIGHT_NORMAL"

HISTORYONLY="$(fresh_tree corrected_value_history_only)"
"$PY" - "$HISTORYONLY/$BOARD" <<'PY'
import sys

path = sys.argv[1]
lines = open(path, encoding="utf-8").readlines()
section = None
out = []
for line in lines:
    if line.startswith("## "):
        section = line.strip()
    if section != "## Historical claims, now superseded":
        line = line.replace("262 K", "REDACTED").replace("262K", "REDACTED")
    out.append(line)
open(path, "w", encoding="utf-8").writelines(out)
PY
expect_rc "a corrected value found only in the Historical table fails" 2 "$HISTORYONLY"
expect_out "history-only corrected value is named" "corrected value '262'"

AFTERFENCE="$(fresh_tree corrected_value_after_fenced_comments)"
"$PY" - "$AFTERFENCE/$BOARD" <<'PY'
import sys

path = sys.argv[1]
lines = open(path, encoding="utf-8").readlines()
keep = False
out = []
for line in lines:
    if line.startswith("`esp_lcd_sh8601` 2.0.1 and 2.0.1~1 differ"):
        keep = True
    if not keep:
        line = line.replace("esp_lcd_sh8601", "REDACTED_DRIVER")
    out.append(line)
open(path, "w", encoding="utf-8").writelines(out)
PY
expect_rc "a corrected value after hash-prefixed lines in a fence remains live" 0 "$AFTERFENCE"

DECLASSED="$(fresh_tree declassed)"
rewrite "$DECLASSED/$BOARD" 's/Physically unverified/Somewhat unclear/g'
expect_rc "dropping a provenance class fails" 2 "$DECLASSED"
expect_out "dropped class is named" "provenance class"

UNLINKED="$(fresh_tree unlinked)"
rewrite "$UNLINKED/docs/esp/DISPLAY.md" 's|(hw-reference/board.md|(hw-reference/nowhere.md|g'
expect_rc "removing a canonical link fails" 2 "$UNLINKED"
expect_out "unlinked document is named" "no Markdown link to board.md"

DEADLINK="$(fresh_tree dead_canonical_link)"
rewrite "$DEADLINK/README.md" \
  's|(docs/esp/hw-reference/board.md)|(docs/esp/hw-reference/nonexistent/board.md)|g'
if ! grep -qF 'nonexistent/board.md' "$DEADLINK/README.md"; then
  bad "could not replace the root README canonical target (pattern did not apply)"
else
  expect_rc "a link-shaped string ending in board.md must resolve to the canonical file" \
    2 "$DEADLINK"
  expect_out "the dead canonical target names the root README" \
    "README.md: no Markdown link to board.md"
fi

echo "== source anchors keep the repository-observed citations fresh =="

# Prose rules cannot notice when the source a repository-observed row was read from
# moves out from under it: the documentation stays word-for-word correct-looking
# while its citation rots. These cases pin both discriminating directions -- one
# mutated token, one deleted file -- and pin the wording, because an anchor claims
# citation freshness and must not be read as proof the fact is true or false.

ANCHOR_LIST="$("$PY" "$CHECKER" --list-anchors 2>/dev/null)"
ANCHOR_LIST_RC=$?
if [ "$ANCHOR_LIST_RC" -eq 0 ] && printf '%s' "$ANCHOR_LIST" | grep -q "$(printf 'BACKLIGHT_GPIO47\tidf_app/main/platform_display_idf.c')"; then
  ok "--list-anchors exits 0 and prints id and path"
else
  bad "--list-anchors failed (rc=$ANCHOR_LIST_RC)"
  printf '%s\n' "$ANCHOR_LIST" | sed 's/^/       | /'
fi

ANCHOR_N="$(printf '%s\n' "$ANCHOR_LIST" | grep -c .)"
if [ "$ANCHOR_N" -ge 13 ]; then
  ok "the anchor table is populated ($ANCHOR_N anchors)"
else
  bad "expected at least 13 source anchors, found $ANCHOR_N"
fi

# A floor, not an equality: the anchor table covers a subset of the canonical
# record's repository-observed rows, and no assertion here may imply otherwise.
# The rows deliberately left unanchored -- negative, uniqueness, derived-count and
# physical -- are named in the checker's docstring and in the ADR.
for want in QSPI_PINS PANEL_INIT_ARRAY ZONE_PICKER_TOUCH_ENTRY ZONE_PICKER_HANDLER; do
  if printf '%s\n' "$ANCHOR_LIST" | cut -f1 | grep -qxF "$want"; then
    ok "the positively quotable row $want is anchored"
  else
    bad "expected a source anchor named $want"
  fi
done

# A tree carrying the cited sources is clean, and says so in the counters. This is
# the control for the two failure cases below.
FRESH="$(fresh_tree anchors_fresh)"
expect_rc "a tree carrying every cited source file is accepted" 0 "$FRESH"
expect_out "the summary counts every anchor as fresh" "anchors_fresh=$ANCHOR_N/$ANCHOR_N"
expect_no_out "a fresh anchor is not reported" "source anchor"

# Direction one: mutate exactly one expected token. The duty assignment is the
# load-bearing one -- it is the value that corrected the stale 50 % comment -- and
# changing it leaves every other anchor in this file intact, so the finding must
# name one anchor and one token.
MUTATED="$(fresh_tree anchor_token_mutated)"
rewrite "$MUTATED/idf_app/main/platform_display_idf.c" \
  's/\.duty = CONFIG_RK_BACKLIGHT_NORMAL/.duty = 128/'
if ! grep -q '\.duty = 128' "$MUTATED/idf_app/main/platform_display_idf.c"; then
  bad "could not mutate the duty assignment (pattern did not apply)"
else
  expect_rc "a mutated anchor token is a structure failure" 2 "$MUTATED"
  expect_out "the stale citation names its anchor" \
    "RK-IDENT-STRUCTURE: source anchor BACKLIGHT_DUTY_ASSIGNMENT"
  expect_out "the stale citation names the file" "idf_app/main/platform_display_idf.c"
  expect_out "the stale citation names the missing token" \
    "'.duty = CONFIG_RK_BACKLIGHT_NORMAL,'"
  expect_out "the finding says a stale citation is not a false claim" \
    "not that the claim is false"
  expect_out "the finding says anchors do not check cited line numbers" \
    "an anchor checks text, never the line numbers the row cites"
  expect_out "the counter shows the shortfall" "anchors_fresh=$((ANCHOR_N - 1))/$ANCHOR_N"
  expect_no_out "a stale citation can never report OK" "RK-IDENT-OK"
  # Only the one anchor is affected: a whole-file panic would make the finding
  # useless for locating the drift.
  expect_no_out "the untouched backlight-pin anchor is not reported" \
    "source anchor BACKLIGHT_GPIO47"
fi

# Direction two: delete a cited file outright. This is a STRUCTURE finding, not
# MISSING: MISSING is reserved for the documents the guard itself requires.
NOSOURCE="$(fresh_tree anchor_file_removed)"
rm -f "$NOSOURCE/idf_app/main/idf_component.yml"
expect_rc "a deleted cited source file is a structure failure" 2 "$NOSOURCE"
expect_out "the absent file names its anchor" \
  "RK-IDENT-STRUCTURE: source anchor DRIVER_VERSION_UNPINNED"
expect_out "the absent file is named" "cited file idf_app/main/idf_component.yml is absent"
expect_out "the absent-file finding is worded as freshness, not falsity" \
  "not evidence the claim is false"
expect_out "the absent-file finding also disclaims line-number checking" \
  "never the line numbers the row cites"
expect_no_out "a deleted cited source can never report OK" "RK-IDENT-OK"

# Relationship, not fragments. The failure mode a split token hides is a file in
# which both halves are still present and no longer describe each other, so this
# renames the encoder A pin and asserts the anchor notices. A pair of tokens
# `#define ENCODER_GPIO_A` + `GPIO_NUM_8` would not: `GPIO_NUM_8` survives in the
# renamed line, and `#define ENCODER_GPIO_A` survives too.
SPLITCASE="$(fresh_tree anchor_relationship)"
rewrite "$SPLITCASE/idf_app/main/platform_input_idf.c" \
  's/#define ENCODER_GPIO_A    GPIO_NUM_8/#define ENCODER_GPIO_A    GPIO_NUM_9  \/\/ was GPIO_NUM_8/'
if ! grep -q 'ENCODER_GPIO_A    GPIO_NUM_9' "$SPLITCASE/idf_app/main/platform_input_idf.c"; then
  bad "could not repoint the encoder A pin (pattern did not apply)"
elif ! grep -q 'GPIO_NUM_8' "$SPLITCASE/idf_app/main/platform_input_idf.c"; then
  bad "the repointed file no longer mentions GPIO_NUM_8, so it does not test a split token"
else
  expect_rc "a broken pin relationship is a structure failure even though both halves remain" \
    2 "$SPLITCASE"
  expect_out "the broken relationship names the encoder anchor" \
    "RK-IDENT-STRUCTURE: source anchor ENCODER_PINS"
  expect_out "the broken relationship names the whole-line token" \
    "'#define ENCODER_GPIO_A GPIO_NUM_8'"
fi

# Word-boundary guards: a token must not be satisfied by a longer literal that
# merely starts with it. Widening 0x15 to 0x155 must be caught.
PREFIXCASE="$(fresh_tree anchor_prefix)"
rewrite "$PREFIXCASE/idf_app/components/i2c_bsp/settings.h" \
  's/#define TOUCH_ADDR 0x15$/#define TOUCH_ADDR 0x155/'
if ! grep -q 'TOUCH_ADDR 0x155' "$PREFIXCASE/idf_app/components/i2c_bsp/settings.h"; then
  bad "could not widen the touch address (pattern did not apply)"
else
  expect_rc "a token does not match a longer literal that starts with it" 2 "$PREFIXCASE"
  expect_out "the widened literal names the touch-address anchor" \
    "RK-IDENT-STRUCTURE: source anchor TOUCH_I2C_ADDRESS"
fi

# Whitespace insensitivity is the other half of the same bargain: pinning a whole
# line is only sane if realignment does not break it.
REALIGNED="$(fresh_tree anchor_realigned)"
rewrite "$REALIGNED/idf_app/main/platform_input_idf.c" \
  's/#define ENCODER_GPIO_A    GPIO_NUM_8/#define ENCODER_GPIO_A GPIO_NUM_8/'
if ! grep -q '#define ENCODER_GPIO_A GPIO_NUM_8' "$REALIGNED/idf_app/main/platform_input_idf.c"; then
  bad "could not realign the encoder A define (pattern did not apply)"
else
  expect_rc "realigning an anchored line does not break its anchor" 0 "$REALIGNED"
  expect_out "the realigned tree still counts every anchor fresh" "anchors_fresh=$ANCHOR_N/$ANCHOR_N"
fi

# ... and "whitespace-insensitive" has to mean the gap may *close* at a punctuation
# boundary, not merely shrink. The QSPI_PINS token was written from a wrapped call
# site (`SH8601_PANEL_IO_QSPI_CONFIG(` then `PIN_NUM_LCD_CS,` on the next line), so
# it carries a space after `(`. Unwrapping the argument list changes no fact, and
# must not report a stale citation. Both one-line spellings are pinned so the
# distinction cannot silently regress to a mandatory space.
for gap in ' ' ''; do
  [ -n "$gap" ] && form=spaced || form=unspaced
  QSPIFORM="$(fresh_tree "anchor_qspi_$form")"
  QSPIWANT="SH8601_PANEL_IO_QSPI_CONFIG(${gap}PIN_NUM_LCD_CS,"
  "$PY" - "$QSPIFORM/idf_app/main/platform_display_idf.c" "$QSPIWANT" <<'PY'
import re
import sys

path, want = sys.argv[1], sys.argv[2]
text = open(path, encoding="utf-8").read()
collapsed, count = re.subn(
    r"SH8601_PANEL_IO_QSPI_CONFIG\(\s+PIN_NUM_LCD_CS,", want, text, count=1
)
if count != 1:
    sys.exit(1)
open(path, "w", encoding="utf-8").write(collapsed)
PY
  if ! grep -qF "$QSPIWANT" "$QSPIFORM/idf_app/main/platform_display_idf.c"; then
    bad "could not unwrap the QSPI macro call to its $form one-line form"
  else
    expect_rc "the $form one-line QSPI macro call keeps its anchor fresh" 0 "$QSPIFORM"
    expect_out "the $form QSPI form counts every anchor fresh" "anchors_fresh=$ANCHOR_N/$ANCHOR_N"
  fi
done

# The other side of that relaxation: whitespace between two *word* lexemes stays
# mandatory, or a token would be satisfied by two identifiers fused into one.
FUSED="$(fresh_tree anchor_word_fused)"
rewrite "$FUSED/idf_app/main/platform_display_idf.c" \
  's/#define PIN_NUM_BK_LIGHT /#definePIN_NUM_BK_LIGHT /'
if ! grep -qF '#definePIN_NUM_BK_LIGHT' "$FUSED/idf_app/main/platform_display_idf.c"; then
  bad "could not fuse the backlight define (pattern did not apply)"
else
  expect_rc "two word lexemes fused together do not satisfy the token" 2 "$FUSED"
  expect_out "the fused define names the backlight anchor" \
    "RK-IDENT-STRUCTURE: source anchor BACKLIGHT_GPIO47"
  expect_out "the fused define names the missing token" \
    "'#define PIN_NUM_BK_LIGHT ((gpio_num_t)47)'"
fi

# Pin the negative capability too. Moving source text without changing it leaves
# every token fresh while the canonical record's literal :62 citation goes stale.
# This test must stay green: it demonstrates the known limit rather than pretending
# to solve it, and makes any future claim that anchors validate line numbers fail
# against executable evidence.
LINEMOVED="$(fresh_tree anchor_line_moved)"
DISPLAY_SOURCE="$LINEMOVED/idf_app/main/platform_display_idf.c"
ORIGINAL_PIN_LINE="$(grep -nF '#define PIN_NUM_BK_LIGHT' "$DISPLAY_SOURCE" | head -1 | cut -d: -f1)"
{
  for n in $(seq 1 20); do
    printf '// line-movement fixture %s\n' "$n"
  done
  cat "$DISPLAY_SOURCE"
} > "$DISPLAY_SOURCE.shifted"
mv "$DISPLAY_SOURCE.shifted" "$DISPLAY_SOURCE"
MOVED_PIN_LINE="$(grep -nF '#define PIN_NUM_BK_LIGHT' "$DISPLAY_SOURCE" | head -1 | cut -d: -f1)"
# Empty means the define was renamed or removed. Default both to 0 so the `-eq`
# arithmetic below cannot emit a shell error before falling through to `bad`, and
# require the original lookup to have found something at all.
: "${ORIGINAL_PIN_LINE:=0}"
: "${MOVED_PIN_LINE:=0}"
if [ "$ORIGINAL_PIN_LINE" -gt 0 ] \
   && [ "$MOVED_PIN_LINE" -eq $((ORIGINAL_PIN_LINE + 20)) ] \
   && grep -qF 'idf_app/main/platform_display_idf.c:62' "$LINEMOVED/$BOARD"; then
  ok "the line-movement fixture leaves the canonical :62 citation stale by 20 lines"
else
  bad "the line-movement fixture did not create the documented stale-citation condition"
fi
expect_rc "pure line movement remains outside source-anchor scope" 0 "$LINEMOVED"
expect_out "line movement leaves every table anchor fresh" \
  "anchors_fresh=$ANCHOR_N/$ANCHOR_N"

# The inventory is in the report, not only the failures, so a reader can see which
# citations were checked at all.
ANCHORREPORT="$WORK/anchors.json"
run "$MUTATED" --report "$ANCHORREPORT"
if "$PY" -c "
import json,sys
d=json.load(open(sys.argv[1]))
rows=d['source_anchors']
assert len(rows)>=13, rows
byid={r['id']: r for r in rows}
bad_=byid['BACKLIGHT_DUTY_ASSIGNMENT']
assert bad_['status']=='missing-token', bad_
assert bad_['missing']==['.duty = CONFIG_RK_BACKLIGHT_NORMAL,'], bad_
assert bad_['path']=='idf_app/main/platform_display_idf.c', bad_
assert bad_['claim'], bad_
assert byid['BACKLIGHT_GPIO47']['status']=='ok', byid['BACKLIGHT_GPIO47']
assert all(r['tokens'] for r in rows), rows
# The report must carry the checker's own statement of what the count means, so a
# reader of the JSON or the step summary cannot take N/N for coverage of the R rows
# or for a check on the cited line numbers.
scope=d['source_anchor_scope']
assert 'NOT that every' in scope, scope
assert 'line numbers' in scope, scope
# The failure is reported once, as a structure failure, and gates the exit code.
assert d['token']=='RK-IDENT-STRUCTURE' and d['exit_code']==2
assert sum(1 for s in d['structure_failures'] if 'source anchor' in s)==1, d['structure_failures']
" "$ANCHORREPORT" 2>/dev/null; then
  ok "the report carries every anchor with its status, and the failure exactly once"
else
  bad "the source-anchor report was not usable"
  printf '%s\n' "$RUN_OUT" | sed 's/^/       | /'
fi

# The renderer counts anchors, labels the count as a table size rather than as
# coverage, carries the checker's scope note, and does not invent a second finding
# section.
sum_anchor_out="$("$PY" "$REPO/scripts/summarize_docs_identity.py" --report "$ANCHORREPORT" 2>/dev/null)"
if printf '%s' "$sum_anchor_out" | grep -qF "| source anchors in table | $ANCHOR_N |" \
   && printf '%s' "$sum_anchor_out" | grep -qF "source anchor BACKLIGHT_DUTY_ASSIGNMENT" \
   && ! printf '%s' "$sum_anchor_out" | grep -qF "### source anchors"; then
  ok "the summary counts anchors and renders the stale one under structure failures"
else
  bad "the summary did not render the source-anchor state correctly"
  printf '%s\n' "$sum_anchor_out" | sed 's/^/       | /'
fi
if printf '%s' "$sum_anchor_out" | grep -qF "NOT that every" \
   && printf '%s' "$sum_anchor_out" | grep -qF "line numbers"; then
  ok "the summary states that the anchor count is not coverage and excludes line numbers"
else
  bad "the summary rendered an anchor count with no scope note"
  printf '%s\n' "$sum_anchor_out" | sed 's/^/       | /'
fi
# A report predating the field must still render, without inventing a claim.
NOSCOPE="$WORK/anchors_noscope.json"
"$PY" -c '
import json, sys
d = json.load(open(sys.argv[1]))
d.pop("source_anchor_scope", None)
json.dump(d, open(sys.argv[2], "w"))
' "$ANCHORREPORT" "$NOSCOPE" 2>/dev/null
sum_noscope_out="$("$PY" "$REPO/scripts/summarize_docs_identity.py" --report "$NOSCOPE" 2>/dev/null)"
sum_noscope_rc=$?
if [ "$sum_noscope_rc" -eq 0 ] \
   && printf '%s' "$sum_noscope_out" | grep -qF "| source anchors in table | $ANCHOR_N |" \
   && ! printf '%s' "$sum_noscope_out" | grep -qF "NOT that every"; then
  ok "a report without the scope note still renders, and claims nothing extra"
else
  bad "the renderer mishandled a report with no source_anchor_scope (rc=$sum_noscope_rc)"
  printf '%s\n' "$sum_noscope_out" | sed 's/^/       | /'
fi

# An anchor with no expected tokens could never fail, which is the way this layer
# rots into decoration. Self-check refuses it.
TOKENLESS="$WORK/tokenless_anchor.py"
sed 's|(".duty = CONFIG_RK_BACKLIGHT_NORMAL,",)|()|' "$CHECKER" > "$TOKENLESS"
if ! cmp -s "$CHECKER" "$TOKENLESS"; then
  TOK_OUT="$("$PY" "$TOKENLESS" --root "$REPO" --self-check 2>/dev/null)"
  TOK_RC=$?
  if [ "$TOK_RC" -eq 4 ] && printf '%s' "$TOK_OUT" | grep -qF "asserts no tokens"; then
    ok "an anchor with no expected tokens fails --self-check (exit 4)"
  else
    bad "a tokenless anchor did not fail --self-check (rc=$TOK_RC)"
    printf '%s\n' "$TOK_OUT" | sed 's/^/       | /'
  fi
else
  bad "could not construct the tokenless-anchor case (patch did not apply)"
fi

# A bare identifier or literal as a token is the split-token regression: it looks
# like an assertion and cannot state the relationship it is cited for. Self-check
# refuses it, so the table cannot quietly slide back to fragments.
BARETOKEN="$WORK/bare_token_anchor.py"
sed 's|(".duty = CONFIG_RK_BACKLIGHT_NORMAL,",)|("CONFIG_RK_BACKLIGHT_NORMAL",)|' \
  "$CHECKER" > "$BARETOKEN"
if ! cmp -s "$CHECKER" "$BARETOKEN"; then
  BARE_OUT="$("$PY" "$BARETOKEN" --root "$REPO" --self-check 2>/dev/null)"
  BARE_RC=$?
  if [ "$BARE_RC" -eq 4 ] \
     && printf '%s' "$BARE_OUT" | grep -qF "is a bare identifier or literal"; then
    ok "a bare-identifier token fails --self-check (exit 4)"
  else
    bad "a bare-identifier token did not fail --self-check (rc=$BARE_RC)"
    printf '%s\n' "$BARE_OUT" | sed 's/^/       | /'
  fi
else
  bad "could not construct the bare-token case (patch did not apply)"
fi

echo "== the suppression fingerprint is pinned =="

# A suppression is truthful qualified prose the guard deliberately chose *not* to
# report -- it never changes the exit code, and a run whose only findings are
# suppressions is still green. That is exactly why the set needs pinning here:
# widening a suppressor is invisible in an exit code and shows up only as a longer
# list nobody diffs. Pinning it turns a change into review friction, which is the
# intended cost, and is NOT a claim that any of these lines is untruthful.
BASEREPORT="$WORK/baseline_suppressions.json"
run "$REPO" --self-check --report "$BASEREPORT"
ACTUAL_SUPPRESSION_FINGERPRINT="$(fingerprint "$BASEREPORT")"
if [ -z "$ACTUAL_SUPPRESSION_FINGERPRINT" ]; then
  bad "could not read the suppression set from the report"
elif [ "$ACTUAL_SUPPRESSION_FINGERPRINT" = "$EXPECTED_SUPPRESSION_FINGERPRINT" ]; then
  ok "the repository's suppression set matches the pinned fingerprint"
else
  bad "the suppression set no longer matches the pinned fingerprint.
       This is REVIEW FRICTION, not a runtime failure: a suppression is truthful
       qualified prose the guard chose not to report, and the checker itself still
       exits 0 on a run whose only findings are suppressions. Nothing here says any
       line is untruthful -- it says a different set of lines is now being excused.

       Below, '<' is the pinned baseline and '>' is what this repository produces.
       Only rule|file|suppressor|count is compared; line numbers and matched prose
       are excluded on purpose, so a line moving or a sentence being reworded will
       not show up here. A changed trailing count means the same suppressor is now
       excusing a different NUMBER of hits for that rule in that file.

       What to do:
         1. Run  $PY scripts/check_docs_identity.py --self-check
            and read the RK-IDENT-SUPPRESSED lines.
         2. For each '>' line with no matching '<': say in review which suppressor
            is now excusing which prose, and why that prose is legitimately
            qualified. If it is NOT legitimate, a suppressor has widened -- narrow
            the rule's 'unless' clause instead of updating this baseline. A line
            that differs only in its count is that same conversation about the
            extra hits, not a formality.
         3. For each '<' line with no matching '>': the prose that needed the
            excuse is gone or was rewritten. Confirm that is intended.
         4. Only then update EXPECTED_SUPPRESSION_FINGERPRINT at the top of
            scripts/test_check_docs_identity.sh to the '>' set."
  diff <(printf '%s\n' "$EXPECTED_SUPPRESSION_FINGERPRINT") \
       <(printf '%s\n' "$ACTUAL_SUPPRESSION_FINGERPRINT") | sed 's/^/       | /'
fi

# The three controls below prove what shape the fingerprint has: what it must catch
# and what it must ignore. They run against a FIXED SYNTHETIC suppression set, never
# against this repository's live one.
#
# That is the point. Derived from the live baseline, a control asserting "these two
# fingerprints are equal to EXPECTED" fails the moment the repository legitimately
# gains or loses a suppression -- so one honest documentation edit would report both
# "the baseline moved" (true, and the intended review friction) and "the fingerprint
# no longer ignores line numbers" (false, and a diagnostic pointing at the wrong
# thing). Pinning the controls to synthetic rows keeps the diagnosis one failure with
# one cause.
SYNTH_SUPPRESSIONS="$WORK/synthetic_suppressions.json"
cat > "$SYNTH_SUPPRESSIONS" <<'EOF'
{
  "suppressions": [
    {"rule": "COLOR_DEPTH", "file": "docs/synthetic_one.md", "line": 12,
     "match": "24-bit colour", "suppressor": "pixel format"},
    {"rule": "TOUCH_VARIANT", "file": "docs/synthetic_two.md", "line": 34,
     "match": "CST816D", "suppressor": "datasheet"}
  ]
}
EOF
SYNTH_FINGERPRINT="$(cat <<'EOF'
COLOR_DEPTH|docs/synthetic_one.md|pixel format|1
TOUCH_VARIANT|docs/synthetic_two.md|datasheet|1
EOF
)"

# Control for the three below: the synthetic rows must fingerprint to the stated
# value, or an assertion of "unchanged" could pass on two empty strings.
SYNTH_ACTUAL="$(fingerprint "$SYNTH_SUPPRESSIONS")"
if [ "$SYNTH_ACTUAL" = "$SYNTH_FINGERPRINT" ]; then
  ok "the synthetic suppression set fingerprints to its stated value"
else
  bad "the synthetic suppression set does not fingerprint as stated, so the cases below prove nothing"
  diff <(printf '%s\n' "$SYNTH_FINGERPRINT") <(printf '%s\n' "$SYNTH_ACTUAL") | sed 's/^/       | /'
fi

# One: a suppressor swapped for another, with the total unchanged -- the change a
# scalar total cannot see, and the whole reason the fingerprint carries triples.
SYNTH_SUBST="$WORK/synthetic_substituted.json"
if "$PY" -c '
import json, sys
d = json.load(open(sys.argv[1]))
d["suppressions"][0]["suppressor"] = "framebuffer format"
json.dump(d, open(sys.argv[2], "w"))
' "$SYNTH_SUPPRESSIONS" "$SYNTH_SUBST" 2>/dev/null; then
  SUBST_ACTUAL="$(fingerprint "$SYNTH_SUBST")"
  SUBST_N="$(printf '%s\n' "$SUBST_ACTUAL" | grep -c .)"
  SYNTH_N="$(printf '%s\n' "$SYNTH_FINGERPRINT" | grep -c .)"
  if [ "$SUBST_N" -ne "$SYNTH_N" ]; then
    bad "the substitution case changed the line count ($SUBST_N vs $SYNTH_N), so it does not test what it claims"
  elif [ "$SUBST_ACTUAL" = "$SYNTH_FINGERPRINT" ]; then
    bad "substituting one suppressor did not change the fingerprint"
  else
    ok "a one-for-one suppressor substitution fails the fingerprint at unchanged count ($SYNTH_N)"
  fi
else
  bad "could not build the substituted suppression report"
fi

# Two: an ADDITIONAL suppressed hit inside a triple that is already pinned. Without
# the trailing count this is invisible -- the set of triples is unchanged -- and an
# existing baseline line becomes a standing licence for as many further hits as
# anyone cares to add under the same excuse. It must fail, and it must fail by the
# count alone: the added row deliberately carries a different line and different
# matched prose, neither of which the fingerprint reads.
SYNTH_EXTRA="$WORK/synthetic_extra_hit.json"
if "$PY" -c '
import json, sys
d = json.load(open(sys.argv[1]))
extra = dict(d["suppressions"][1])
extra["line"] = 99
extra["match"] = "a second CST816D sentence in the same file"
d["suppressions"].append(extra)
json.dump(d, open(sys.argv[2], "w"))
' "$SYNTH_SUPPRESSIONS" "$SYNTH_EXTRA" 2>/dev/null; then
  EXTRA_ACTUAL="$(fingerprint "$SYNTH_EXTRA")"
  EXTRA_TRIPLES="$(printf '%s\n' "$EXTRA_ACTUAL" | grep -c .)"
  if [ "$EXTRA_TRIPLES" -ne "$(printf '%s\n' "$SYNTH_FINGERPRINT" | grep -c .)" ]; then
    bad "the extra-hit case added a new triple, so it does not test what it claims"
  elif [ "$EXTRA_ACTUAL" = "$SYNTH_FINGERPRINT" ]; then
    bad "an extra suppressed hit inside an already-pinned triple did not change the fingerprint"
  elif printf '%s\n' "$EXTRA_ACTUAL" | grep -qxF "TOUCH_VARIANT|docs/synthetic_two.md|datasheet|2"; then
    ok "an extra suppressed hit in an existing triple fails the fingerprint, and the count says how many"
  else
    bad "the extra-hit fingerprint changed but does not report the new count"
    printf '%s\n' "$EXTRA_ACTUAL" | sed 's/^/       | /'
  fi
else
  bad "could not build the extra-hit suppression report"
fi

# Three: and it must NOT churn on the two things deliberately excluded, or it would
# fail so often that updating the constant becomes reflex rather than review. Every
# line number moves and every matched string is replaced; nothing may change.
SYNTH_LINESHIFT="$WORK/synthetic_lineshift.json"
if "$PY" -c '
import json, sys
d = json.load(open(sys.argv[1]))
for r in d["suppressions"]:
    r["line"] = r["line"] + 1000
    r["match"] = "entirely different prose"
json.dump(d, open(sys.argv[2], "w"))
' "$SYNTH_SUPPRESSIONS" "$SYNTH_LINESHIFT" 2>/dev/null; then
  if [ "$(fingerprint "$SYNTH_LINESHIFT")" = "$SYNTH_FINGERPRINT" ]; then
    ok "the fingerprint ignores suppression line numbers and matched prose"
  else
    bad "the fingerprint changed when only line numbers and matched prose changed"
  fi
else
  bad "could not build the line-shifted suppression report"
fi

echo "== precedence: a structure failure never hides a contradiction =="

BOTH="$(fresh_tree both)"
cp "$FIXTURES/violation_PANEL_AMOLED.md" "$BOTH/docs/scratch.md"
grep -v '^## Identity summary' "$BASE/$BOARD" > "$BOTH/$BOARD.tmp"
mv "$BOTH/$BOARD.tmp" "$BOTH/$BOARD"
expect_rc "structure outranks violation in the exit code" 2 "$BOTH"
expect_out "the outranked violation is still printed" "RK-IDENT-VIOLATION: PANEL_AMOLED"
expect_out "the structure failure is printed too" "RK-IDENT-STRUCTURE"

ALL3="$(fresh_tree all_three)"
cp "$FIXTURES/retired_marker.md" "$ALL3/docs/scratch.md"
grep -v '^## Identity summary' "$BASE/$BOARD" > "$ALL3/$BOARD.tmp"
mv "$ALL3/$BOARD.tmp" "$ALL3/$BOARD"
rm -f "$ALL3/docs/esp/ROTARY_ENCODER.md"
expect_rc "missing outranks structure, marker abuse and violation" 3 "$ALL3"
expect_out "missing is printed" "RK-IDENT-MISSING"
expect_out "structure is still printed" "RK-IDENT-STRUCTURE"
expect_out "marker abuse is still printed" "RK-IDENT-MARKER-ABUSE"
expect_out "the violation is still printed" "RK-IDENT-VIOLATION: PANEL_AMOLED"

echo "== --self-check is live =="

run "$REPO" --self-check
expect_out "self-check reports zero failures on the shipped rules" "selfcheck=0"

BROKEN="$WORK/broken_checker.py"
sed 's|r"\\btouch\[\\s_-\]\*amoled\\b"|r"zzz-this-can-never-match"|' "$CHECKER" > "$BROKEN"
if grep -q 'zzz-this-can-never-match' "$BROKEN"; then
  BROKEN_OUT="$("$PY" "$BROKEN" --root "$REPO" --self-check 2>/dev/null)"
  BROKEN_RC=$?
  if [ "$BROKEN_RC" -eq 4 ] && printf '%s' "$BROKEN_OUT" | grep -qF "RK-IDENT-SELFCHECK"; then
    ok "a rule whose regex stops matching its own probe fails --self-check (exit 4)"
  else
    bad "broken rule table did not fail --self-check (rc=$BROKEN_RC)"
    printf '%s\n' "$BROKEN_OUT" | sed 's/^/       | /'
  fi
  # Without --self-check the broken rule is silently inert: that is exactly why
  # the flag exists, and CI passes it.
  BROKEN_OUT="$("$PY" "$BROKEN" --root "$REPO" 2>/dev/null)"
  BROKEN_RC=$?
  if [ "$BROKEN_RC" -eq 0 ]; then
    ok "without --self-check the same breakage is silent (so CI must pass the flag)"
  else
    bad "expected the broken checker to pass without --self-check, got $BROKEN_RC"
  fi
else
  bad "could not construct the broken-checker case (patch did not apply)"
fi

# A suppressor that swallows its own rule is the other way a rule can rot.
WIDE="$WORK/wide_unless.py"
sed 's@unless=r"(datasheet@unless=r"(via@' "$CHECKER" > "$WIDE"
if ! cmp -s "$CHECKER" "$WIDE"; then
  WIDE_OUT="$("$PY" "$WIDE" --root "$REPO" --self-check 2>/dev/null)"
  WIDE_RC=$?
  if [ "$WIDE_RC" -eq 4 ] && printf '%s' "$WIDE_OUT" | grep -qF "also suppresses the rule's own probe"; then
    ok "an over-wide unless pattern fails --self-check (exit 4)"
  else
    bad "over-wide unless pattern did not fail --self-check (rc=$WIDE_RC)"
    printf '%s\n' "$WIDE_OUT" | sed 's/^/       | /'
  fi
else
  bad "could not construct the over-wide-unless case (patch did not apply)"
fi

# And an allowance parked on a current-fact section must be refused by
# self-check, not merely by convention.
MISPLACED="$WORK/misplaced_allowance.py"
sed 's|CANONICAL: ("## Historical claims, now superseded",)|CANONICAL: ("## Identity summary",)|' \
  "$CHECKER" > "$MISPLACED"
if ! cmp -s "$CHECKER" "$MISPLACED"; then
  MIS_OUT="$("$PY" "$MISPLACED" --root "$REPO" --self-check 2>/dev/null)"
  MIS_RC=$?
  if [ "$MIS_RC" -eq 4 ] && printf '%s' "$MIS_OUT" | grep -qF "must never carry a section allowance"; then
    ok "an allowance on a current-fact section fails --self-check (exit 4)"
  else
    bad "misplaced section allowance did not fail --self-check (rc=$MIS_RC)"
    printf '%s\n' "$MIS_OUT" | sed 's/^/       | /'
  fi
else
  bad "could not construct the misplaced-allowance case (patch did not apply)"
fi

# A file skip that is not root-level is refused too, so SKIP_FILES cannot grow
# into a directory amnesty.
DEEPSKIP="$WORK/deep_skip.py"
sed 's|^SKIP_FILES = ("CHANGELOG.md",)$|SKIP_FILES = ("docs/esp/DISPLAY.md",)|' "$CHECKER" > "$DEEPSKIP"
if ! cmp -s "$CHECKER" "$DEEPSKIP"; then
  DEEP_OUT="$("$PY" "$DEEPSKIP" --root "$REPO" --self-check 2>/dev/null)"
  DEEP_RC=$?
  if [ "$DEEP_RC" -eq 4 ] && printf '%s' "$DEEP_OUT" | grep -qF "root-level only"; then
    ok "a non-root-level file skip fails --self-check (exit 4)"
  else
    bad "non-root file skip did not fail --self-check (rc=$DEEP_RC)"
    printf '%s\n' "$DEEP_OUT" | sed 's/^/       | /'
  fi
else
  bad "could not construct the deep-skip case (patch did not apply)"
fi

# Putting .github back into SKIP_DIRS must fail self-check, because that is the
# regression that made the documented scope and the real scope disagree.
GHSKIP="$WORK/github_skipped.py"
sed 's|^        "\.git",$|        ".git",\n        ".github",|' "$CHECKER" > "$GHSKIP"
if ! cmp -s "$CHECKER" "$GHSKIP"; then
  GH_OUT="$("$PY" "$GHSKIP" --root "$REPO" --self-check 2>/dev/null)"
  GH_RC=$?
  if [ "$GH_RC" -eq 4 ] && printf '%s' "$GH_OUT" | grep -qF "must stay scannable"; then
    ok "re-excluding .github fails --self-check (exit 4)"
  else
    bad "re-excluding .github did not fail --self-check (rc=$GH_RC)"
    printf '%s\n' "$GH_OUT" | sed 's/^/       | /'
  fi
else
  bad "could not construct the .github-skip case (patch did not apply)"
fi

echo "== reporting and usage contract =="

REPORT="$WORK/report.json"
run "$REPO" --self-check --report "$REPORT"
if [ -s "$REPORT" ] && "$PY" -c "
import json,sys
d=json.load(open(sys.argv[1]))
assert d['token']=='RK-IDENT-OK', d['token']
assert d['exit_code']==0
assert d['violations']==[]
assert d['marker_abuse']==[]
assert d['structure_failures']==[]
assert d['missing']==[]
assert isinstance(d['suppressions'], list)
assert all(r['file'] and r['line']>0 and r['rule'] and r['suppressor'] for r in d['suppressions'])
assert len(d['rules'])>=9
assert len(d['files_scanned'])>=20
assert not any(f.startswith('scripts/fixtures') for f in d['files_scanned'])
assert 'CHANGELOG.md' not in d['files_scanned']
assert 'README.md' in d['files_scanned']
assert '.github/RELEASE_TEMPLATE.md' in d['files_scanned']
" "$REPORT" 2>/dev/null; then
  ok "--report writes a JSON report whose fields agree with the exit code"
else
  bad "--report produced no usable JSON report"
fi

VIOLREPORT="$WORK/viol.json"
VTREE="$(fresh_tree violation_report)"
cp "$FIXTURES/violation_COLOR_DEPTH.md" "$VTREE/docs/scratch.md"
run "$VTREE" --report "$VIOLREPORT"
if [ "$RUN_RC" -eq 1 ] && "$PY" -c "
import json,sys
d=json.load(open(sys.argv[1]))
assert d['token']=='RK-IDENT-VIOLATION', d['token']
assert d['exit_code']==1
ids={v['rule'] for v in d['violations']}
assert 'COLOR_DEPTH' in ids, ids
assert all(v['line']>0 and v['file'] for v in d['violations'])
" "$VIOLREPORT" 2>/dev/null; then
  ok "a failing run still writes a report naming the rule, file and line"
else
  bad "failing run did not write a usable report"
fi

ABUSEREPORT="$WORK/abuse.json"
ATREE="$(fresh_tree abuse_report)"
cp "$FIXTURES/other_target_shipping.md" "$ATREE/docs/scratch.md"
run "$ATREE" --report "$ABUSEREPORT"
if [ "$RUN_RC" -eq 1 ] && "$PY" -c "
import json,sys
d=json.load(open(sys.argv[1]))
assert d['marker_abuse'], d['marker_abuse']
assert all(r['file'] and r['line']>0 and r['message'] for r in d['marker_abuse'])
" "$ABUSEREPORT" 2>/dev/null; then
  ok "marker abuse is reported with file, line and message"
else
  bad "marker abuse report was not usable"
fi

expect_rc "an unknown flag is a usage error" 64 "$REPO" --no-such-flag
expect_out "usage error is named" "RK-IDENT-USAGE"

HELP_OUT="$("$PY" "$CHECKER" --help 2>&1)"
HELP_RC=$?
if [ "$HELP_RC" -eq 0 ] \
   && printf '%s' "$HELP_OUT" | grep -qF "phrase tripwire" \
   && printf '%s' "$HELP_OUT" | grep -qF "not semantic or" \
   && printf '%s' "$HELP_OUT" | grep -qF "factual proof" \
   && ! printf '%s' "$HELP_OUT" | grep -qF "RK-IDENT-USAGE"; then
  ok "--help exits 0 and states the guard's epistemic limit"
else
  bad "--help is not successful or does not state the guard's limit (rc=$HELP_RC)"
  printf '%s\n' "$HELP_OUT" | sed 's/^/       | /'
fi

# `--quiet` gates only the per-hit suppression and violation lines. Pin the help text
# to that, and pin the behaviour it describes, so the two cannot drift apart: an
# earlier "print only the summary token" promised silence it never delivered.
if printf '%s' "$HELP_OUT" | grep -qF "suppress the per-hit suppression and violation lines" \
   && ! printf '%s' "$HELP_OUT" | grep -qF "print only the summary token"; then
  ok "--help describes --quiet as gating only suppression and violation lines"
else
  bad "--quiet help text does not match its documented behaviour"
  printf '%s\n' "$HELP_OUT" | sed 's/^/       | /'
fi

QUIETCASE="$(fresh_tree quiet_behaviour)"
cp "$FIXTURES/violation_PANEL_AMOLED.md" "$QUIETCASE/docs/scratch.md"
cp "$FIXTURES/comment_unterminated.md" "$QUIETCASE/docs/dangling.md"
cp "$FIXTURES/retired_marker.md" "$QUIETCASE/docs/retired.md"
run "$QUIETCASE" --quiet
expect_no_out "--quiet drops the per-hit violation lines" "RK-IDENT-VIOLATION: PANEL_AMOLED"
expect_no_out "--quiet drops the quoting note printed with them" \
  "quote source in a fenced code block"
expect_out "--quiet still prints STRUCTURE lines" "RK-IDENT-STRUCTURE: docs/dangling.md"
expect_out "--quiet still prints MARKER-ABUSE lines" "RK-IDENT-MARKER-ABUSE: docs/retired.md"
expect_out "--quiet still prints the summary token" "RK-IDENT-STRUCTURE: files="

expect_rc "an empty --root value is a usage error" 64 ""
expect_out "empty argument is named" "RK-IDENT-USAGE"

run "$REPO" --report "$WORK/no/such/dir/report.json"
if [ "$RUN_RC" -eq 64 ]; then
  ok "an unwritable report path is a usage error (exit 64)"
else
  bad "unwritable report path returned $RUN_RC, wanted 64"
fi

echo "== the step summary renderer =="

# This block exists because the renderer used to be a heredoc inside the workflow
# and could not be tested. It indexed every detail row as row["rule"], which
# marker_abuse rows do not have, so the first marker-abuse finding turned the
# summary into a KeyError -- on exactly the runs that needed a summary.
SUMMARIZER="$REPO/scripts/summarize_docs_identity.py"
ALLGROUPS="$FIXTURES/report_all_groups.json"

# sum_run <report path or ''> -> sets SUM_OUT and SUM_RC
sum_run() {
  if [ -n "${1:-}" ]; then
    SUM_OUT="$("$PY" "$SUMMARIZER" --report "$1" 2>/dev/null)"
  else
    SUM_OUT="$("$PY" "$SUMMARIZER" 2>/dev/null)"
  fi
  SUM_RC=$?
}

sum_expect() {
  local label="$1" needle="$2"
  if printf '%s' "$SUM_OUT" | grep -qF -- "$needle"; then
    ok "$label"
  else
    bad "$label: summary did not contain '$needle'"
    printf '%s\n' "$SUM_OUT" | sed 's/^/       | /'
  fi
}

if [ ! -f "$SUMMARIZER" ]; then
  bad "renderer missing at scripts/summarize_docs_identity.py"
elif [ ! -f "$ALLGROUPS" ]; then
  bad "synthetic report missing at scripts/fixtures/docs_identity/report_all_groups.json"
else
  # A synthetic report carrying every group at once, including the marker_abuse
  # rows that used to crash the heredoc.
  sum_run "$ALLGROUPS"
  if [ "$SUM_RC" -eq 0 ]; then
    ok "renderer exits 0 on a report containing every group"
  else
    bad "renderer exited $SUM_RC on the all-groups report"
    printf '%s\n' "$SUM_OUT" | sed 's/^/       | /'
  fi
  sum_expect "renderer never raises KeyError" "Docs identity guard"
  if printf '%s' "$SUM_OUT" | grep -qiE 'traceback|keyerror'; then
    bad "renderer output contains a traceback"
    printf '%s\n' "$SUM_OUT" | sed 's/^/       | /'
  else
    ok "renderer output contains no traceback"
  fi

  # Counts, taken from the fixture: 3 files, 2 rules, 1 violation, 2 marker abuse,
  # 2 structure failures, 1 missing, 1 self-check failure, 2 suppressions.
  sum_expect "token row is rendered" '| token | `RK-IDENT-MISSING` |'
  sum_expect "exit code row is rendered" '| exit code | 3 |'
  sum_expect "files-scanned count is 3" '| files scanned | 3 |'
  sum_expect "rules count is 2" '| rules | 2 |'
  sum_expect "violations count is 1" '| violations | 1 |'
  sum_expect "marker-abuse count is 2" '| marker abuse | 2 |'
  sum_expect "structure count is 2" '| structure failures | 2 |'
  sum_expect "missing count is 1" '| missing | 1 |'
  sum_expect "self-check count is 1" '| self-check failures | 1 |'
  sum_expect "suppression count is rendered as informational" \
    '| suppressions (informational) | 2 |'

  # Every group renders a heading and a usable detail line.
  sum_expect "violations section is present" "### violations"
  sum_expect "violation detail names rule, file, line and message" \
    '- `PANEL_AMOLED` docs/esp/hw-reference/board.md:27'
  # The indented-code consequence is rendered from the report, beneath the findings
  # it explains, so a reader of the step summary meets the remedy where the
  # violations are rather than in the checker's docstring.
  sum_expect "the violations section renders the report's quoting note" \
    "a four-space indented block is scanned as prose"
  sum_expect "marker-abuse section is present" "### marker abuse"
  sum_expect "marker-abuse detail names file and line without a rule" \
    "- docs/howto/PORTING.md:42"
  sum_expect "second marker-abuse row is rendered too" "- docs/esp/DISPLAY.md:9"
  sum_expect "structure section is present" "### structure failures"
  sum_expect "structure detail is rendered" "corrected value 'ST77916' no longer present"
  sum_expect "an unterminated-fence structure row is rendered" \
    "docs/esp/DISPLAY.md:12: unterminated code fence"
  sum_expect "missing section is present" "### missing"
  sum_expect "missing detail is rendered" "required document absent: docs/esp/ROTARY_ENCODER.md"
  sum_expect "self-check section is present" "### selfcheck failures"
  sum_expect "self-check detail is rendered" "probe does not match its own pattern"

  # Suppressions render under their own heading, carry the suppressor as evidence,
  # and are labelled informational so a reader does not read them as findings.
  sum_expect "suppressions section is present and labelled" "### suppressions (informational)"
  sum_expect "suppressions section explains it does not affect the exit code" \
    "do not affect the exit code"
  sum_expect "a suppression row names rule, file, line, match and suppressor" \
    '- `TOUCH_VARIANT` docs/esp/hw-reference/cst816d.md:8 — CST816D suppressed by `datasheet`'
  sum_expect "the second suppression row is rendered too" \
    '- `COLOR_DEPTH` docs/esp/hw-reference/board.md:118 — 24-bit colour suppressed by `RGB888`'

  SUM_LINES="$(printf '%s\n' "$SUM_OUT" | grep -c '^- ')"
  if [ "$SUM_LINES" -eq 9 ]; then
    ok "exactly the nine synthetic detail rows are rendered"
  else
    bad "expected 9 detail lines, got $SUM_LINES"
    printf '%s\n' "$SUM_OUT" | sed 's/^/       | /'
  fi

  # A clean report renders the counts, no detail sections, and the honesty note.
  CLEANREPORT="$WORK/clean_report.json"
  run "$REPO" --self-check --report "$CLEANREPORT"
  sum_run "$CLEANREPORT"
  if [ "$SUM_RC" -eq 0 ]; then
    ok "renderer exits 0 on the repository's own clean report"
  else
    bad "renderer exited $SUM_RC on a clean report"
  fi
  sum_expect "clean report renders the OK token" '| token | `RK-IDENT-OK` |'
  if printf '%s' "$SUM_OUT" \
      | grep -qE '^### (violations|marker abuse|structure failures|missing|selfcheck failures)$'; then
    bad "clean report rendered a finding section"
    printf '%s\n' "$SUM_OUT" | sed 's/^/       | /'
  else
    ok "clean report renders no finding sections"
  fi
  # The repository's own report carries suppressions, and they must be visible
  # without being mistaken for findings: the informational section renders and the
  # closing "nothing found" note still appears.
  sum_expect "clean report still renders the informational suppressions section" \
    "### suppressions (informational)"
  sum_expect "clean report states the tripwire limit" "phrase tripwire, not a proof"

  # A report with findings *and* suppressions renders both, and a report with no
  # suppressions renders no suppressions section at all.
  printf '{"token": "RK-IDENT-OK", "exit_code": 0, "suppressions": []}' \
    > "$WORK/no_suppressions.json"
  sum_run "$WORK/no_suppressions.json"
  if printf '%s' "$SUM_OUT" | grep -qF "### suppressions"; then
    bad "an empty suppressions list still rendered a section"
    printf '%s\n' "$SUM_OUT" | sed 's/^/       | /'
  else
    ok "an empty suppressions list renders no section"
  fi
  sum_expect "an empty suppressions list still renders a zero count" \
    '| suppressions (informational) | 0 |'

  # Degraded inputs must still be exit 0 and must say so in prose, because the
  # workflow calls this with if: always().
  sum_run "$WORK/no_such_report.json"
  if [ "$SUM_RC" -eq 0 ]; then
    ok "a missing report is exit 0"
  else
    bad "missing report exited $SUM_RC"
  fi
  sum_expect "missing report is named as unusable" "No usable report"

  printf 'not json at all' > "$WORK/broken_report.json"
  sum_run "$WORK/broken_report.json"
  if [ "$SUM_RC" -eq 0 ]; then
    ok "a malformed report is exit 0"
  else
    bad "malformed report exited $SUM_RC"
  fi
  sum_expect "malformed report is named as unusable" "No usable report"

  # A report missing the fields the renderer reads must degrade, not raise.
  printf '{"token": "RK-IDENT-VIOLATION", "violations": [{"file": "x.md"}, "a bare string"]}' \
    > "$WORK/partial_report.json"
  sum_run "$WORK/partial_report.json"
  if [ "$SUM_RC" -eq 0 ]; then
    ok "a report with absent fields is exit 0"
  else
    bad "partial report exited $SUM_RC"
    printf '%s\n' "$SUM_OUT" | sed 's/^/       | /'
  fi
  sum_expect "absent counts degrade to a placeholder" '| files scanned | ? |'
  sum_expect "a row without a line still renders its file" "- x.md"
  sum_expect "a string row inside a dict group still renders" "- a bare string"

  sum_run ""
  if [ "$SUM_RC" -eq 0 ]; then
    ok "no --report and no RK_IDENT_REPORT is exit 0"
  else
    bad "renderer with no report path exited $SUM_RC"
  fi

  if RK_IDENT_REPORT="$ALLGROUPS" "$PY" "$SUMMARIZER" 2>/dev/null \
      | grep -qF '| marker abuse | 2 |'; then
    ok "RK_IDENT_REPORT is honoured when --report is absent"
  else
    bad "RK_IDENT_REPORT was not honoured"
  fi

  if "$PY" "$SUMMARIZER" --no-such-flag >/dev/null 2>&1; then
    ok "an unknown flag is still exit 0 (a summary must never fail the step)"
  else
    bad "an unknown flag made the renderer exit non-zero"
  fi

  # A bad invocation and `--help` both leave argparse via SystemExit, so the
  # always-exit-0 contract used to answer `--help` with "invoked incorrectly".
  # `--help` must print argparse's own help and nothing else.
  SUMHELP_OUT="$("$PY" "$SUMMARIZER" --help 2>&1)"
  SUMHELP_RC=$?
  if [ "$SUMHELP_RC" -eq 0 ] \
     && printf '%s' "$SUMHELP_OUT" | grep -qF -- "--report" \
     && ! printf '%s' "$SUMHELP_OUT" | grep -qF "invoked incorrectly"; then
    ok "--help prints help and is not reported as an incorrect invocation"
  else
    bad "--help was swallowed into the incorrect-invocation path (rc=$SUMHELP_RC)"
    printf '%s\n' "$SUMHELP_OUT" | sed 's/^/       | /'
  fi

  # ... while a genuinely bad invocation still says so, and still exits 0.
  SUMBAD_OUT="$("$PY" "$SUMMARIZER" --no-such-flag 2>/dev/null)"
  SUMBAD_RC=$?
  if [ "$SUMBAD_RC" -eq 0 ] \
     && printf '%s' "$SUMBAD_OUT" | grep -qF "invoked incorrectly"; then
    ok "a bad invocation still renders the incorrect-invocation summary at exit 0"
  else
    bad "a bad invocation no longer names itself (rc=$SUMBAD_RC)"
    printf '%s\n' "$SUMBAD_OUT" | sed 's/^/       | /'
  fi
fi

echo "== the workflow calls what the tests cover =="

WF="$REPO/.github/workflows/docs-identity.yml"
if [ ! -f "$WF" ]; then
  bad "workflow missing at .github/workflows/docs-identity.yml"
else
  for needle in \
    'bash scripts/test_check_docs_identity.sh' \
    'scripts/check_docs_identity.py' \
    'scripts/summarize_docs_identity.py' \
    '--self-check' \
    'contents: read' \
    'persist-credentials: false' \
    'cancel-in-progress'; do
    if grep -qF -- "$needle" "$WF"; then
      ok "workflow references $needle"
    else
      bad "workflow does not reference $needle"
    fi
  done
  # No path filters: a skipped job cannot be a meaningful required status check.
  if grep -qE '^\s*paths:' "$WF"; then
    bad "workflow still has a paths: filter, so it can be skipped on a pull request"
  else
    ok "workflow has no paths: filter, so it always reports"
  fi
  # No embedded interpreter block: the renderer must stay a tested file.
  if grep -qE 'python3 - ' "$WF"; then
    bad "workflow still embeds an inline Python program"
  else
    ok "workflow embeds no inline Python program"
  fi
fi

echo "== the guard is reachable locally, without ESP-IDF =="

# A guard that only ever runs in one workflow is a guard contributors meet for the
# first time in a red pull request. The local route is the direct command, named in
# the two documents a contributor or an agent actually reads before editing docs.
#
# It is deliberately NOT wired into scripts/ci_sanity.sh. That script's very next
# step is `idf.py build`, so it cannot run on a host without ESP-IDF, which is the
# host this guard exists for; it is not exercised by any workflow in this
# repository; and a check bolted to the front of a script nobody can run is not a
# gate. A direct command that works everywhere is the honest version.
LOCAL_CMD='python3 scripts/check_docs_identity.py --self-check'

# The invocation itself: standard library Python, no cmake, no idf.py, no network.
LOCAL_OUT="$("$PY" "$CHECKER" --self-check 2>/dev/null)"
LOCAL_RC=$?
if [ "$LOCAL_RC" -eq 0 ] && printf '%s' "$LOCAL_OUT" | grep -qF "RK-IDENT-OK"; then
  ok "the documented local command succeeds on this host with no ESP-IDF present"
else
  bad "the documented local command failed standalone (rc=$LOCAL_RC)"
  printf '%s\n' "$LOCAL_OUT" | sed 's/^/       | /'
fi

# Both documents must name the command verbatim, so the instruction and the command
# cannot drift apart.
for doc in AGENTS.md docs/dev/DEVELOPMENT.md; do
  if [ ! -f "$REPO/$doc" ]; then
    bad "$doc missing"
  elif grep -qF "$LOCAL_CMD" "$REPO/$doc"; then
    ok "$doc names the local command verbatim"
  else
    bad "$doc does not name '$LOCAL_CMD'"
  fi
done

# Consistency, in the direction that actually rots: the documentation must not
# describe a local gate that is not wired up. Rather than trying to forbid a claim
# in prose, the correct claim is pinned as an exact phrase -- so the state of
# scripts/ci_sanity.sh and the state of the documents cannot disagree without a
# failure here, in either direction.
SANITY="$REPO/scripts/ci_sanity.sh"
SANITY_DISCLAIMER='not wired into `scripts/ci_sanity.sh`'
if [ ! -f "$SANITY" ]; then
  bad "scripts/ci_sanity.sh missing"
elif grep -qF 'check_docs_identity.py' "$SANITY"; then
  # Someone wired it in for real. Then --self-check is mandatory, and the documents
  # must stop saying it is not wired in.
  if grep -A2 -F 'check_docs_identity.py' "$SANITY" | grep -qF -- '--self-check'; then
    ok "ci_sanity.sh runs the guard, and does so with --self-check"
  else
    bad "ci_sanity.sh runs the guard without --self-check"
  fi
  STALE="$(grep -lF "$SANITY_DISCLAIMER" "$REPO/$ADR" "$REPO/docs/dev/DEVELOPMENT.md" 2>/dev/null || true)"
  if [ -z "$STALE" ]; then
    ok "no document still says the guard is not wired into ci_sanity.sh"
  else
    bad "ci_sanity.sh now runs the guard, but these files still say it does not:
$(printf '%s\n' "$STALE" | sed 's/^/       | /')"
  fi
else
  for doc in "$ADR" docs/dev/DEVELOPMENT.md; do
    if grep -qF "$SANITY_DISCLAIMER" "$REPO/$doc"; then
      ok "$doc states that the guard is not wired into ci_sanity.sh"
    else
      bad "ci_sanity.sh does not run the guard, but $doc does not say so"
    fi
  done
fi

echo
printf 'RK-IDENT-FIXTURES: passed=%d failed=%d\n' "$PASS" "$FAIL"
if [ "$FAIL" -ne 0 ]; then
  echo "RK-IDENT-FIXTURES-FAIL"
  exit 1
fi
echo "RK-IDENT-FIXTURES-OK"
