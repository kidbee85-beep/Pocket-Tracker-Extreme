#!/usr/bin/env python3
"""Exercise the actual optional preparer and the operating system's no-clobber commit."""
from pathlib import Path
import contextlib,ctypes,errno,hashlib,importlib.util,io,json,subprocess,sys,tempfile,types,unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[2]
SCRIPT=ROOT/'tools/release/prepare_presets.py'
spec=importlib.util.spec_from_file_location('preset_preparer',SCRIPT)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class Preparation(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='preset spaces $() literal ')
        self.root=Path(self.temp.name);self.factory=self.root/'factory/Presets';self.factory.mkdir(parents=True)
        for name,value in [('Drums/Kick 01.pti','KICK'),('Synths/Pad.pti','PAD')]:
            p=self.factory/name;p.parent.mkdir(exist_ok=True);p.write_text(value)
        h=hashlib.sha256()
        for p in sorted(self.factory.rglob('*'),key=lambda p:p.relative_to(self.factory).as_posix()):
            if p.is_file():h.update(p.relative_to(self.factory).as_posix().encode()+b'\0');h.update(hashlib.sha256(p.read_bytes()).digest())
        self.pack=h.hexdigest();(self.factory.parent/'presets-pack.id').write_text(self.pack+'\n')
        self.data=self.root/'data'
    def tearDown(self):self.temp.cleanup()
    def run_cli(self):return subprocess.run([sys.executable,str(SCRIPT),'--factory',str(self.factory),'--data',str(self.data)],capture_output=True,text=True,timeout=30)
    def test_fresh_setup_and_marker(self):
        result=self.run_cli();self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((self.data/'Presets/Drums/Kick 01.pti').read_text(),'KICK')
        self.assertEqual((self.data/'.factory-presets-pack.id').read_text().strip(),self.pack)
        self.assertFalse(list(self.data.rglob('.pte-*')))
    def test_existing_edit_is_preserved(self):
        edited=self.data/'Presets/Drums/Kick 01.pti';edited.parent.mkdir(parents=True);edited.write_text('MY EDIT')
        self.assertEqual(self.run_cli().returncode,0);self.assertEqual(edited.read_text(),'MY EDIT')
        self.assertEqual((self.data/'Presets/Synths/Pad.pti').read_text(),'PAD')
    def test_bad_source_fails_before_destination_creation(self):
        (self.factory/'Synths/Pad.pti').write_text('CORRUPT')
        self.assertNotEqual(self.run_cli().returncode,0);self.assertFalse(self.data.exists())
    @unittest.skipIf(sys.platform=='win32','Symlink permission is optional on Windows')
    def test_parent_symlink_is_rejected(self):
        outside=self.root/'outside';outside.mkdir();presets=self.data/'Presets';presets.mkdir(parents=True);(presets/'Drums').symlink_to(outside,target_is_directory=True)
        self.assertNotEqual(self.run_cli().returncode,0);self.assertFalse(list(outside.iterdir()));self.assertFalse((self.data/'.factory-presets-pack.id').exists())
    def test_existing_target_directory_is_preserved(self):
        p=self.data/'Presets/Drums/Kick 01.pti';p.mkdir(parents=True)
        self.assertNotEqual(self.run_cli().returncode,0);self.assertTrue(p.is_dir())
    def test_atomic_commit_cannot_replace_file_created_by_another_writer(self):
        temp=self.root/'new.tmp';temp.write_text('NEW');target=self.root/'existing.pti';target.write_text('USER')
        with self.assertRaises(FileExistsError):module.commit_new_file(temp,target)
        self.assertEqual(target.read_text(),'USER');self.assertEqual(temp.read_text(),'NEW')
    @unittest.skipIf(sys.platform=='win32','Exercises the macOS/POSIX no-replace path')
    def test_unsupported_macos_filesystem_keeps_custom_preset_and_marker(self):
        class UnsupportedRename:
            def __call__(self, source, target, flags):
                self.flags=flags
                ctypes.set_errno(errno.ENOTSUP)
                return -1
        for existing_marker in (False,True):
            with self.subTest(existing_marker=existing_marker):
                data=self.root/('unsupported-with-marker' if existing_marker else 'unsupported-fresh')
                custom=data/'Presets/Drums/Kick 01.pti';custom.parent.mkdir(parents=True);custom.write_bytes(b'MY CUSTOM PRESET')
                marker=data/'.factory-presets-pack.id'
                if existing_marker:marker.write_bytes(b'previous-pack-marker\n')
                rename=UnsupportedRename();libc=types.SimpleNamespace(renamex_np=rename)
                stderr=io.StringIO()
                with mock.patch.object(module.ctypes,'CDLL',return_value=libc), mock.patch.object(module.sys,'platform','darwin'), mock.patch.object(module.sys,'argv',[str(SCRIPT),'--factory',str(self.factory),'--data',str(data)]), contextlib.redirect_stderr(stderr):
                    result=module.main()
                self.assertEqual(result,1)
                self.assertEqual(rename.flags,4)  # Actual RENAME_EXCL call returned ENOTSUP.
                self.assertIn('does not support the atomic no-replace operation',stderr.getvalue())
                self.assertIn('first launch will prepare missing presets',stderr.getvalue())
                self.assertEqual(custom.read_bytes(),b'MY CUSTOM PRESET')
                self.assertFalse((data/'Presets/Synths/Pad.pti').exists())
                if existing_marker:self.assertEqual(marker.read_bytes(),b'previous-pack-marker\n')
                else:self.assertFalse(marker.exists())
                self.assertFalse(list(data.rglob('.pte-*')))
    def test_repeated_preparation_keeps_all_files(self):
        self.assertEqual(self.run_cli().returncode,0);self.assertEqual(self.run_cli().returncode,0)
        self.assertEqual(len(list((self.data/'Presets').rglob('*.pti'))),2)

if __name__=='__main__':
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Preparation))
    raise SystemExit(0 if result.wasSuccessful() else 1)
