#!/usr/bin/env python3
"""Check the local wire-v0 model against its immutable schema manifest."""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs" / "dev" / "surface_wire_v0_conformance_manifest.json"
MODEL = ROOT / "common" / "surface_client_protocol.h"
AFFECT = ROOT / "common" / "surface_projected_affect.h"


def struct_fields(source: str, typedef: str) -> list[str]:
    bodies = re.findall(
        r"typedef\s+struct\s*\{(?P<body>.*?)\}\s*(?P<name>\w+)\s*;",
        source,
        re.DOTALL,
    )
    match = next((body for body, name in bodies if name == typedef), None)
    if match is None:
        raise ValueError(f"missing {typedef}")
    body = re.sub(r"/\*.*?\*/", "", match, flags=re.DOTALL)
    fields: list[str] = []
    for statement in body.split(";"):
        statement = statement.strip()
        if not statement:
            continue
        field = re.search(r"([A-Za-z_]\w*)\s*(?:\[[^]]*\])?$", statement)
        if not field:
            raise ValueError(f"unparsed {typedef} field: {statement!r}")
        fields.append(field.group(1))
    return fields


def enum_values(source: str) -> list[str]:
    match = re.search(
        r"typedef\s+enum\s*\{(?P<body>.*?)\}\s*surface_control_kind_t\s*;",
        source,
        re.DOTALL,
    )
    if not match:
        raise ValueError("missing surface_control_kind_t")
    return re.findall(r"SURFACE_CONTROL_([A-Z_]+)\s*(?:=\s*[^,]+)?\s*,", match.group("body"))


def numeric_macro(source: str, name: str) -> int | None:
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+(\d+)[uU]?\s*$",
        source,
        re.MULTILINE,
    )
    return int(match.group(1)) if match else None


def effective_default(source: str, name: str, default_name: str) -> bool:
    return bool(re.search(
        rf"#ifndef\s+{re.escape(name)}\s*\n\s*#define\s+{re.escape(name)}\s+"
        rf"{re.escape(default_name)}\b",
        source,
    ))


def check_manifest(manifest: dict, model: str, affect: str) -> list[str]:
    errors: list[str] = []
    if manifest.get("schema") != "agent-surface-wire-v0-snapshot":
        errors.append("schema name drifted")
    if manifest.get("version") != 1 or not manifest.get("referenceCommit"):
        errors.append("version/provenance drifted")

    expected_enums = [value.upper().replace("-", "_") for value in manifest["controlKinds"]]
    if enum_values(model) != expected_enums:
        errors.append("control kind order drifted")

    for typedef, expected in manifest["modelFields"].items():
        if struct_fields(model if typedef != "surface_projected_affect_t" else affect,
                         typedef) != expected:
            errors.append(f"{typedef} field inventory drifted")

    bounds = manifest["bounds"]
    schema_maxima = bounds["schemaMaxima"]
    default_bounds = bounds["defaultTargetBounds"]
    schema_macros = {
        "SURFACE_WIRE_V0_SCHEMA_MAX_SECTIONS": schema_maxima["sections"],
        "SURFACE_WIRE_V0_SCHEMA_MAX_CONTROLS": schema_maxima["controlsPerSection"],
        "SURFACE_WIRE_V0_SCHEMA_MAX_OPTIONS": schema_maxima["optionsPerControl"],
    }
    default_macros = {
        "SURFACE_WIRE_V0_DEFAULT_MAX_SECTIONS": default_bounds["sections"],
        "SURFACE_WIRE_V0_DEFAULT_MAX_CONTROLS": default_bounds["controlsPerSection"],
        "SURFACE_WIRE_V0_DEFAULT_MAX_OPTIONS": default_bounds["optionsPerControl"],
    }
    for name, expected in {**schema_macros, **default_macros}.items():
        if numeric_macro(model, name) != expected:
            errors.append(f"{name} drifted")

    effective_macros = bounds["targetEffectiveMacros"]
    effective_defaults = {
        effective_macros["sections"]: "SURFACE_WIRE_V0_DEFAULT_MAX_SECTIONS",
        effective_macros["controlsPerSection"]: "SURFACE_WIRE_V0_DEFAULT_MAX_CONTROLS",
        effective_macros["optionsPerControl"]: "SURFACE_WIRE_V0_DEFAULT_MAX_OPTIONS",
    }
    for name, default_name in effective_defaults.items():
        if not effective_default(model, name, default_name):
            errors.append(f"{name} does not use its target default via #ifndef")

    string_macros = {
        "SURFACE_ID_LEN": bounds["stringLimits"]["idChars"],
        "SURFACE_LABEL_LEN": bounds["stringLimits"]["labelChars"],
        "SURFACE_CONTENT_LEN": bounds["stringLimits"]["contentChars"],
        "SURFACE_FALLBACK_LEN": bounds["stringLimits"]["fallbackChars"],
        "SURFACE_ACTIONREF_LEN": bounds["stringLimits"]["actionRefChars"],
        "SURFACE_ICON_LEN": bounds["stringLimits"]["iconChars"],
    }
    for name, expected in string_macros.items():
        if numeric_macro(model, name) != expected:
            errors.append(f"{name} drifted")

    affect_fields = set(manifest["modelFields"]["surface_projected_affect_t"])
    for bound_name in manifest["projectedAffect"]:
        if bound_name not in affect_fields:
            errors.append(f"missing affect field {bound_name}")

    if manifest.get("mediaResourceField") is not None:
        errors.append("wire-v0 media/resource field is no longer absent")
    actual_fields = set(sum((fields for fields in manifest["modelFields"].values()), []))
    if any(re.search(r"media|resource|artwork|url", field, re.IGNORECASE)
           for field in actual_fields):
        errors.append("media/resource escape field entered the model")
    return errors


def check_target_overrides(host_sizes: dict[str, int]) -> None:
    probe = """
#include "surface_client_protocol.h"
#include "surface_projection_snapshot.h"
_Static_assert(SURFACE_MAX_SECTIONS > 0, "section bound");
_Static_assert(SURFACE_MAX_CONTROLS > 0, "control bound");
_Static_assert(SURFACE_MAX_OPTIONS > 0, "option bound");
#ifdef SURFACE_EXPECT_DEFAULT_HOST
_Static_assert(sizeof(surface_projection_t) == 66264,
               "default projection size");
_Static_assert(sizeof(surface_projection_snapshot_t) == 66288,
               "default snapshot size");
#endif
int main(void) { return (int)(sizeof(surface_projection_t) +
                             sizeof(surface_projection_snapshot_t) == 0); }
"""
    probe = probe.replace("66264", str(host_sizes["surface_projection_t"]))
    probe = probe.replace(
        "66288", str(host_sizes["surface_projection_snapshot_t"])
    )
    with tempfile.TemporaryDirectory(prefix="surface-wire-v0-") as directory:
        source_path = Path(directory) / "bounds_probe.c"
        source_path.write_text(probe, encoding="utf-8")

        def compile_probe(
            definitions: dict[str, int], expect_default_host: bool = False
        ) -> bool:
            output = Path(directory) / (
                "probe-" + str(len(list(Path(directory).iterdir())))
            )
            command = [
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-I", str(ROOT / "common"),
            ]
            command.extend(
                f"-D{name}={value}" for name, value in definitions.items()
            )
            if expect_default_host:
                command.append("-DSURFACE_EXPECT_DEFAULT_HOST=1")
            command.extend(["-x", "c", "-c", str(source_path), "-o", str(output)])
            return subprocess.run(
                command,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            ).returncode == 0

        if not compile_probe({}, expect_default_host=True):
            raise SystemExit("wire-v0 default host size check failed")
        if not compile_probe({
            "SURFACE_MAX_SECTIONS": 2,
            "SURFACE_MAX_CONTROLS": 3,
            "SURFACE_MAX_OPTIONS": 2,
        }):
            raise SystemExit("wire-v0 target override compile check failed")
        for name, value in (
            ("SURFACE_MAX_SECTIONS", 0),
            ("SURFACE_MAX_CONTROLS", 9),
            ("SURFACE_MAX_OPTIONS", 0),
            ("SURFACE_MAX_SECTIONS", 7),
            ("SURFACE_MAX_CONTROLS", 0),
            ("SURFACE_MAX_OPTIONS", 7),
        ):
            if compile_probe({name: value}):
                raise SystemExit(
                    f"wire-v0 invalid target override unexpectedly compiled: {name}={value}"
                )


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    model = MODEL.read_text(encoding="utf-8")
    affect = AFFECT.read_text(encoding="utf-8")
    errors = check_manifest(manifest, model, affect)
    if "--negative-self-test" in sys.argv:
        mutated = model.replace("SURFACE_CONTROL_MOMENTARY", "SURFACE_CONTROL_MUTATED", 1)
        if not check_manifest(manifest, mutated, affect):
            errors.append("negative mutation did not fail closed")
        else:
            print("wire-v0 manifest negative mutation rejected")
    if "--check-overrides" in sys.argv:
        check_target_overrides(manifest["hostDefaultSizes"])
        print("wire-v0 target override compile checks passed")
    if errors:
        raise SystemExit("wire-v0 local model mismatch: " + "; ".join(errors))
    print("wire-v0 snapshot v1 local model/manifest conformance passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
