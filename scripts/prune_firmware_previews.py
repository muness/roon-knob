#!/usr/bin/env python3
"""Bound PR previews without touching stable, beta, alpha or other site roots."""
import argparse
import json
from pathlib import Path
import re
import shutil

BUILD = re.compile(r'([0-9a-f]{40})-(\d+)-(\d+)')

def prune(root, open_prs, apply=False):
    removed = []
    pr_root = root / 'pr'
    if not pr_root.exists(): return removed
    for folder in pr_root.iterdir():
        if not folder.is_dir() or not folder.name.isdigit(): continue
        if int(folder.name) not in open_prs:
            removed.append((folder, sum(p.stat().st_size for p in folder.rglob('*') if p.is_file())))
            if apply: shutil.rmtree(folder)
            continue
        builds = [(int(m[2]), int(m[3]), m[0]) for p in folder.iterdir() if p.is_file() and (m := BUILD.search(p.name))]
        if not builds: continue  # Legacy-only preview: keep until a replacement exists.
        latest = max(builds)[2]
        for path in folder.iterdir():
            if not path.is_file(): continue  # Shared assets and lightweight /site stay.
            match = BUILD.search(path.name)
            if match and match[0] != latest:
                removed.append((path, path.stat().st_size))
                if apply: path.unlink()
    return removed

def main():
    p = argparse.ArgumentParser()
    p.add_argument('root', type=Path)
    p.add_argument('--open-prs', required=True, type=Path)
    p.add_argument('--apply', action='store_true')
    args = p.parse_args()
    numbers = set(json.loads(args.open_prs.read_text()))
    removed = prune(args.root, numbers, args.apply)
    print(f"{'Removed' if args.apply else 'Would remove'} {len(removed)} entries, {sum(n for _,n in removed)/1e6:.1f} MB")

if __name__ == '__main__': main()
