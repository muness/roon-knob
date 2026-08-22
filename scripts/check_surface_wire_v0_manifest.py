#!/usr/bin/env python3
"""Check the local wire-v0 model against its immutable schema manifest."""

from __future__ import annotations

import json
import re
import sys
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
    macros = {
        "SURFACE_MAX_SECTIONS": bounds["sections"],
        "SURFACE_MAX_CONTROLS": bounds["controlsPerSection"],
        "SURFACE_MAX_OPTIONS": bounds["optionsPerControl"],
        "SURFACE_ID_LEN": bounds["idChars"],
        "SURFACE_LABEL_LEN": bounds["labelChars"],
        "SURFACE_CONTENT_LEN": bounds["contentChars"],
        "SURFACE_ACTIONREF_LEN": bounds["actionRefChars"],
    }
    for name, expected in macros.items():
        match = re.search(rf"#define\s+{name}\s+(\d+)", model)
        if not match or int(match.group(1)) != expected:
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
    if errors:
        raise SystemExit("wire-v0 local model mismatch: " + "; ".join(errors))
    print("wire-v0 snapshot v1 local model/manifest conformance passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
