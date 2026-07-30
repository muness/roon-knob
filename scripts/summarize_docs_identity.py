#!/usr/bin/env python3
"""Render a check_docs_identity.py JSON report as a GitHub step summary.

Issue #211. Standard library only, no network, reads one file and writes Markdown
to stdout.

This lives in a file rather than in a heredoc inside the workflow for one reason:
the heredoc version could not be tested, and it was wrong. It indexed every detail
row as ``row["rule"]``, which is true of ``violations`` and false of
``marker_abuse``, so the first marker-abuse finding turned the summary step into a
``KeyError`` traceback -- on exactly the runs a reader most needs the summary.

Contract, so the workflow can call it with ``if: always()``:

*   Exit status is **always 0**. This is a reporting layer; it must never be the
    thing that fails a run, and it must never mask the checker's own exit code.
*   A missing, empty or malformed report is reported as such, in prose, and is
    still exit 0.
*   Absent or unexpected fields degrade to ``?`` or to a repr of the row rather
    than raising. Report shapes drift; summaries should not crash on drift.
*   Source-anchor freshness is counted, not given its own detail group. A stale
    anchor is already a ``structure_failures`` row naming the anchor, the file and
    the missing token, so rendering it twice would read as two findings.
*   The anchor *count* is rendered with the report's own ``source_anchor_scope``
    note directly beneath the table, because a bare count invites the two readings
    it does not support: that every repository-observed row is anchored, and that
    the line numbers those rows cite were checked. Neither is true. The note is
    taken from the report rather than written here so the checker stays the single
    place that states its own scope; a report without the field renders no note.
*   The ``violations`` group is followed by the report's own ``quoting_note`` when
    the report carries one, for the same reason as the anchor scope note: a
    four-space indented block is scanned as prose rather than exempted as code, so
    a reader looking at a violation on a line they meant as a quotation needs the
    remedy -- fence it -- next to the finding. The text comes from the report so the
    checker stays the single authority; a report without the field renders no note.
*   Findings and informational rows are rendered separately. ``DETAIL_GROUPS`` are
    findings: their presence is why a run is red. ``INFO_GROUPS`` -- currently
    ``suppressions``, the hits a rule's ``unless`` clause excused -- are rendered
    under their own heading and never affect the checker's exit code. They are
    here so a widened suppressor is visible in the step summary rather than only
    in the checker's regexes, and so the "nothing found" note below still appears
    on a clean run that merely excused something.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

TOOL = "summarize_docs_identity"

# Report keys rendered as detail sections, in reporting order. Every one of these
# is a list; entries are dicts for the scanned findings and plain strings for the
# structural ones.
DETAIL_GROUPS = (
    "violations",
    "marker_abuse",
    "structure_failures",
    "missing",
    "selfcheck_failures",
)

# Informational groups: rendered, counted, and deliberately excluded from the
# "was anything found?" test that gates the closing note.
INFO_GROUPS = ("suppressions",)

INFO_NOTES = {
    "suppressions": (
        "Hits a rule's `unless` clause excused, so truthful qualified prose passes. "
        "These do not affect the exit code; they are listed so a widened suppressor "
        "is visible here rather than only in the checker's regexes."
    ),
}

# Count rows: (label, report key). Neither the `rules` row nor the
# `source anchors in table` row is a coverage number -- the first is the size of the
# rule table, not a count of covered alternation branches; the second is the size of
# the anchor table, not a count of repository-observed rows. A stale anchor is a
# `structure_failures` row, so it is counted there rather than twice here.
COUNT_ROWS = (
    ("files scanned", "files_scanned"),
    ("rules", "rules"),
    ("source anchors in table", "source_anchors"),
    ("violations", "violations"),
    ("marker abuse", "marker_abuse"),
    ("structure failures", "structure_failures"),
    ("missing", "missing"),
    ("self-check failures", "selfcheck_failures"),
    ("suppressions (informational)", "suppressions"),
)


def _count(report, key):
    value = report.get(key)
    if isinstance(value, (list, tuple, dict)):
        return str(len(value))
    if isinstance(value, int):
        return str(value)
    return "?"


def _rows(report, key):
    value = report.get(key)
    return value if isinstance(value, (list, tuple)) else []


def format_row(row):
    """One detail bullet.

    Dict rows may or may not carry a ``rule`` -- ``violations`` do, ``marker_abuse``
    does not -- and either may lack ``file``/``line`` in a degraded report. String
    rows (``structure_failures``, ``missing``, ``selfcheck_failures``) pass through.
    ``suppressions`` carry no ``message`` but do carry a ``suppressor``, which is
    the evidence a reader needs, so it is appended rather than dropped.
    """
    if isinstance(row, str):
        return "- %s" % row
    if not isinstance(row, dict):
        return "- %r" % (row,)

    parts = []
    rule = row.get("rule")
    if rule:
        parts.append("`%s`" % rule)

    where = row.get("file")
    if where:
        line = row.get("line")
        if isinstance(line, int) and line > 0:
            where = "%s:%d" % (where, line)
        parts.append(where)

    text = row.get("message") or row.get("match")
    suppressor = row.get("suppressor")
    if suppressor:
        text = "%s suppressed by `%s`" % (text or "match not recorded", suppressor)

    if parts and text:
        return "- %s — %s" % (" ".join(parts), text)
    if parts:
        return "- %s" % " ".join(parts)
    if text:
        return "- %s" % text
    return "- %r" % (row,)


def render(report, path=None):
    """Return the summary as a list of Markdown lines."""
    out = ["## Docs identity guard", ""]

    if report is None:
        out.append("No usable report at `%s`." % (path or "<unset>"))
        out.append("")
        out.append("The checker did not reach its reporting stage; read the step logs.")
        return out

    token = report.get("token", "?")
    exit_code = report.get("exit_code", "?")
    out.append("| Field | Value |")
    out.append("|---|---|")
    out.append("| token | `%s` |" % token)
    out.append("| exit code | %s |" % exit_code)
    for label, key in COUNT_ROWS:
        out.append("| %s | %s |" % (label, _count(report, key)))

    # The checker's own statement of what the anchor count does and does not mean.
    # Rendered from the report so there is one authority for it; absent in an older
    # or degraded report, in which case nothing is claimed here either.
    scope = report.get("source_anchor_scope")
    if isinstance(scope, str) and scope.strip():
        out.append("")
        out.append(scope.strip())

    for group in DETAIL_GROUPS:
        rows = _rows(report, group)
        if not rows:
            continue
        out.append("")
        out.append("### %s" % group.replace("_", " "))
        for row in rows:
            out.append(format_row(row))
        # The indented-code consequence, in the checker's own words, beneath the
        # findings it explains. Absent from an older or degraded report, in which
        # case nothing is claimed here either.
        if group == "violations":
            quoting = report.get("quoting_note")
            if isinstance(quoting, str) and quoting.strip():
                out.append("")
                out.append(quoting.strip())

    for group in INFO_GROUPS:
        rows = _rows(report, group)
        if not rows:
            continue
        out.append("")
        out.append("### %s (informational)" % group.replace("_", " "))
        note = INFO_NOTES.get(group)
        if note:
            out.append("")
            out.append(note)
            out.append("")
        for row in rows:
            out.append(format_row(row))

    if token == "RK-IDENT-OK" and not any(_rows(report, g) for g in DETAIL_GROUPS):
        out.append("")
        out.append(
            "No guarded contradiction found in a known or tested wording. This is a "
            "phrase tripwire, not a proof: see the checker's module docstring for what "
            "it does and does not cover."
        )

    return out


def load(path):
    """Return the parsed report, or None if it cannot be used."""
    try:
        with open(path, encoding="utf-8") as handle:
            report = json.load(handle)
    except (OSError, ValueError):
        return None
    return report if isinstance(report, dict) else None


def main(argv):
    parser = argparse.ArgumentParser(
        prog=TOOL,
        description="Render a check_docs_identity.py JSON report as Markdown.",
    )
    parser.add_argument(
        "--report",
        default=None,
        help="path to the JSON report (default: $RK_IDENT_REPORT)",
    )
    try:
        args = parser.parse_args(argv)
    except SystemExit as exc:
        if exc.code in (0, None):
            # `--help`/`--version` already printed what was asked for. Emitting the
            # "invoked incorrectly" summary on top of it would be a false report.
            return 0
        # Even a bad invocation must not fail the workflow step.
        print("## Docs identity guard")
        print()
        print("The summary renderer was invoked incorrectly; read the step logs.")
        return 0

    path = args.report or os.environ.get("RK_IDENT_REPORT") or ""
    report = load(path) if path else None
    for line in render(report, path):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
