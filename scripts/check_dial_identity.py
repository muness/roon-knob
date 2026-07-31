#!/usr/bin/env python3
"""Verify HiPhi Dial identity and classify every retained legacy identifier."""

from __future__ import annotations

import fnmatch
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INVENTORY = ROOT / "scripts" / "dial_identity_exceptions.json"
SELF_PATHS = {
    "scripts/check_dial_identity.py",
    "scripts/dial_identity_exceptions.json",
}
VALID_CATEGORIES = {
    "compatibility",
    "deferred-external-endpoint",
    "historical-provenance",
}


def repository_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    paths: list[Path] = []
    for relative in result.stdout.splitlines():
        if not relative or relative in SELF_PATHS:
            continue
        if any(part in {".git", ".ci", "build", "managed_components"} for part in Path(relative).parts):
            continue
        path = ROOT / relative
        if path.is_file():
            paths.append(path)
    return sorted(paths)


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def main() -> int:
    data = json.loads(INVENTORY.read_text(encoding="utf-8"))
    errors: list[str] = []

    if data.get("schema_version") != 1:
        fail(errors, "inventory schema_version must be 1")

    tokens = data.get("legacy_tokens", [])
    if not tokens or len(tokens) != len(set(tokens)):
        fail(errors, "legacy_tokens must be a non-empty unique list")

    rules = data.get("exceptions", [])
    rule_hits: Counter[str] = Counter()
    category_hits: Counter[str] = Counter()
    compiled: list[tuple[dict[str, object], re.Pattern[str]]] = []

    for rule in rules:
        rule_id = str(rule.get("id", ""))
        category = str(rule.get("category", ""))
        if not rule_id:
            fail(errors, "every exception needs an id")
        if category not in VALID_CATEGORIES:
            fail(errors, f"{rule_id}: invalid category {category!r}")
        if not rule.get("owner") or not rule.get("retirement"):
            fail(errors, f"{rule_id}: owner and retirement are required")
        if not rule.get("path_globs") or not rule.get("tokens"):
            fail(errors, f"{rule_id}: path_globs and tokens are required")
        unknown = set(rule.get("tokens", [])) - set(tokens)
        if unknown:
            fail(errors, f"{rule_id}: unknown tokens {sorted(unknown)}")
        try:
            pattern = re.compile(str(rule.get("line_regex", "")))
        except re.error as exc:
            fail(errors, f"{rule_id}: invalid line_regex: {exc}")
            continue
        compiled.append((rule, pattern))

    for path in repository_files():
        relative = path.relative_to(ROOT).as_posix()
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        for line_number, line in enumerate(text.splitlines(), start=1):
            for token in tokens:
                if token not in line:
                    continue
                matches: list[dict[str, object]] = []
                for rule, pattern in compiled:
                    if token not in rule["tokens"]:
                        continue
                    if not any(fnmatch.fnmatch(relative, glob) for glob in rule["path_globs"]):
                        continue
                    if not pattern.search(line):
                        continue
                    matches.append(rule)
                if not matches:
                    fail(
                        errors,
                        f"{relative}:{line_number}: unclassified legacy token {token!r}: {line.strip()}",
                    )
                    continue
                for rule in matches:
                    rule_id = str(rule["id"])
                    rule_hits[rule_id] += 1
                category_hits[str(matches[0]["category"])] += 1

    for rule, _ in compiled:
        rule_id = str(rule["id"])
        if rule_hits[rule_id] == 0:
            fail(errors, f"{rule_id}: exception is stale and matched no legacy token")

    for required in data.get("required", []):
        relative = str(required["path"])
        path = ROOT / relative
        if not path.is_file():
            fail(errors, f"{relative}: required file is missing")
            continue
        text = path.read_text(encoding="utf-8")
        needle = str(required["contains"])
        minimum = int(required.get("min_count", 1))
        count = text.count(needle)
        if count < minimum:
            fail(
                errors,
                f"{relative}: expected at least {minimum} occurrence(s) of {needle!r}, found {count}",
            )

    for forbidden in data.get("forbidden", []):
        relative = str(forbidden["path"])
        path = ROOT / relative
        if not path.is_file():
            fail(errors, f"{relative}: forbidden-string target is missing")
            continue
        needle = str(forbidden["contains"])
        if needle in path.read_text(encoding="utf-8"):
            fail(errors, f"{relative}: forbidden active identity remains: {needle!r}")

    if errors:
        print("HiPhi Dial identity check FAILED", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print("HiPhi Dial identity check passed")
    for category in sorted(category_hits):
        print(f"- {category}: {category_hits[category]} classified legacy token hit(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
