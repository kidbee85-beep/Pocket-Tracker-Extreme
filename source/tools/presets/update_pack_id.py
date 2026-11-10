#!/usr/bin/env python3
"""Rebuild/check the preset pack ID used by the non-overwriting device seeder."""
from pathlib import Path
import argparse
import hashlib

ROOT = Path(__file__).resolve().parents[2]

def calculate():
    directory = ROOT / 'factory/Presets'
    digest = hashlib.sha256()
    count = 0
    for path in sorted(directory.rglob('*'), key=lambda p: p.relative_to(directory).as_posix()):
        if path.is_symlink():
            raise ValueError('Preset pack must not contain symlinks')
        if not path.is_file() or path.name.startswith('.'):
            continue
        digest.update(path.relative_to(directory).as_posix().encode('utf-8') + b'\0')
        digest.update(hashlib.sha256(path.read_bytes()).digest())
        count += 1
    return digest.hexdigest(), count

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    value, count = calculate()
    marker = ROOT / 'factory/presets-pack.id'
    if args.check:
        if marker.read_text().strip() != value:
            raise SystemExit('Preset pack ID is stale; run tools/presets/update_pack_id.py')
    else:
        marker.write_text(value + '\n')
    print(f'Verified {count} factory presets: {value}')

if __name__ == '__main__':
    main()
