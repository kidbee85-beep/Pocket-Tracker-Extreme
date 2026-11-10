#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""TrackWerk 0.9 Onion installer. Plan by default; program backups and missing-only presets."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, json, os, re, shutil, stat, time, uuid

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''): h.update(b)
    return h.hexdigest()

def checked(root, relative=''):
    rel = PurePosixPath(relative)
    if rel.is_absolute() or '..' in rel.parts or '\\' in relative:
        raise RuntimeError('Unsafe relative path')
    p = root.joinpath(*rel.parts)
    for q in [p, *p.parents]:
        if q.is_symlink(): raise RuntimeError('Symbolic link retained; installer stopped: ' + str(q))
        if q == root: break
    return p

def copy_new(data, target, mode=0o644):
    target.parent.mkdir(parents=True, exist_ok=True); owned = None
    try:
        with target.open('xb') as f:
            s = os.fstat(f.fileno()); owned = (s.st_dev, s.st_ino)
            f.write(data); f.flush(); os.fsync(f.fileno())
            try: os.fchmod(f.fileno(), mode)
            except (AttributeError, OSError): pass
        if target.read_bytes() != data: raise RuntimeError('Readback failed: ' + str(target))
    except BaseException:
        if owned and target.exists():
            s = target.lstat()
            if (s.st_dev, s.st_ino) == owned: target.unlink()
        raise

def protected(home):
    found = {}
    for name in ['settings.json', 'config.json', 'config.example.json', 'autosave.ptp', 'template.ptp']:
        p = home / name
        if p.is_file() and not p.is_symlink(): found[name] = digest(p)
    for folder in ['Presets', 'Themes', 'Projects', 'Songs']:
        p = home / folder
        if p.is_dir() and not p.is_symlink():
            for f in p.rglob('*'):
                if f.is_file() and not f.is_symlink() and not f.name.startswith('.') and f.suffix.lower() not in ['.wav','.mp3','.flac','.ogg','.aiff','.aif','.m4a','.sf2']:
                    found[f.relative_to(home).as_posix()] = digest(f)
    return found

def load(package):
    manifest = json.loads((package / 'PROGRAM-MANIFEST.json').read_text())
    app = manifest['app_directory']; profile = manifest['profile']; rows = manifest['files']
    if (profile, app) not in [('mini-offline','PocketTrackerExtreme'),('plus-online','PocketTrackerExtremePlusArchive')]:
        raise RuntimeError('Unknown device profile')
    allowed = {f'App/{app}/config.json', f'App/{app}/icon.png', f'App/{app}/launch.sh', f'Roms/PORTS/Imgs/{app}.png', f'Roms/PORTS/Shortcuts/{app}.port'}
    seen = set()
    for row in rows:
        rel = row['path']
        if not (rel in allowed or rel.startswith(f'Roms/PORTS/Games/{app}/')) or rel.casefold() in seen:
            raise RuntimeError('Unexpected or duplicate program path')
        seen.add(rel.casefold()); p = checked(package / 'Card-Payload', rel)
        if p.stat().st_size != row['bytes'] or digest(p) != row['sha256']: raise RuntimeError('Package program checksum failed: ' + rel)
    core = f'Roms/PORTS/Games/{app}/pockettracker'
    if core not in seen and core.casefold() not in seen: raise RuntimeError('Missing ARM program')
    head = checked(package / 'Card-Payload', core).read_bytes()[:20]
    if head[:5] != b'\x7fELF\x01' or int.from_bytes(head[18:20], 'little') != 40: raise RuntimeError('Expected ARM32 program')
    factory = json.loads((package / 'FACTORY-MANIFEST.json').read_text())['files']
    if len(factory) != 2304: raise RuntimeError('Expected 2,304 factory preset files')
    for row in factory:
        p = checked(package / 'Factory/Presets', row['path'])
        if p.stat().st_size != row['bytes'] or digest(p) != row['sha256']: raise RuntimeError('Factory checksum failed')
    return manifest, core, factory

def source_data(root, row):
    data = checked(root, row['path']).read_bytes()
    if len(data) != row['bytes'] or hashlib.sha256(data).hexdigest() != row['sha256']:
        raise RuntimeError('Package source changed during installation')
    return data

def install(card, package, apply=False):
    card = Path(os.path.abspath(card)); package = Path(os.path.abspath(package)); checked(card)
    version = checked(card, '.tmp_update/onionVersion/version.txt')
    if not version.is_file(): raise RuntimeError('Select the root of an existing, booted Onion OS card. This installer does not install or format Onion.')
    mount_device = card.stat().st_dev; onion_version = version.read_text().strip()
    manifest, core, factory = load(package); rows = manifest['files']; home = checked(card, 'PocketTrackerExtreme')
    numbers=re.search(r'(\d+)\.(\d+)',onion_version)
    minimum=(4,3) if manifest['profile']=='plus-online' else (4,2)
    if not numbers or tuple(map(int,numbers.groups()))<minimum:
        raise RuntimeError('This profile requires Onion '+'.'.join(map(str,minimum))+' or newer; older versions have not been validated.')
    if not checked(card,'.tmp_update/script/stop_audioserver.sh').is_file():
        raise RuntimeError('Onion audio handover is missing. Finish the normal Onion installation before installing this app.')
    if manifest['profile']=='plus-online':
        for name in ['Emu/PORTS/launch_standalone.sh','.tmp_update/lib/libcurl.so.4']:
            if not checked(card,name).is_file():
                raise RuntimeError('Mini Plus online requires Onion 4.3, its Ports Collection and normal HTTPS runtime. Missing: '+name)
    before = protected(home); changes = []; required = 0
    for row in rows:
        p = checked(card, row['path']); old = p.read_bytes() if p.is_file() else None
        if p.exists() and not p.is_file(): raise RuntimeError('Program target is not a file')
        if old is None or hashlib.sha256(old).hexdigest() != row['sha256']:
            changes.append((row, old)); required += row['bytes'] + (len(old) if old is not None else 0)
    additions = []
    for row in factory:
        p = checked(home / 'Presets', row['path'])
        if p.exists():
            if not p.is_file(): raise RuntimeError('Preset path is not a file; retained')
        else: additions.append(row); required += row['bytes']
    free = shutil.disk_usage(card).free
    report = dict(status='PLAN_ONLY', version='0.9.0', profile=manifest['profile'], card=str(card), onion_version=onion_version,
                  program_files=len(rows), changed_program_files=len(changes), missing_factory_presets=len(additions),
                  retained_existing_factory_presets=len(factory)-len(additions), protected_existing_files=len(before),
                  free_bytes=free, required_bytes_with_reserve=required+64*1024*1024,
                  samples_projects_settings_overwritten=False, formats_or_deletes=False)
    if not apply: return report
    if free < report['required_bytes_with_reserve']: raise RuntimeError('Insufficient free space including reserve')
    def gate():
        if card.stat().st_dev != mount_device or version.read_text().strip() != onion_version: raise RuntimeError('Selected card changed; stopped')
    gate(); backup = checked(card, '.TrackWerk-Program-Backups') / ('0.9-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])
    backup.mkdir(parents=True, exist_ok=False)
    (backup/'BEFORE.json').write_text(json.dumps({'plan':report,'protected_hashes':before},indent=2)+'\n')
    for row, old in changes:
        if old is not None: copy_new(old, backup / row['path'])
    written = []
    try:
        for row, old in sorted(changes, key=lambda item:(item[0]['path']==core,item[0]['path'])):
            gate(); target = checked(card, row['path']); current = target.read_bytes() if target.exists() else None
            if current != old: raise RuntimeError('Program changed during update; retained')
            data = source_data(package/'Card-Payload',row)
            if old is None:
                copy_new(data,target,row['mode']); s=target.stat(); written.append((row,old,s.st_dev,s.st_ino))
            else:
                target.parent.mkdir(parents=True,exist_ok=True); temp=target.parent/('.trackwerk-'+uuid.uuid4().hex+'.part')
                try:
                    copy_new(data,temp,row['mode']);s=temp.stat();written.append((row,old,s.st_dev,s.st_ino));os.replace(temp,target)
                finally:
                    if temp.exists():temp.unlink()
        for row in rows:
            if digest(checked(card,row['path'])) != row['sha256']:raise RuntimeError('Program readback failed')
        for name in ['Samples','Presets','Projects','Themes','Renders','Soundfonts']:
            p=checked(home,name);p.mkdir(parents=True,exist_ok=True)
        added=0
        for row in additions:
            gate(); target=checked(home/'Presets',row['path'])
            if target.exists():continue
            data=source_data(package/'Factory/Presets',row)
            try:copy_new(data,target)
            except FileExistsError:continue
            added+=1
        for rel, value in before.items():
            if digest(checked(home,rel)) != value:raise RuntimeError('Existing user-file verification failed')
        gate();report.update(status='INSTALLED_VERIFIED_EXISTING_DATA_RETAINED',added_factory_presets=added,backup=str(backup),physical_device_tested=False)
        (backup/'AFTER.json').write_text(json.dumps(report,indent=2)+'\n');return report
    except BaseException:
        for row,old,dev,ino in reversed(written):
            target=checked(card,row['path'])
            if not target.exists():continue
            s=target.stat()
            if (s.st_dev,s.st_ino)!=(dev,ino) or digest(target)!=row['sha256']:continue
            if old is None:target.unlink()
            else:
                temp=target.parent/('.trackwerk-rollback-'+uuid.uuid4().hex);copy_new(old,temp,row['mode']);os.replace(temp,target)
        raise

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--card',required=True,type=Path);p.add_argument('--apply',action='store_true');a=p.parse_args()
    try:print(json.dumps(install(a.card,Path(__file__).resolve().parent,a.apply),indent=2))
    except (OSError,RuntimeError,ValueError,KeyError) as e:p.exit(1,'Installation stopped: '+str(e)+'\n')
