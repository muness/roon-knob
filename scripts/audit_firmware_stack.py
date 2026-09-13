#!/usr/bin/env python3
"""Audit Xtensa ELF frames and direct-call chains; this is not a stack safety proof."""
import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path


def parse(text):
    functions = {}
    current = None
    for line in text.splitlines():
        label = re.match(r'^([0-9a-f]+) <(.+)>:$', line)
        if label:
            current = label[2]
            current_address = int(label[1], 16)
            functions[current] = {'frame_bytes': None, 'calls': [], 'indirect': False}
        elif current:
            f = functions[current]
            frame = re.search(r'\bentry\s+a1,\s*(0x[0-9a-f]+|\d+)', line)
            instruction = re.match(r"^\s*([0-9a-f]+):", line)
            if frame and instruction and int(instruction[1], 16) == current_address:
                f['frame_bytes'] = int(frame[1], 0)
            call = re.search(r'\bcall(?:0|4|8|12)\s+[^<]+<([^>]+)>', line)
            if call:
                f['calls'].append(call[1].split('+0x')[0])
            if re.search(r'\bcallx(?:0|4|8|12)\b', line):
                f['indirect'] = True
    return functions


def report(functions):
    # Limit traversal explicitly: indirect calls, missing symbols, cycles and
    # depth truncation remain visible. Values are known-path estimates only.
    visits = [0]
    def chain(name, seen):
        visits[0] += 1
        if visits[0] > 5000:
            return 0, [], ["analysis visit limit"]
        if name in seen:
            return 0, [], ['recursion']
        if len(seen) >= 32:
            return 0, [], ['depth limit']
        f = functions.get(name)
        if f is None:
            return 0, [], ['missing callee']
        gaps = ['unknown frame'] if f['frame_bytes'] is None else []
        if f['indirect']:
            gaps.append('indirect call')
        best = (0, [])
        for child in set(f['calls']):
            size, path, child_gaps = chain(child, seen | {name})
            gaps.extend(child_gaps)
            if size > best[0]:
                best = size, path
        return (f['frame_bytes'] or 0) + best[0], [name] + best[1], sorted(set(gaps))
    rows = []
    for name, f in functions.items():
        rows.append(dict(name=name, **f))
    # Entry points of interest; every function frame is still reported below.
    roots = [n for n in functions if n.endswith('_handler') or n.endswith('_thread') or n.endswith('_task') or n == 'bridge_client_execute_command']
    chains = []
    for name in roots:
        visits[0] = 0
        size, path, gaps = chain(name, set())
        chains.append(dict(root=name, known_chain_bytes=size, path=path, gaps=gaps))
    return {'limitations': 'Xtensa entry frames only. Direct calls only; no interrupt, dynamic allocation, library callback or task-budget proof. Gaps are not zero-cost guarantees.',
            'frames': sorted(rows, key=lambda r: r['frame_bytes'] or 0, reverse=True),
            'entry_paths': sorted(chains, key=lambda r: r['known_chain_bytes'], reverse=True)}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('elf')
    p.add_argument('--output', required=True)
    p.add_argument('--enforce-dial', action='store_true')
    args = p.parse_args()
    tool = shutil.which('xtensa-esp-elf-objdump') or shutil.which('xtensa-esp32s3-elf-objdump') or shutil.which('xtensa-esp32-elf-objdump')
    if not tool:
        raise SystemExit('Xtensa objdump required')
    data = report(parse(subprocess.check_output([tool, '-d', args.elf], text=True)))
    Path(args.output).write_text(json.dumps(data, indent=2) + '\n')
    for row in data['frames'][:12]:
        print(f"{row['frame_bytes']!s:>6} bytes  {row['name']}")
    budgets = {'config_get_handler': 2048, 'root_get_handler': 2048, 'bridge_client_execute_command': 2048}
    violations = [r for r in data['frames'] if r['name'] in budgets and (r['frame_bytes'] is None or r['frame_bytes'] > budgets[r['name']])]
    if args.enforce_dial and violations:
        raise SystemExit('Stack budget exceeded: ' + ', '.join(r['name'] for r in violations))

if __name__ == '__main__':
    main()
