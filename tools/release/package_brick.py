#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create a full Brick ZIP or a single-program update from matching build outputs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import stat
import tarfile
import tempfile
import zipfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy(source, target, executable=False):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    target.chmod(0o755 if executable else 0o644)


def elf(path):
    with path.open('rb') as f:
        header = f.read(64)
    if header[:6] != b'\x7fELF\x02\x01' or int.from_bytes(header[18:20], 'little') != 183:
        raise ValueError('Expected an ARM64 little-endian ELF: ' + path.name)


def licenses(source, target):
    for p in (source / 'docs/licenses').iterdir():
        if p.is_file():
            copy(p, target / 'project' / p.name)
    for name in ['LICENSE', 'CREDITS.md']:
        copy(source / name, target / name)
    for base, name in [('native/vendor', 'vendor'), ('native/effects', 'effects'),
                       ('external/iphone-usb/vendor', 'iphone-usb'),
                       ('external/iphone-pair/vendor', 'iphone-pair'),
                       ('external/apple-usb-deps', 'apple-usb')]:
        for p in (source / base).rglob('*'):
            if p.is_file() and p.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE')):
                copy(p, target / name / p.relative_to(source / base))
    for name in ['yt-dlp-LICENSE', 'yt-dlp-THIRD_PARTY_LICENSES.txt', 'CA-bundle-MPL-2.0.txt', 'helper-provenance.json']:
        copy(source / 'external' / name, target / name)
    with tarfile.open(source / 'external/quickjs-2026-06-04.tar.xz') as archive:
        member = next(x for x in archive.getmembers() if x.name.endswith('/LICENSE'))
        (target / 'QuickJS-LICENSE').write_bytes(archive.extractfile(member).read())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--helpers', type=Path, help='Full-package inputs: yt-dlp, qjs and three iPhone helpers')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--program-update', action='store_true')
    parser.add_argument('--allow-modified-helpers', action='store_true', help='For forks: permit explicitly rebuilt helper hashes')
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    version = re.search(r'versionName\s*=\s*"([^"]+)"', (source/'app/build.gradle.kts').read_text()).group(1)
    binary = args.binary.resolve(); elf(binary)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pte-package-', dir=args.out.parent) as temp:
        stage = Path(temp)
        if args.program_update:
            copy(binary, stage/'pockettracker_extreme.aarch64', True)
            copy(source/'tools/release/update_brick.py',stage/'update_brick.py')
            manifest = {'platform':'trimui-brick','program':'pockettracker_extreme.aarch64',
                        'sha256':sha(binary),'bytes':binary.stat().st_size,'version':version}
            (stage/'update-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
            copy(source/'docs/brick/UPDATE-EXISTING-INSTALL.txt',stage/'UPDATE-EXISTING-INSTALL.txt')
            licenses(source,stage/'licenses')
        else:
            if args.helpers is None:
                parser.error('--helpers is required for the complete feature package')
            app = stage/'pockettracker_extreme'
            copy(binary,app/'pockettracker_extreme.aarch64',True)
            copy(source/'shell/brick/Pocket Tracker Extreme.sh',stage/'Pocket Tracker Extreme.sh',True)
            for name in ['port.json','gameinfo.xml','screenshot.png']:
                copy(source/'shell/brick'/name,app/name)
            for p in (source/'app/src/main/assets/fonts').glob('*.ttf'):
                copy(p,app/'fonts'/p.name)
            shutil.copytree(source/'factory',app/'factory')
            provenance=json.loads((source/'external/helper-provenance.json').read_text())
            expected={'yt-dlp':provenance['yt_dlp']['sha256'],'qjs':provenance['quickjs']['binary_sha256']}
            expected.update({n:v['sha256'] for n,v in provenance['iphone_helpers'].items()})
            actual={}
            for name,h in expected.items():
                p=args.helpers/name;elf(p);actual[name]=sha(p)
                if actual[name]!=h and not args.allow_modified_helpers:
                    raise ValueError('Helper hash differs from pinned provenance: '+name)
                copy(p,app/'tools'/name,True)
            copy(source/'external/cacert.pem',app/'tools/cacert.pem')
            copy(source/'tools/release/prepare_presets.py',app/'tools/prepare_presets.py')
            for name in ['HOW-TO-FIRST-START.txt','UPDATE-EXISTING-INSTALL.txt']:
                copy(source/'docs/brick'/name,stage/name)
                copy(source/'docs/brick'/name,app/name)
            for name in ['KNULLI-Official-Download.url','KNULLI-SHA256.txt']:
                copy(source/'docs/brick'/name,stage/name)
            copy(source/'docs/brick/HOW-TO-FIRST-START.txt',app/'README.txt')
            copy(source/'docs/quick-start-guide.md',app/'QUICK-START.md')
            copy(source/'docs/HELPER-SOURCES.md',app/'HELPER-SOURCES.md')
            (app/'VERSION.txt').write_text('Pocket Tracker Extreme '+version+'\n')
            licenses(source,app/'licenses')
            (app/'licenses/packaged-helper-hashes.json').write_text(json.dumps(actual,indent=2)+'\n')
        (stage/'SOURCE.txt').write_text('Matching source and build instructions:\nhttps://github.com/kidbee85-beep/Pocket-Tracker-Extreme/releases/download/v'+version+'/Pocket-Tracker-Extreme-'+version+'-Source.zip\n')
        with zipfile.ZipFile(args.out,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
            for p in sorted(stage.rglob('*')):
                if not p.is_file():
                    continue
                info=zipfile.ZipInfo(p.relative_to(stage).as_posix(),date_time=(2026,11,9,0,0,0))
                info.create_system=3;info.compress_type=zipfile.ZIP_DEFLATED
                info.external_attr=(stat.S_IFREG | (0o755 if p.stat().st_mode & 0o111 else 0o644))<<16
                archive.writestr(info,p.read_bytes())
        with zipfile.ZipFile(args.out) as archive:
            if archive.testzip() is not None:
                raise ValueError('ZIP integrity check failed')
    print(json.dumps({'file':args.out.name,'bytes':args.out.stat().st_size,'sha256':sha(args.out),'version':version,'program_sha256':sha(binary)},indent=2))


if __name__=='__main__':
    main()
