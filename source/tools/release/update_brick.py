#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline app updater. Shut down the Brick and insert its card first.
Replaces the program and optionally adds verified factory assets. User data stays put.
"""
import argparse
import contextlib
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile

PROGRAM = 'pockettracker_extreme.aarch64'
BACKUPS = '.pte-program-backups'
SHA = re.compile(r'[0-9a-f]{64}')


def checked_path(path):
    path = Path(os.path.abspath(os.fspath(path)))
    for entry in [*reversed(path.parents), path]:
        if entry.is_symlink():
            raise RuntimeError('A selected path contains a symbolic link.')
    return path


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def elf_check(path):
    if not path.is_file() or path.is_symlink():
        raise RuntimeError('Program is missing, linked or not a regular file.')
    with path.open('rb') as source:
        head = source.read(64)
    if len(head) != 64 or head[:6] != b'\x7fELF\x02\x01' or int.from_bytes(head[18:20], 'little') != 183:
        raise RuntimeError('Program is not a little-endian ARM64 Linux executable.')


def sync_directory(path):
    if os.name == 'nt':
        return
    fd = os.open(path, os.O_RDONLY)
    try:
        try:
            os.fsync(fd)
        except OSError as error:
            if error.errno not in (errno.EINVAL, errno.ENOTSUP):
                raise
    finally:
        os.close(fd)


def stage_copy(source, directory, expected, mode=0o755):
    fd, name = tempfile.mkstemp(prefix='.pte-program-', dir=directory)
    temp = Path(name)
    try:
        with os.fdopen(fd, 'wb') as output, source.open('rb') as input_file:
            for block in iter(lambda: input_file.read(1024 * 1024), b''):
                output.write(block)
            output.flush()
            os.fsync(output.fileno())
        if digest(temp) != expected:
            raise RuntimeError('Copied program checksum failed.')
        try:
            temp.chmod(mode)
        except OSError as error:
            if error.errno not in (errno.EPERM, errno.ENOTSUP):
                raise
        elf_check(temp)
        return temp
    except BaseException:
        temp.unlink(missing_ok=True)
        raise


@contextlib.contextmanager
def update_lock(app):
    lock = app / '.pte-program-update.lock'
    try:
        fd = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        raise RuntimeError('Update lock exists. Close the other updater. After interruption, verify the program and backup before removing this lock.') from None
    try:
        with os.fdopen(fd, 'w') as output:
            output.write('TrackWerk program update\n')
            output.flush()
            os.fsync(output.fileno())
        yield
    finally:
        lock.unlink(missing_ok=True)


def select_install(target):
    app = checked_path(target)
    if app.name != 'pockettracker_extreme' or not app.is_dir():
        raise RuntimeError('Select the existing roms/ports/pockettracker_extreme folder.')
    program = checked_path(app / PROGRAM)
    elf_check(program)
    if not (app / 'data').is_dir() or (app / 'data').is_symlink():
        raise RuntimeError('Existing data folder is missing or linked; this is an update, not a new installation.')
    return app, program


def install_program(source, target, expected, version):
    if not SHA.fullmatch(expected):
        raise RuntimeError('Invalid expected checksum.')
    source = checked_path(source)
    elf_check(source)
    if digest(source) != expected:
        raise RuntimeError('Package checksum failed; installed files were not changed.')
    app, program = select_install(target)
    with update_lock(app):
        before = digest(program)
        if before == expected:
            return {'status': 'already_current', 'sha256': expected, 'version': version}
        backups = checked_path(app / BACKUPS)
        backups.mkdir(exist_ok=True)
        backup = checked_path(backups / (before + '.aarch64'))
        mode = stat.S_IMODE(program.stat().st_mode) | stat.S_IXUSR
        temp = stage_copy(source, app, expected, mode)
        backup_temp = None
        try:
            if backup.exists():
                if digest(backup) != before:
                    raise RuntimeError('Existing rollback program checksum failed.')
                elf_check(backup)
            else:
                backup_temp = stage_copy(program, backups, before, mode)
                os.replace(backup_temp, backup)
                backup_temp = None
                sync_directory(backups)
            if digest(program) != before:
                raise RuntimeError('Installed program changed during preparation.')
            os.replace(temp, program)
            sync_directory(app)
            if digest(program) != expected:
                raise RuntimeError('Installed program verification failed; the retained backup can be restored.')
            return {'status': 'updated', 'version': version, 'sha256': expected,
                    'previous_sha256': before, 'backup_file': backup.name,
                    'changed_program': PROGRAM, 'data_modified': False}
        finally:
            temp.unlink(missing_ok=True)
            if backup_temp is not None:
                backup_temp.unlink(missing_ok=True)


def read_package(package):
    package = checked_path(package)
    value = json.loads(checked_path(package / 'update-manifest.json').read_text(encoding='utf-8'))
    if not isinstance(value, dict) or value.get('platform') != 'trimui-brick' or value.get('program') != PROGRAM:
        raise RuntimeError('Not a Brick program-only update package.')
    sha = value.get('sha256', '')
    if not isinstance(sha, str) or not SHA.fullmatch(sha):
        raise RuntimeError('Invalid package checksum.')
    source = checked_path(package / PROGRAM)
    if source.stat().st_size != value.get('bytes'):
        raise RuntimeError('Package size verification failed.')
    version = value.get('version')
    if not isinstance(version, str) or not version.strip() or len(version) > 80:
        raise RuntimeError('Invalid package version.')
    return source, sha, version


def install_factory(package, target, expected):
    """Add missing immutable factory files; never write into the user's data tree."""
    if not isinstance(expected, str) or not SHA.fullmatch(expected):
        raise RuntimeError('Invalid factory preset pack checksum.')
    directory = checked_path(Path(package) / 'factory' / 'Presets')
    if not directory.is_dir():
        raise RuntimeError('Factory preset payload is missing.')
    entries = []
    pack = hashlib.sha256()
    for source in sorted(directory.rglob('*'), key=lambda p: p.relative_to(directory).as_posix()):
        if source.is_symlink():
            raise RuntimeError('Factory preset payload contains a symbolic link.')
        if not source.is_file() or source.name.startswith('.'):
            continue
        relative = source.relative_to(directory)
        value = digest(source)
        pack.update(relative.as_posix().encode('utf-8') + b'\0')
        pack.update(bytes.fromhex(value))
        entries.append((source, relative, value))
    if not entries or pack.hexdigest() != expected:
        raise RuntimeError('Factory preset checksum failed; installed files were not changed.')
    marker_source = checked_path(directory.parent / 'presets-pack.id')
    if marker_source.read_text().strip() != expected:
        raise RuntimeError('Factory preset marker does not match its payload.')
    app, _ = select_install(target)
    destination = checked_path(app / 'factory' / 'Presets')
    with update_lock(app):
        # Validate every existing asset before adding anything. An owner-modified
        # factory file is kept and reported, never silently replaced.
        for _, relative, value in entries:
            path = checked_path(destination / relative)
            if path.exists() and (not path.is_file() or digest(path) != value):
                raise RuntimeError('An installed factory file differs; kept unchanged: ' + relative.as_posix())
        added = 0
        changed_directories = set()
        for source, relative, value in entries:
            path = checked_path(destination / relative)
            path.parent.mkdir(parents=True, exist_ok=True)
            try:
                fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
            except FileExistsError:
                if not path.is_file() or digest(path) != value:
                    raise RuntimeError('A factory destination changed during preparation.') from None
                continue
            try:
                with os.fdopen(fd, 'wb') as output, source.open('rb') as input_file:
                    for block in iter(lambda: input_file.read(1024 * 1024), b''):
                        output.write(block)
                    output.flush()
                    os.fsync(output.fileno())
                if digest(path) != value:
                    raise RuntimeError('Copied factory preset verification failed.')
            except BaseException:
                # Only this updater's exclusively created new asset is discarded.
                path.unlink(missing_ok=True)
                raise
            added += 1
            directory_entry = path.parent
            while directory_entry != app:
                changed_directories.add(directory_entry)
                directory_entry = directory_entry.parent
        for directory_entry in sorted(changed_directories, key=lambda p: len(p.parts), reverse=True):
            sync_directory(directory_entry)
        marker = checked_path(app / 'factory' / 'presets-pack.id')
        fd, name = tempfile.mkstemp(prefix='.pte-pack-', dir=marker.parent)
        temp = Path(name)
        try:
            with os.fdopen(fd, 'w') as output:
                output.write(expected + '\n'); output.flush(); os.fsync(output.fileno())
            os.replace(temp, marker)
            sync_directory(marker.parent)
        finally:
            temp.unlink(missing_ok=True)
    return {'added': added, 'factory_files': len(entries), 'pack_id': expected,
            'user_data_modified': False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True, type=Path)
    parser.add_argument('--package', type=Path, default=Path(__file__).absolute().parent)
    parser.add_argument('--rollback', metavar='SHA256', help='Full SHA256 backup identifier, without .aarch64')
    args = parser.parse_args(argv)
    factory = None
    try:
        if args.rollback is not None:
            if not SHA.fullmatch(args.rollback):
                raise RuntimeError('Rollback requires a full lower-case SHA256 backup identifier.')
            app, _ = select_install(args.target)
            source = checked_path(app / BACKUPS / (args.rollback + '.aarch64'))
            result = install_program(source, app, args.rollback, 'rollback')
        else:
            source, sha, version = read_package(args.package)
            elf_check(source)
            if digest(source) != sha:
                raise RuntimeError('Package checksum failed; installed files were not changed.')
            manifest = json.loads((args.package / 'update-manifest.json').read_text(encoding='utf-8'))
            if 'factory_pack_id' in manifest:
                factory = install_factory(args.package, args.target, manifest['factory_pack_id'])
            result = install_program(source, args.target, sha, version)
            if factory is not None:
                result['factory_assets'] = factory
        print(json.dumps(result, indent=2))
        print('Samples, projects, presets, themes, controls and folder structure retained. Eject the card safely before removing it.')
        return 0
    except (OSError, RuntimeError, ValueError, TypeError) as error:
        print('Program update stopped:', error, file=sys.stderr)
        if factory is not None:
            print('The verified factory library was prepared, but the program update did not finish. '
                  'User data is unchanged; rerun the same update package.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
