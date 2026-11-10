#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package the verified, portable factory parameter library (no audio samples)."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import stat
import zipfile

from package_brick import verify_factory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    pack_id = verify_factory(source)
    version = re.search(r'versionName\s*=\s*"([^"]+)"', (source / 'app/build.gradle.kts').read_text()).group(1)
    paths = sorted(p for p in (source / 'factory/Presets').rglob('*') if p.is_file() and not p.name.startswith('.'))
    counts = Counter(p.suffix for p in paths)
    if counts != {'.pti': 2048, '.ptr': 64, '.ptd': 64, '.ptfx': 96, '.ptmaster': 32}:
        raise ValueError('Unexpected preset type counts: ' + str(dict(counts)))
    items = {p.relative_to(source / 'factory').as_posix(): p.read_bytes() for p in paths}
    manifest = {'version': version, 'factory_pack_id': pack_id, 'preset_files': len(paths),
                'counts': dict(sorted(counts.items())), 'contains_audio_samples': False,
                'files': [{'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                          for name, data in sorted(items.items())]}
    items['Pocket-Tracker-Extreme-Factory-Presets-README.txt'] = ('''POCKET TRACKER EXTREME — OPTIONAL FACTORY PRESETS
Version ''' + version + ''' · Kid Bunka
2,304 original parameter presets; no third-party audio samples.
2,048 instruments + 64 Reverb + 64 Delay + 96 Studio FX + 32 Master.

Extract only the Presets folder into the tracker media home you selected.
Preserve the folders inside it; keep existing edited files when asked to replace.
The 256 new effect/master files have their own Extreme_Collection subfolders.

ANDROID
Choose the media folder using ADD FOLDER. For instruments, open INSTRUMENT >
LOAD > Instrument Presets (.pti). Send effects: EFFECTS > REVERB/DELAY > LOAD.
Studio: R+Select, then FX Rack > FX LIBRARY or Master > MASTER LIBRARY.
The new settings are also built into the native preset selectors in beta.3;
this optional ZIP adds categorized file browsing without first-launch copying.

BRICK / LINUX
The complete application ZIP already includes this factory library. The
Program-Factory-Update kit adds missing immutable factory files and can prepare
the writable library with its computer-side helper. Do not replace the whole
data folder. The launcher copies only missing data/Presets files on first run
after a pack change; existing custom presets and edited files remain intact.

See Effect-Collection-Guide.txt for controls and category descriptions.
GPL-3.0-or-later; full source and notices accompany the matching app release.
https://github.com/kidbee85-beep/Pocket-Tracker-Extreme
''').encode('utf-8')
    items['Pocket-Tracker-Extreme-Factory-Presets-LICENSE.txt'] = (source / 'LICENSE').read_bytes()
    for name in ['Effect-Collection-Guide.txt', 'Effect-Collection-Catalog.txt', 'effect-catalog.json', 'presets-pack.id']:
        items[name] = (source / 'factory' / name).read_bytes()
    items['factory-manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode('utf-8')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.out.with_name(args.out.name + '.tmp')
    try:
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name, data in sorted(items.items()):
                info = zipfile.ZipInfo(name, date_time=(2026, 11, 9, 0, 0, 0))
                info.create_system = 3
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (stat.S_IFREG | 0o644) << 16
                archive.writestr(info, data)
        with zipfile.ZipFile(temporary) as archive:
            if archive.testzip() is not None or len(archive.namelist()) != len(items):
                raise ValueError('Factory ZIP integrity check failed')
            for entry in manifest['files']:
                if hashlib.sha256(archive.read(entry['path'])).hexdigest() != entry['sha256']:
                    raise ValueError('Archived preset checksum failed: ' + entry['path'])
        temporary.replace(args.out)
    finally:
        temporary.unlink(missing_ok=True)
    print(json.dumps({'file': args.out.name, 'bytes': args.out.stat().st_size,
                      'sha256': hashlib.sha256(args.out.read_bytes()).hexdigest(),
                      'version': version, 'presets': len(paths), 'factory_pack_id': pack_id}, indent=2))


if __name__ == '__main__':
    main()
