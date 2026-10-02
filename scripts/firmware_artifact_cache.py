#!/usr/bin/env python3
"""Exact-input cache for PR firmware archives. Release tags always build fresh."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

PROJECTS = {
    'dial': {'idf_app'}, 'frame': {'frame_app'},
    'rlcd': {'rlcd_app', 'frame_app'},
    'atom': {'atom_app', 'tough_app'}, 'tough': {'tough_app'},
    'm5': {'m5_beta_app', 'tough_app'}, 'aux': {'knob_aux_app'},
}
ALL_PROJECTS = set().union(*PROJECTS.values())

def relevant(path, target):
    root = path.split('/')[0]
    if root in ALL_PROJECTS:
        return root in PROJECTS[target]
    if root in {'docs', 'web'} or path == 'README.md':
        return False
    if root == 'tests':
        return path.startswith('tests/fixtures/')
    if path == 'scripts/test_connection_runtime.sh':
        return False
    # Include unknown inputs, workflow/toolchain, scripts, shared code, and
    # components conservatively. An omitted new dependency must not look safe.
    return True

def fingerprint(entries, target, variant):
    selected = sorted((p, oid) for p, oid in entries if relevant(p, target))
    return hashlib.sha256(json.dumps([1, target, variant, selected]).encode()).hexdigest()

def main():
    p = argparse.ArgumentParser()
    p.add_argument('mode', choices=['key', 'stage', 'verify'])
    p.add_argument('--target', required=True, choices=PROJECTS)
    p.add_argument('--variant', default='')
    p.add_argument('--directory', required=True)
    args = p.parse_args()
    raw = subprocess.check_output(['git', 'ls-tree', '-rz', 'HEAD']).decode()
    entries = [(item.split('\t',1)[1], item.split('\t',1)[0]) for item in raw.split('\0') if item]
    key = fingerprint(entries, args.target, args.variant)
    directory = Path(args.directory)
    metadata = directory / ('provenance-' + args.target + (('-' + args.variant) if args.variant else '') + '.json')
    if args.mode == 'key':
        with open(os.environ['GITHUB_OUTPUT'], 'a') as f:
            f.write('fingerprint=' + key + '\n')
        return
    if args.mode == 'stage':
        if directory.exists(): shutil.rmtree(directory)
        directory.mkdir(parents=True, exist_ok=True)
        files = {}
        for name in os.environ['FIRMWARE_FILES'].splitlines():
            if not name.strip(): continue
            source = Path(name.strip())
            if not source.is_file(): raise SystemExit('Missing artifact: ' + str(source))
            dest = directory / source
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, dest)
            files[str(source)] = hashlib.sha256(source.read_bytes()).hexdigest()
        metadata.write_text(json.dumps({'fingerprint': key, 'build_sha': os.environ['GITHUB_SHA'], 'run_id': os.environ['GITHUB_RUN_ID'], 'files': files}, indent=2)+'\n')
    data = json.loads(metadata.read_text())
    if data['fingerprint'] != key or not data['files']:
        raise SystemExit('Artifact provenance mismatch')
    expected = set(data['files']) | {metadata.name}
    actual = {str(p.relative_to(directory)) for p in directory.rglob('*') if p.is_file()}
    if actual != expected or any(p.is_symlink() for p in directory.rglob('*')):
        raise SystemExit('Unexpected artifact inventory')
    for name, digest in data['files'].items():
        path = Path(name)
        if path.is_absolute() or '..' in path.parts:
            raise SystemExit('Unsafe artifact path')
        if hashlib.sha256((directory/path).read_bytes()).hexdigest() != digest:
            raise SystemExit('Artifact digest mismatch: ' + name)
    with open(os.environ.get('GITHUB_STEP_SUMMARY', os.devnull), 'a') as f:
        f.write(f"### Firmware {args.target} {args.variant}\nOriginal build SHA: `{data['build_sha']}` · run `{data['run_id']}`\n\n")

if __name__ == '__main__': main()
