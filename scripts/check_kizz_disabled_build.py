#!/usr/bin/env python3
"""Prove the default M5 build contains no experimental wake/voice dependencies."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shlex

FORBIDDEN = ('kizz_wake_word', 'micro_wake_word', 'espressif__esp-sr', 'espressif__esp-nn',
             'espressif__esp_sr', 'espressif__esp_nn', 'ESP_SR.cpp', 'esp32-hal-sr.c',
             'kizz_detector_aot', 'kizz_verifier_aot', 'kizz_control_detector_tflite',
             'kizz_control_compact_verifier', 'kizz_control_ordered_verifier')
GATED_SOURCES = ('m5_platform.cpp', 'touch_ui.cpp', 'captive_portal.c')


def check_build(directory: Path, project: str) -> None:
    commands = json.loads((directory / 'compile_commands.json').read_text())
    gated_commands = {source: [] for source in GATED_SOURCES}
    for entry in commands:
        serialized = json.dumps(entry)
        for forbidden in FORBIDDEN:
            if forbidden in serialized:
                raise ValueError(f'disabled wake dependency in compiler input: {forbidden}')
        source = Path(entry['file']).name
        if source in gated_commands:
            arguments = entry.get('arguments') or shlex.split(entry['command'])
            gated_commands[source].append(arguments)
    for source, source_commands in gated_commands.items():
        if not source_commands or any('-DHIPHI_KIZZ_WAKE_WORD=0' not in command or
                                      '-DHIPHI_KIZZ_WAKE_WORD=1' in command for command in source_commands):
            raise ValueError(f'{source} must be explicitly compiled with HIPHI_KIZZ_WAKE_WORD=0')
    link_map = (directory / f'{project}.map').read_text()
    for forbidden in FORBIDDEN:
        if forbidden in link_map:
            raise ValueError(f'disabled wake dependency or model in linked image: {forbidden}')
    print(f'{project}: wake/voice compile gate is OFF; runtime, AOT, models and ESP-SR/ESP-NN absent')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build_dir', type=Path)
    parser.add_argument('--project', required=True)
    args = parser.parse_args()
    try:
        check_build(args.build_dir, args.project)
    except (ValueError, OSError, KeyError) as error:
        raise SystemExit(str(error))
