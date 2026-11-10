#!/usr/bin/env python3
"""Portable, binary-preserving TrackWerk release text refresh (Python 3.9+).

Unchanged ZIP local records are copied verbatim. Changed text is ZIP_STORED,
so the output does not depend on the host's zlib version. No device installs.
"""
import argparse, copy, hashlib, json, re, shutil, stat, zipfile
from pathlib import Path

BASELINES = {
    'TrackWerk-0.9-Miyoo-Mini-Onion.zip': 'e90ee4a24842be642a7a6526f12d29b9aa5d4ce5cb35a25db3fb8c37a78a86e5',
    'TrackWerk-0.9-Miyoo-Plus-Onion.zip': '5e868d794ee3d7d570a6bb643e2a988ac7dc4239c59119c56e5e252403923e38',
    'TrackWerk-0.9-Brick-KNULLI.zip': '20b7d5af274be8a6bfdecb3b0fc470844e85d72d266ecd716b07f0a2ec83a1c2',
    'TrackWerk-0.9-Brick-Program-Factory-Update.zip': 'b23aa771d77c052db91ba296cae8a5318ae433e3d99cde4f9a190a2c3eb8dfd9',
    'TrackWerk-0.9-Factory-Presets.zip': '36ec474f3210454038270011a0a4bf23c351b16066c339b1946cae7c8c226e19',
    'TrackWerk-0.9-Source.zip': '0c7ec1615de39ef5d33540502c8233b4f320e753274fbf2347ec55f57aa52249',
}
PREFIX = 'TrackWerk-0.9/'
SOURCE_NAME = 'TrackWerk-0.9-Source.zip'
OLD_REPO = 'https://github.com/kidbee85-beep/' + 'Pocket' + '-Tracker-Extreme'
NEW_REPO = 'https://github.com/kidbee85-beep/TrackWerk'
OLD_SITE = 'https://kidbee85-beep.github.io/' + 'Pocket' + '-Tracker-Extreme'
NEW_SITE = 'https://kidbee85-beep.github.io/TrackWerk'
SOURCE_URL = NEW_REPO + '/releases/download/v0.9.0-beta/TrackWerk-0.9-Source.zip'
TEXT_EXT = {'.txt', '.md', '.sh', '.json', '.desktop', '.html', '.xml', '.svg',
            '.kt', '.kts', '.cpp', '.h', '.hpp', '.c', '.py', '.yml', '.yaml', '.cmake', '.gradle', '.ps1'}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def dump(value):
    return (json.dumps(value, indent=2, ensure_ascii=False) + '\n').encode()

def legal(name):
    n = name.lower()
    return '/licenses/' in n or n.startswith('licenses/') or Path(n).name in {'credits.md', 'license', 'license.txt', 'copying', 'copying.txt'}

def patch_text(name, data):
    if legal(name) or Path(name).suffix.lower() not in TEXT_EXT:
        return data
    try:
        text = data.decode('utf-8')
    except UnicodeDecodeError:
        return data
    text = text.replace(OLD_REPO, NEW_REPO).replace(OLD_SITE, NEW_SITE)
    # Preserve real legacy launcher paths and historical Android migration wording.
    protected = ['Pocket Tracker Extreme.sh', 'earlier Pocket Tracker Extreme app']
    for i, value in enumerate(protected):
        text = text.replace(value, '__TRACKWERK_COMPAT_%d__' % i)
    text = text.replace('Pocket Tracker Extreme', 'TrackWerk')
    text = text.replace('POCKET TRACKER EXTREME', 'TRACKWERK')
    text = text.replace('Pocket-Tracker-Extreme', 'TrackWerk')
    for i, value in enumerate(protected):
        text = text.replace('__TRACKWERK_COMPAT_%d__' % i, value)
    text = text.replace('TrackWerk0.2.0-beta.2', 'TrackWerk 0.9.0')
    text = text.replace('TrackWerk0.2.0-beta.1', 'TrackWerk 0.9.0')
    text = text.replace('TrackWerk 0.2.0-beta.2-Brick', 'TrackWerk 0.9.0-Brick')
    text = text.replace('TrackWerk 0.2.0-beta.2-R36S', 'TrackWerk 0.9.0-R36S')
    text = text.replace('TRACKWERK — PUBLIC BETA 0.2.0-beta.2', 'TRACKWERK — PUBLIC BETA 0.9.0')
    text = re.sub(r'TrackWerk-0\.2\.0-beta\.[123]-Brick\.zip', 'TrackWerk-0.9-Brick-KNULLI.zip', text)
    if name.endswith('tools/release/package_brick.py'):
        lines = text.splitlines(keepends=True)
        for i, line in enumerate(lines):
            if "(stage/'SOURCE.txt').write_text(" in line:
                indent = line[:len(line) - len(line.lstrip())]
                lines[i] = indent + "(stage/'SOURCE.txt').write_text('Matching source and build instructions:\\n" + SOURCE_URL + "\\n')\n"
        text = ''.join(lines)
    if name.endswith('BUILD-TRACKWERK-0.9.txt'):
        text = text.replace('The five identity/artwork source changes are recorded in',
                            'The identity/artwork and distribution-text changes are recorded in')
    return text.encode()

def load_archive(path):
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None, path.name
        assert len(z.namelist()) == len(set(z.namelist())), 'Duplicate archive entry'
        return {i.filename: z.read(i) for i in z.infolist()}

def metadata(info):
    return {k: getattr(info, k) for k in ['filename', 'date_time', 'comment', 'extra', 'create_system',
            'create_version', 'extract_version', 'reserved', 'volume', 'internal_attr', 'external_attr']}

def rewrite_archive(source, output, rows):
    """Preserve unchanged compressed data and original entry attributes exactly."""
    changed = []
    with zipfile.ZipFile(source) as old, source.open('rb') as raw, zipfile.ZipFile(output, 'w') as new:
        new.comment = old.comment
        infos = old.infolist()
        offsets = sorted(i.header_offset for i in infos)
        ends = {offset: (offsets[n + 1] if n + 1 < len(offsets) else old.start_dir)
                for n, offset in enumerate(offsets)}
        assert not any(i.file_size >= 2**32 or i.compress_size >= 2**32 for i in infos)
        assert set(i.filename for i in infos) <= set(rows), 'No entry removal permitted'
        for original in infos:
            name = original.filename
            info = copy.copy(original)
            data = rows[name]
            if data == old.read(original):
                raw.seek(original.header_offset)
                chunk = raw.read(ends[original.header_offset] - original.header_offset)
                new.fp.seek(new.start_dir)
                info.header_offset = new.fp.tell()
                new.fp.write(chunk)
                new.start_dir = new.fp.tell()
                new.filelist.append(info)
                new.NameToInfo[name] = info
                new._didModify = True
            else:
                changed.append(name)
                new.writestr(info, data, compress_type=zipfile.ZIP_STORED)
        for name in sorted(set(rows) - set(old.namelist())):
            changed.append(name)
            info = zipfile.ZipInfo(name, (2026, 11, 10, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | (0o755 if name.endswith('.py') else 0o644)) << 16
            new.writestr(info, rows[name], compress_type=zipfile.ZIP_STORED)
    with zipfile.ZipFile(source) as old, zipfile.ZipFile(output) as new:
        assert new.testzip() is None, 'CRC failure: ' + output.name
        assert set(new.namelist()) == set(rows)
        for name, data in rows.items():
            assert new.read(name) == data, name
            if name in old.namelist():
                assert metadata(new.getinfo(name)) == metadata(old.getinfo(name)), name
        # ELF, image, preset, sample and legal bytes must all remain untouched.
        for i in old.infolist():
            b = old.read(i)
            if b.startswith(b'\x7fELF') or legal(i.filename) or Path(i.filename).suffix.lower() in {'.png', '.jpg', '.wav', '.pti', '.ptd', '.ttf'}:
                assert new.read(i.filename) == b, 'Protected payload changed: ' + i.filename
    return {'file': output.name, 'bytes': output.stat().st_size, 'sha256': sha(output.read_bytes()),
            'entries': len(rows), 'changed_entries': changed, 'all_entries_read_back': True,
            'zip_crc': 'PASS', 'entry_attributes_preserved': True, 'ELF_presets_samples_art_legal_unchanged': True}

def source_rows(rows):
    rows = {n: patch_text(n, b) for n, b in rows.items()}
    key = PREFIX + 'PUBLIC-PRODUCTION-SOURCE-FREEZE.json'
    freeze = json.loads(rows[key])
    previous = freeze['manifest_sha256']
    assert sha(json.dumps(freeze['entries'], sort_keys=True, separators=(',', ':')).encode()) == previous
    changed = []
    distribution = {r['path']: r for r in freeze['distribution_changes']}
    for item in freeze['entries']:
        data = rows[PREFIX + 'source/' + item['path']]
        digest = sha(data)
        if digest != item['sha256']:
            assert not item['path'].startswith(('app/', 'native/', 'vendor/')), item['path']
            changed.append(item['path'])
            distribution[item['path']] = {'path': item['path'], 'before_sha256': item['sha256'], 'after_sha256': digest}
            item.update(bytes=len(data), sha256=digest)
    freeze['manifest_sha256'] = sha(json.dumps(freeze['entries'], sort_keys=True, separators=(',', ':')).encode())
    freeze['distribution_changes'] = list(distribution.values())
    freeze['previous_public_manifest_sha256'] = previous
    freeze['branding_metadata_changes'] = changed
    freeze['audio_dsp_or_input_logic_changed'] = False
    freeze['engine_inputs_byte_identical_to_previous_public_build'] = True
    rows[key] = dump(freeze)
    # The patch algorithm itself is corresponding release-authoring source.
    rows[PREFIX + 'release-tools/refresh_public_branding.py'] = Path(__file__).read_bytes()
    old_manifest = json.loads(rows[PREFIX + 'SOURCE-FILES.json'])
    old_manifest['freeze'] = freeze['manifest_sha256']
    old_manifest['previous_public_freeze'] = previous
    old_manifest['files'] = [{'path': n[len(PREFIX):], 'bytes': len(b), 'sha256': sha(b)}
                             for n, b in sorted(rows.items()) if n != PREFIX + 'SOURCE-FILES.json']
    rows[PREFIX + 'SOURCE-FILES.json'] = dump(old_manifest)
    return rows, freeze

def refresh_checksums(rows):
    for name in list(rows):
        if not name.endswith('SHA256SUMS.txt'):
            continue
        prefix = name.rsplit('/', 1)[0] + '/' if '/' in name else ''
        lines = []
        for line in rows[name].decode().splitlines():
            m = re.fullmatch(r'[0-9a-f]{64}  (.+)', line)
            if m and prefix + m[1] in rows:
                line = sha(rows[prefix + m[1]]) + '  ' + m[1]
            lines.append(line)
        rows[name] = ('\n'.join(lines) + '\n').encode()

def package_rows(rows, freeze):
    rows = {n: patch_text(n, b) for n, b in rows.items()}
    manifest_name = 'PROGRAM-MANIFEST.json'
    if manifest_name in rows:
        m = json.loads(rows[manifest_name])
        m['engine_build_source_manifest'] = m['production_source_manifest']
        m['production_source_manifest'] = freeze['manifest_sha256']
        for name in rows:
            if name.endswith('EXPERIMENTAL-STATUS.json'):
                v = json.loads(rows[name]); v['source_freeze_manifest_sha256'] = freeze['manifest_sha256']
                v['engine_build_source_manifest'] = freeze['previous_public_manifest_sha256']; rows[name] = dump(v)
        refresh_checksums(rows)
        for entry in m['files']:
            b = rows['Card-Payload/' + entry['path']]; entry.update(bytes=len(b), sha256=sha(b))
        rows[manifest_name] = dump(m)
    for name in ['OWN-BRANDING-BUILD.json', 'update-manifest.json']:
        if name in rows:
            m = json.loads(rows[name])
            m['engine_build_source_manifest'] = freeze['previous_public_manifest_sha256']
            m['source_production_manifest'] = freeze['manifest_sha256']; rows[name] = dump(m)
    return rows

def verify_rows(name, rows, freeze):
    for n, b in rows.items():
        if Path(n).suffix.lower() in TEXT_EXT:
            assert OLD_REPO.encode() not in b and OLD_SITE.encode() not in b, (name, n)
    if name == SOURCE_NAME:
        for row in freeze['entries']:
            b = rows[PREFIX + 'source/' + row['path']]
            assert len(b) == row['bytes'] and sha(b) == row['sha256'], row['path']
        m = json.loads(rows[PREFIX + 'SOURCE-FILES.json'])
        for row in m['files']:
            b = rows[PREFIX + row['path']]
            assert len(b) == row['bytes'] and sha(b) == row['sha256'], row['path']
    elif 'PROGRAM-MANIFEST.json' in rows:
        for row in json.loads(rows['PROGRAM-MANIFEST.json'])['files']:
            b = rows['Card-Payload/' + row['path']]
            assert len(b) == row['bytes'] and sha(b) == row['sha256'], row['path']

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True); args = parser.parse_args()
    src = args.input.resolve(); out = args.output.resolve()
    assert src != out, 'Use a separate output directory; keep baseline backups'
    out.mkdir(parents=True, exist_ok=True)
    for name, expected in BASELINES.items():
        assert sha((src / name).read_bytes()) == expected, 'Unexpected baseline: ' + name
    rows, freeze = source_rows(load_archive(src / SOURCE_NAME))
    reports = []
    for name in [SOURCE_NAME] + [n for n in BASELINES if n != SOURCE_NAME]:
        payload = rows if name == SOURCE_NAME else package_rows(load_archive(src / name), freeze)
        verify_rows(name, payload, freeze)
        reports.append(rewrite_archive(src / name, out / name, payload))
    m = json.loads((src / 'PUBLIC-RELEASE-MANIFEST.json').read_text())
    for entry in m['assets']:
        new = next((r for r in reports if r['file'] == entry['file']), None)
        if new:
            entry.update({k: new[k] for k in ['bytes', 'sha256', 'entries']})
            if entry['file'] == SOURCE_NAME:
                entry['production_changes'] = 'Identity/artwork plus documentation and launcher metadata; audio DSP and input logic unchanged'
    m['production_source_manifest'] = freeze['manifest_sha256']
    m['engine_build_source_manifest'] = freeze['previous_public_manifest_sha256']
    m['repository'] = NEW_REPO; m['website'] = NEW_SITE + '/'
    m['branding_text_refresh'] = 'Own public names and canonical URLs; binaries, data paths, presets and legal credits preserved'
    (out / 'PUBLIC-RELEASE-MANIFEST.json').write_bytes(dump(m))
    checks = (src / 'SHA256SUMS.txt').read_text().splitlines()
    patched = {r['file']: r['sha256'] for r in reports}
    for i, line in enumerate(checks):
        match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
        if match and match[2] in patched: checks[i] = patched[match[2]] + '  ' + match[2]
    (out / 'SHA256SUMS.txt').write_text('\n'.join(checks) + '\n')
    (out / 'PUBLIC-PRODUCTION-SOURCE-FREEZE.json').write_bytes(dump(freeze))
    report = {'status': 'PUBLIC_BRANDING_TEXT_AND_CANONICAL_URLS_VERIFIED',
              'repository': NEW_REPO, 'website': NEW_SITE + '/',
              'source_manifest': freeze['manifest_sha256'],
              'previous_public_source_manifest': freeze['previous_public_manifest_sha256'],
              'unchanged_ELF_APK_audio_input_presets_samples_art_legal': True,
              'cross_platform_reproducibility': 'Raw unchanged ZIP records; ZIP_STORED changed text; no zlib recompression',
              'assets': reports}
    (out / 'PUBLIC-BRANDING-REFRESH-QA.json').write_bytes(dump(report))
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
