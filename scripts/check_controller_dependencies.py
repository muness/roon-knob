#!/usr/bin/env python3
"""Fail-closed direct-include policy for the controller boundary.

This intentionally checks source-level quoted includes, not IDF component or
link dependencies. Dial and Frame are evaluated separately because their CMake
include order resolves target headers differently.
"""

from __future__ import annotations

import argparse
import copy
import fnmatch
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_POLICY = ROOT / "scripts" / "controller_dependency_policy.json"
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)
ANGLE_INCLUDE_RE = re.compile(r"^\s*#\s*include\s*<([^>]+)>", re.MULTILINE)
MACRO_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s+(?!["<])(?P<value>\S+)', re.MULTILINE
)
RAW_STORAGE_API_RE = re.compile(
    r"\bplatform_storage_(?:read|write|defaults)\s*\("
)
RAW_STORAGE_ALLOWED = {
    "common/controller_config.c",
    "common/platform/platform_storage.h",
    "idf_app/main/platform_storage_idf.c",
    "frame_app/main/platform_storage_frame.c",
}
SRC_FILES_RE = re.compile(
    r"set\s*\(\s*SRC_FILES(?P<body>.*?)\n\s*\)", re.DOTALL
)
COMPONENT_REGISTER_RE = re.compile(
    r"idf_component_register\s*\((?P<body>.*?)\n\s*\)", re.DOTALL
)
SET_PROPERTY_RE = re.compile(
    r"set_property\s*\((?P<body>.*?)\)", re.DOTALL
)
QUOTED_VALUE_RE = re.compile(r'"([^"]+)"')
PROJECT_INCLUDE_SUFFIXES = {".h", ".hh", ".hpp", ".inc"}
SOURCE_SUFFIXES = {".c", ".cc", ".cpp"}
SOURCE_TOKEN_RE = re.compile(
    r'(?:"[^"]+"|[A-Za-z0-9_./${}-]+)\.(?:c|cc|cpp|s|S)\b'
)
CMAKE_COMMAND_RE = re.compile(
    r"(?m)^\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\("
)
SUPPORTED_CMAKE_COMMANDS = {
    "idf_component_register",
    "set",
    "set_property",
    "target_compile_definitions",
    "target_compile_options",
}
SUPPORTED_SET_PROPERTIES = {
    "DIRECTORY PROPERTY CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON",
    "TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11",
}


@dataclass(frozen=True, order=True)
class Edge:
    target: str
    source: str
    header: str

    @property
    def key(self) -> str:
        return f"{self.target}|{self.source}|{self.header}"


def relative_path(path: Path, root: Path = ROOT) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def parse_cmake_sources(cmake_path: Path) -> list[Path]:
    text = cmake_path.read_text(encoding="utf-8")
    uncommented = re.sub(r"#.*$", "", text, flags=re.MULTILINE)
    command_names = CMAKE_COMMAND_RE.findall(uncommented)
    unsupported_commands = sorted(
        {
            name
            for name in command_names
            if name.lower() not in SUPPORTED_CMAKE_COMMANDS
        }
    )
    if unsupported_commands:
        raise ValueError(
            f"{relative_path(cmake_path)}: unsupported CMake command(s): "
            + ", ".join(unsupported_commands)
        )
    if sum(name.lower() == "set" for name in command_names) != 1:
        raise ValueError(
            f"{relative_path(cmake_path)}: only the literal SRC_FILES set is supported"
        )
    observed_properties = {
        " ".join(match.group("body").split())
        for match in SET_PROPERTY_RE.finditer(uncommented)
    }
    if (
        len(observed_properties)
        != sum(name.lower() == "set_property" for name in command_names)
        or not observed_properties.issubset(SUPPORTED_SET_PROPERTIES)
    ):
        raise ValueError(
            f"{relative_path(cmake_path)}: unsupported set_property form"
        )
    matches = list(SRC_FILES_RE.finditer(uncommented))
    location = relative_path(cmake_path)
    if len(matches) != 1:
        raise ValueError(
            f"{location}: expected exactly one literal set(SRC_FILES ...)"
        )
    match = matches[0]
    registrations = list(COMPONENT_REGISTER_RE.finditer(uncommented))
    registration_body = (
        registrations[0].group("body") if len(registrations) == 1 else ""
    )
    if (
        uncommented.count("SRC_FILES") != 2
        or len(registrations) != 1
        or not re.search(
            r"\bSRCS\s+\$\{SRC_FILES\}\s+(?:INCLUDE_DIRS\b|$)",
            registration_body,
            re.DOTALL,
        )
        or re.search(r"\b(?:SRC_DIRS|EXCLUDE_SRCS)\b", registration_body)
    ):
        raise ValueError(
            f"{location}: unsupported SRC_FILES mutation or component registration"
        )
    outside_inventory = (
        uncommented[: match.start()]
        + uncommented[match.end() : registrations[0].start()]
        + uncommented[registrations[0].end() :]
    )
    if re.search(r"\btarget_sources\s*\(", outside_inventory):
        raise ValueError(f"{location}: target_sources is outside the enforced contract")
    extra_source = SOURCE_TOKEN_RE.search(
        registration_body.replace("${SRC_FILES}", "")
        + outside_inventory
    )
    if extra_source:
        raise ValueError(
            f"{location}: source outside literal SRC_FILES: {extra_source.group(0)}"
        )
    unparsed = QUOTED_VALUE_RE.sub("", match.group("body")).strip()
    if unparsed:
        raise ValueError(
            f"{location}: SRC_FILES entries must be literal quoted paths: {unparsed}"
        )

    sources: list[Path] = []
    for value in QUOTED_VALUE_RE.findall(match.group("body")):
        source = (cmake_path.parent / value).resolve()
        if source.suffix not in SOURCE_SUFFIXES:
            raise ValueError(f"{location}: unsupported source type: {value}")
        sources.append(source)
    return sources


def project_file_index(root: Path = ROOT) -> dict[str, list[Path]]:
    index: dict[str, list[Path]] = {}
    ignored_parts = {".git", "build", "managed_components"}
    for candidate in root.rglob("*"):
        if not candidate.is_file():
            continue
        if ignored_parts.intersection(candidate.relative_to(root).parts):
            continue
        index.setdefault(candidate.name, []).append(candidate.resolve())
    return index


def normalize_preprocessor_text(text: str) -> str:
    text = text.replace("\\\r\n", "").replace("\\\n", "")

    def replace(match: re.Match[str]) -> str:
        return " " + ("\n" * match.group(0).count("\n"))

    return re.sub(r"/\*.*?\*/", replace, text, flags=re.DOTALL)


def resolve_header(
    include: str,
    source: Path,
    include_dirs: Iterable[Path],
    index: dict[str, list[Path]],
) -> Path | None:
    candidates = [source.parent / include]
    candidates.extend(directory / include for directory in include_dirs)
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return None


def collect_edges(
    policy: dict,
    root: Path = ROOT,
    extra_sources: dict[str, list[Path]] | None = None,
) -> set[Edge]:
    index = project_file_index(root)
    observed: set[Edge] = set()

    for target_name, target in policy["targets"].items():
        cmake_path = root / target["cmake"]
        include_dirs = [(root / path).resolve() for path in target["include_dirs"]]
        pending = list(parse_cmake_sources(cmake_path))
        if extra_sources:
            pending.extend(extra_sources.get(target_name, []))
        visited: set[Path] = set()
        while pending:
            source = pending.pop()
            if source in visited:
                continue
            visited.add(source)
            if not source.is_file():
                raise ValueError(
                    f"{target_name}: configured source is missing: "
                    f"{relative_path(source, root)}"
                )
            source_text = normalize_preprocessor_text(
                source.read_text(encoding="utf-8")
            )
            macro_includes = MACRO_INCLUDE_RE.findall(source_text)
            if macro_includes:
                raise ValueError(
                    f"{relative_path(source, root)}: macro include is outside "
                    f"the enforced contract: {macro_includes[0]}"
                )
            for include in ANGLE_INCLUDE_RE.findall(source_text):
                if Path(include).name in index:
                    raise ValueError(
                        f"{relative_path(source, root)}: repository-local angle "
                        f"include is outside the enforced contract: {include}"
                    )
            for include in INCLUDE_RE.findall(source_text):
                resolved = resolve_header(include, source, include_dirs, index)
                if resolved is None:
                    if Path(include).name in index:
                        raise ValueError(
                            f"{relative_path(source, root)}: unresolved or "
                            f"unreachable repository include: {include}"
                        )
                    continue
                if resolved.suffix in PROJECT_INCLUDE_SUFFIXES and resolved not in visited:
                    pending.append(resolved)
                header = relative_path(resolved, root)
                observed.add(
                    Edge(
                        target=target_name,
                        source=relative_path(source, root),
                        header=header,
                    )
                )
    return observed


def edge_from_key(key: str) -> Edge:
    parts = key.split("|")
    if len(parts) != 3 or not all(parts):
        raise ValueError(f"invalid edge key: {key}")
    return Edge(*parts)


def expected_edges(policy: dict) -> set[Edge]:
    expected = {edge_from_key(key) for key in policy["expected_edges"]}
    if len(expected) != len(policy["expected_edges"]):
        raise ValueError("duplicate expected edge")
    return expected


def grandfathered_edges(policy: dict) -> dict[Edge, dict]:
    grandfathered: dict[Edge, dict] = {}
    expected = expected_edges(policy)
    for item in policy["grandfathered_edges"]:
        edge = edge_from_key(item["edge"])
        if edge in grandfathered:
            raise ValueError(f"duplicate grandfathered edge: {edge.key}")
        if edge not in expected:
            raise ValueError(f"grandfathered edge is not expected: {edge.key}")
        for field in ("owner", "retirement_issue", "reason"):
            if not item.get(field):
                raise ValueError(f"{edge.key}: {field} is required")
        grandfathered[edge] = item
    return grandfathered


def authorized_forbidden_edges(policy: dict) -> dict[Edge, dict]:
    """Exact permanent boundary owners allowed to match a forbidden rule."""
    authorized: dict[Edge, dict] = {}
    expected = expected_edges(policy)
    for item in policy.get("authorized_forbidden_edges", []):
        edge = edge_from_key(item["edge"])
        if edge in authorized:
            raise ValueError(f"duplicate authorized forbidden edge: {edge.key}")
        if edge not in expected:
            raise ValueError(
                f"authorized forbidden edge is not expected: {edge.key}"
            )
        for field in ("owner", "reason"):
            if not item.get(field):
                raise ValueError(f"{edge.key}: {field} is required")
        authorized[edge] = item
    return authorized


def forbidden_rules_for(edge: Edge, policy: dict) -> list[str]:
    matches: list[str] = []
    for rule in policy["forbidden_rules"]:
        if any(
            fnmatch.fnmatchcase(edge.source, pattern)
            for pattern in rule["source_patterns"]
        ) and any(
            fnmatch.fnmatchcase(edge.header, pattern)
            for pattern in rule["header_patterns"]
        ):
            matches.append(rule["name"])
    return matches


def validate(policy: dict, observed: set[Edge]) -> list[str]:
    expected = expected_edges(policy)
    grandfathered = grandfathered_edges(policy)
    authorized = authorized_forbidden_edges(policy)
    errors: list[str] = []

    for edge in sorted(set(grandfathered) & set(authorized)):
        errors.append(
            f"edge cannot be both grandfathered and permanently authorized: {edge.key}"
        )

    rule_names = [rule["name"] for rule in policy["forbidden_rules"]]
    if len(set(rule_names)) != len(rule_names):
        errors.append("duplicate forbidden rule name")

    for edge in sorted(observed - expected):
        errors.append(f"unlisted restricted edge: {edge.key}")
    for edge in sorted(expected - observed):
        errors.append(f"stale policy edge: {edge.key}")
    for edge in sorted(expected):
        forbidden = forbidden_rules_for(edge, policy)
        if forbidden and edge not in grandfathered and edge not in authorized:
            errors.append(
                f"forbidden edge lacks ownership metadata ({','.join(forbidden)}): "
                f"{edge.key}"
            )
        if not forbidden and (edge in grandfathered or edge in authorized):
            errors.append(
                f"owned exception matches no forbidden rule: {edge.key}"
            )
    return errors


def print_edges(edges: Iterable[Edge]) -> None:
    for edge in sorted(edges):
        print(edge.key)


def raw_storage_api_errors(
    root: Path = ROOT, extra_sources: Iterable[Path] = ()
) -> list[str]:
    """Reject raw configuration persistence references outside its owner/ports."""
    candidates: set[Path] = set(extra_sources)
    for source_root in (root / "common", root / "idf_app" / "main",
                        root / "frame_app" / "main"):
        for suffix in ("*.c", "*.h"):
            candidates.update(source_root.rglob(suffix))

    errors: list[str] = []
    for path in sorted(candidates):
        relative = relative_path(path, root)
        if relative in RAW_STORAGE_ALLOWED:
            continue
        text = normalize_preprocessor_text(path.read_text(encoding="utf-8"))
        for match in RAW_STORAGE_API_RE.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            errors.append(
                f"raw storage API is owner-private: {relative}:{line}:"
                f"{match.group(0)[:-1]}"
            )
    return errors


def run_negative_fixture(policy: dict) -> None:
    target = policy["negative_fixture"]["target"]
    source = ROOT / policy["negative_fixture"]["source"]
    expected_header = policy["negative_fixture"]["header"]
    expected_fixture = Edge(
        target=target,
        source=relative_path(source),
        header=expected_header,
    )
    production_edges = collect_edges(policy)
    fixture_edges = collect_edges(policy, extra_sources={target: [source]})
    if expected_fixture not in fixture_edges:
        raise AssertionError(
            f"negative fixture resolved {sorted(edge.key for edge in fixture_edges)}, "
            f"expected {expected_fixture.key}"
        )

    errors = validate(policy, fixture_edges)
    expected_error = f"unlisted restricted edge: {expected_fixture.key}"
    if expected_error not in errors:
        raise AssertionError(
            "negative fixture was not rejected with the expected edge: "
            + "; ".join(errors)
        )

    raw_errors = raw_storage_api_errors(extra_sources=[source])
    expected_raw_prefix = (
        "raw storage API is owner-private: "
        f"{relative_path(source)}:"
    )
    if not any(error.startswith(expected_raw_prefix) for error in raw_errors):
        raise AssertionError(
            "negative fixture could bypass raw-storage ownership: "
            + "; ".join(raw_errors)
        )

    ownerless_policy = copy.deepcopy(policy)
    ownerless_policy["expected_edges"].append(expected_fixture.key)
    ownerless_errors = validate(ownerless_policy, production_edges | {expected_fixture})
    if not any(
        error.startswith("forbidden edge lacks ownership metadata")
        and error.endswith(expected_fixture.key)
        for error in ownerless_errors
    ):
        raise AssertionError(
            "negative fixture could be classified as allowed without ownership: "
            + "; ".join(ownerless_errors)
        )

    for fixture_path in policy["negative_fixture"]["unsupported_cmake"]:
        unsupported_cmake = ROOT / fixture_path
        try:
            parse_cmake_sources(unsupported_cmake)
        except ValueError:
            pass
        else:
            raise AssertionError(
                f"unsupported CMake source mutation was accepted: {fixture_path}"
            )
    print("controller dependency negative fixture passed")


def load_policy(path: Path) -> dict:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--policy", type=Path, default=DEFAULT_POLICY)
    parser.add_argument("--dump", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    try:
        policy = load_policy(args.policy)
        observed = collect_edges(policy)
        if args.dump:
            print_edges(observed)
            return 0
        if args.self_test:
            run_negative_fixture(policy)
            return 0

        errors = validate(policy, observed) + raw_storage_api_errors()
        if errors:
            for error in errors:
                print(error, file=sys.stderr)
            return 1
        grandfathered = len(grandfathered_edges(policy))
        authorized = len(authorized_forbidden_edges(policy))
        allowed = len(policy["expected_edges"]) - grandfathered - authorized
        print(
            "controller dependency policy passed: "
            f"{allowed} allowed, {authorized} boundary-owned, "
            f"{grandfathered} grandfathered"
        )
        return 0
    except (OSError, ValueError, KeyError, json.JSONDecodeError, AssertionError) as err:
        print(f"controller dependency policy error: {err}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
