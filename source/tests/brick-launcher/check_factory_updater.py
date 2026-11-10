#!/usr/bin/env python3
"""Existing program-preservation checks plus factory-only update migrations."""
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('base_checks', Path(__file__).with_name('check_program_updater.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
updater = base.updater

class FactoryChecks(base.UpdateChecks):
    def factory(self):
        folder = self.package / 'factory/Presets'
        for relative, value in {'Untouched.json': b'factory', 'New/Room.ptr': b'new room', 'New/Echo.ptd': b'new echo'}.items():
            path = folder / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(value)
        pack = hashlib.sha256()
        for path in sorted(p for p in folder.rglob('*') if p.is_file()):
            pack.update(path.relative_to(folder).as_posix().encode() + b'\0')
            pack.update(hashlib.sha256(path.read_bytes()).digest())
        self.pack_id = pack.hexdigest()
        (folder.parent / 'presets-pack.id').write_text(self.pack_id + '\n')
        (self.app / 'factory/presets-pack.id').write_text('previous factory marker\n')
        self.manifest['factory_pack_id'] = self.pack_id
        (self.package / 'update-manifest.json').write_text(json.dumps(self.manifest))
        self.preserved = self.snapshot()

    def user_data(self):
        return {p.relative_to(self.app / 'data').as_posix(): p.read_bytes()
                for p in (self.app / 'data').rglob('*') if p.is_file()}

    def test_factory_addition_is_idempotent_and_never_seeds_user_data(self):
        self.factory(); before = self.user_data()
        result = updater.install_factory(self.package, self.app, self.pack_id)
        self.assertEqual(result['added'], 2)
        self.assertEqual(result['factory_files'], 3)
        self.assertFalse(result['user_data_modified'])
        self.assertEqual(self.user_data(), before)
        self.assertEqual(self.program.read_bytes(), self.old)
        self.assertEqual((self.app / 'factory/Presets/Untouched.json').read_bytes(), b'factory')
        self.assertEqual((self.app / 'factory/presets-pack.id').read_text(), self.pack_id + '\n')
        self.assertEqual(updater.install_factory(self.package, self.app, self.pack_id)['added'], 0)
        self.assertEqual(self.user_data(), before)

    def test_corrupt_factory_payload_changes_nothing(self):
        self.factory()
        (self.package / 'factory/Presets/New/Room.ptr').write_bytes(b'corruption')
        with self.assertRaisesRegex(RuntimeError, 'checksum'):
            updater.install_factory(self.package, self.app, self.pack_id)
        self.assert_preserved(self.old)

    def test_modified_installed_factory_is_kept_before_any_copy(self):
        self.factory(); (self.app / 'factory/Presets/Untouched.json').write_bytes(b'OWNER EDIT')
        self.preserved = self.snapshot()
        with self.assertRaisesRegex(RuntimeError, 'differs'):
            updater.install_factory(self.package, self.app, self.pack_id)
        self.assert_preserved(self.old)

    def test_bad_program_refuses_factory_changes_too(self):
        self.factory(); self.source.write_bytes(base.fake_elf(b'other same-size version'))
        self.manifest['bytes'] = self.source.stat().st_size
        (self.package / 'update-manifest.json').write_text(json.dumps(self.manifest))
        self.assertEqual(updater.main(['--target', str(self.app), '--package', str(self.package)]), 1)
        self.assert_preserved(self.old)

    def test_full_package_adds_only_factory_and_program(self):
        self.factory(); before = self.user_data(); launcher = self.launcher.read_bytes()
        self.assertEqual(updater.main(['--target', str(self.app), '--package', str(self.package)]), 0)
        self.assertEqual(self.program.read_bytes(), self.new)
        self.assertEqual(self.user_data(), before)
        self.assertEqual(self.launcher.read_bytes(), launcher)
        self.assertEqual((self.app / updater.BACKUPS / (self.old_sha + '.aarch64')).read_bytes(), self.old)

    def test_contending_factory_destination_is_never_overwritten(self):
        self.factory(); before = self.user_data(); original = updater.os.open
        destination = self.app / 'factory/Presets/New/Echo.ptd'
        def compete(path, flags, *args):
            if Path(path) == destination:
                destination.write_bytes(b'CONCURRENT OWNER FILE')
            return original(path, flags, *args)
        with patch.object(updater.os, 'open', compete):
            with self.assertRaisesRegex(RuntimeError, 'changed'):
                updater.install_factory(self.package, self.app, self.pack_id)
        self.assertEqual(destination.read_bytes(), b'CONCURRENT OWNER FILE')
        self.assertEqual(self.user_data(), before)
        self.assertEqual((self.app / 'factory/presets-pack.id').read_text(), 'previous factory marker\n')

    def test_factory_subfolder_symlink_is_refused(self):
        self.factory(); outside = self.root / 'outside'; outside.mkdir()
        (outside / 'owner').write_bytes(b'KEEP')
        (self.app / 'factory/Presets/New').symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, 'symbolic link'):
            updater.install_factory(self.package, self.app, self.pack_id)
        self.assertEqual(list(outside.iterdir()), [outside / 'owner'])
        self.assertEqual((outside / 'owner').read_bytes(), b'KEEP')

if __name__ == '__main__':
    unittest.main()
