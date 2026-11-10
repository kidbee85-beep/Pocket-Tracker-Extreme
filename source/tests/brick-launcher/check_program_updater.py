#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Meaningful offline-update preservation and failure-injection fixtures."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('updater', ROOT / 'tools/release/update_brick.py')
updater = importlib.util.module_from_spec(spec)
spec.loader.exec_module(updater)


def fake_elf(tail):
    head = bytearray(64)
    head[:6] = b'\x7fELF\x02\x01'
    head[18:20] = (183).to_bytes(2, 'little')
    return bytes(head) + tail


class UpdateChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pte-update-')
        self.root = Path(self.temp.name).resolve()
        self.app = self.root / 'SHARE/roms/ports/pockettracker_extreme'
        self.app.mkdir(parents=True)
        self.old = fake_elf(b'old-tested-version')
        self.new = fake_elf(b'new-beta2-version')
        self.program = self.app / updater.PROGRAM
        self.program.write_bytes(self.old)
        for name, value in {
            'data/Samples/Nested/my drum.wav': b'private drums',
            'data/Projects/my song.ptp': b'private song',
            'data/Presets/Edited preset.json': b'custom preset',
            'data/settings.json': b'theme32 surface8 controls custom',
            'data/.factory-presets-pack.id': b'existing-pack-id',
            'factory/Presets/Untouched.json': b'factory',
            'iphone/pairing.plist': b'private pairing',
            'log.txt': b'previous log',
            'capture-helper': b'existing capture helper',
            'settings.ini': b'user preferences',
        }.items():
            path = self.app / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(value)
        self.launcher = self.app.parent / 'Pocket Tracker Extreme.sh'
        self.launcher.write_bytes(b'working surface launcher')
        self.package = self.root / 'update'
        self.package.mkdir()
        self.source = self.package / updater.PROGRAM
        self.source.write_bytes(self.new)
        self.sha = hashlib.sha256(self.new).hexdigest()
        self.old_sha = hashlib.sha256(self.old).hexdigest()
        self.manifest = {'platform': 'trimui-brick', 'program': updater.PROGRAM,
                         'sha256': self.sha, 'bytes': len(self.new), 'version': '0.2.0-beta.2'}
        (self.package / 'update-manifest.json').write_text(json.dumps(self.manifest))
        self.preserved = self.snapshot()

    def tearDown(self):
        self.temp.cleanup()

    def snapshot(self):
        return {str(p.relative_to(self.app.parent)): p.read_bytes()
                for p in self.app.parent.rglob('*') if p.is_file()
                and p != self.program and updater.BACKUPS not in p.parts
                and not p.name.startswith('.pte-program-')}

    def install(self):
        return updater.install_program(self.source, self.app, self.sha, '0.2.0-beta.2')

    def assert_preserved(self, binary):
        self.assertEqual(self.program.read_bytes(), binary)
        self.assertEqual(self.snapshot(), self.preserved)
        self.assertFalse((self.app / '.pte-program-update.lock').exists())
        self.assertEqual([p for p in self.app.glob('.pte-program-*') if p.name != updater.BACKUPS], [])

    def test_update_backup_idempotency_and_rollback(self):
        result = self.install()
        self.assertEqual(result['previous_sha256'], self.old_sha)
        self.assertFalse(result['data_modified'])
        self.assert_preserved(self.new)
        backup = self.app / updater.BACKUPS / (self.old_sha + '.aarch64')
        self.assertEqual(backup.read_bytes(), self.old)
        self.assertEqual(self.install()['status'], 'already_current')
        self.assertEqual(updater.main(['--target', str(self.app), '--rollback', self.old_sha]), 0)
        self.assert_preserved(self.old)
        self.assertEqual((self.app / updater.BACKUPS / (self.sha + '.aarch64')).read_bytes(), self.new)

    def test_bad_checksum_changes_nothing(self):
        with self.assertRaisesRegex(RuntimeError, 'checksum'):
            updater.install_program(self.source, self.app, '0' * 64, 'test')
        self.assert_preserved(self.old)
        self.assertFalse((self.app / updater.BACKUPS).exists())

    def test_wrong_architecture_changes_nothing(self):
        self.source.write_bytes(b'not an ARM executable')
        with self.assertRaises(RuntimeError):
            self.install()
        self.assert_preserved(self.old)

    def test_wrong_destination_refused(self):
        for target in [self.root, self.app / 'data', self.app.parent]:
            with self.assertRaises(RuntimeError):
                updater.install_program(self.source, target, self.sha, 'test')
        self.assert_preserved(self.old)

    def test_backup_failure_keeps_program(self):
        original = updater.stage_copy
        def fail_backup(source, directory, expected, mode=0o755):
            if directory.name == updater.BACKUPS:
                raise OSError('simulated full card before backup')
            return original(source, directory, expected, mode)
        with patch.object(updater, 'stage_copy', fail_backup):
            with self.assertRaises(OSError):
                self.install()
        self.assert_preserved(self.old)

    def test_commit_failure_keeps_original_and_verified_backup(self):
        original = updater.os.replace
        def fail_commit(source, target):
            if Path(target) == self.program:
                raise OSError('simulated rename failure')
            return original(source, target)
        with patch.object(updater.os, 'replace', fail_commit):
            with self.assertRaises(OSError):
                self.install()
        self.assert_preserved(self.old)
        self.assertEqual((self.app / updater.BACKUPS / (self.old_sha + '.aarch64')).read_bytes(), self.old)

    def test_corrupted_existing_backup_refused(self):
        backup = self.app / updater.BACKUPS / (self.old_sha + '.aarch64')
        backup.parent.mkdir()
        backup.write_bytes(b'corrupt')
        with self.assertRaisesRegex(RuntimeError, 'rollback'):
            self.install()
        self.assert_preserved(self.old)

    def test_symbolic_link_escape_refused(self):
        link = self.root / 'linked-card'
        link.symlink_to(self.app, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, 'symbolic link'):
            updater.install_program(self.source, link, self.sha, 'test')
        (self.app / updater.BACKUPS).symlink_to(self.root, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, 'symbolic link'):
            self.install()
        self.assert_preserved(self.old)

    def test_concurrent_update_refused(self):
        lock = self.app / '.pte-program-update.lock'
        lock.write_text('other updater')
        with self.assertRaisesRegex(RuntimeError, 'lock'):
            self.install()
        self.assertEqual(self.program.read_bytes(), self.old)
        self.assertEqual(lock.read_text(), 'other updater')

    def test_manifest_path_and_size_guard(self):
        self.assertEqual(updater.read_package(self.package), (self.source, self.sha, '0.2.0-beta.2'))
        self.manifest['program'] = '../settings.ini'
        (self.package / 'update-manifest.json').write_text(json.dumps(self.manifest))
        self.assertNotEqual(updater.main(['--target', str(self.app), '--package', str(self.package)]), 0)
        self.assert_preserved(self.old)


if __name__ == '__main__':
    unittest.main(verbosity=2)
