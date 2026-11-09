#!/usr/bin/env python3
"""Exercise the standalone launcher using real Bash and owned process fixtures."""
from pathlib import Path
import hashlib,json,os,shutil,signal,subprocess,tempfile,time,unittest
HERE=Path(__file__).resolve().parent
LAUNCHER=Path(__file__).resolve().parents[2]/'shell/r36s/Pocket Tracker Extreme.sh'
NATIVE='''#!/usr/bin/python3
from pathlib import Path
import os,sys,json,time,signal
keys=['SDL_GAMECONTROLLERCONFIG','SDL_GAMECONTROLLERCONFIG_FILE','SDL_VIDEODRIVER','SDL_AUDIODRIVER','SDL_RENDER_DRIVER','LD_LIBRARY_PATH','POCKETTRACKER_HOME','POCKETTRACKER_AUDIO_FRAMES','POCKETTRACKER_FULLSCREEN','POCKETTRACKER_CACERT','POCKETTRACKER_VIDEO','POCKETTRACKER_SYNC_STARTUP','PTE_STARTUP_READY_FILE','PATH']
Path(os.environ['PTE_TEST_SNAPSHOT']).write_text(json.dumps({'cwd':os.getcwd(),'argv':sys.argv[1:],'pid':os.getpid(),'env':{k:os.environ.get(k) for k in keys}}))
mode=os.environ.get('PTE_TEST_MODE','normal')
if mode=='hang': signal.signal(signal.SIGTERM,signal.SIG_IGN)
if mode!='no-frame' and mode!='hang':Path(os.environ['PTE_STARTUP_READY_FILE']).write_text('invalid\\n' if os.environ.get('PTE_TEST_BAD_MARKER') else 'frame submitted\\n')
Path(os.environ['PTE_TEST_READY']).write_text('entered')
if mode in ('wait','no-frame','hang'):
 def stop(sig,frame):
  Path(os.environ['PTE_TEST_SIGNAL']).write_text(str(sig));sys.exit(0)
 if mode!='hang':signal.signal(signal.SIGTERM,stop)
 while True:time.sleep(.02)
if mode=='long-ready':time.sleep(2.3)
sys.exit(int(os.environ.get('PTE_TEST_RC','0')))
'''
class Launcher(unittest.TestCase):
 def setUp(self):
  self.root=Path(tempfile.mkdtemp(prefix='card spaces $() ; ` literal ',dir=HERE))
  self.ports=self.root/'ports';self.ports.mkdir();self.app=self.ports/'pockettracker_extreme';self.app.mkdir()
  self.script=self.ports/'Pocket Tracker Extreme.sh';shutil.copyfile(LAUNCHER,self.script)
  self.binary=self.app/'pockettracker_extreme.aarch64';self.binary.write_text(NATIVE);self.binary.chmod(0o644)
  self.tools=self.app/'tools';self.tools.mkdir();(self.tools/'cacert.pem').write_text('fixture')
  for name in ['qjs','yt-dlp','pte-iphone-usb','pte-iphone-pair','pte-usbmuxd']:(self.tools/name).write_text('#!/bin/sh\nexit 0\n')
  self.db=self.app/'controllerdb/gamecontrollerdb.txt';self.db.parent.mkdir();self.db.write_text('pinned fixture db')
  self.data=self.app/'data';self.data.mkdir();(self.data/'own.ptp').write_text('KEEP')
  self.snapshot=self.root/'snapshot.json';self.log=self.app/'log.txt';self.home=self.root/'home';self.home.mkdir()
  self.env=dict(os.environ)
  for name in ['SDL_GAMECONTROLLERCONFIG','SDL_GAMECONTROLLERCONFIG_FILE','SDL_VIDEODRIVER','SDL_AUDIODRIVER','SDL_RENDER_DRIVER','LD_LIBRARY_PATH','PTE_STARTUP_TIMEOUT_SECONDS','POCKETTRACKER_HOME','POCKETTRACKER_FULLSCREEN','POCKETTRACKER_AUDIO_FRAMES','POCKETTRACKER_CACERT','POCKETTRACKER_VIDEO']:
   self.env.pop(name,None)
  self.env.update(HOME=str(self.home),PTE_TEST_SNAPSHOT=str(self.snapshot),PTE_TEST_READY=str(self.root/'ready'),PTE_TEST_SIGNAL=str(self.root/'signal'),PTE_STARTUP_TIMEOUT_SECONDS='3')
  self.proc=None
 def tearDown(self):
  if self.proc and self.proc.poll() is None:
   self.proc.send_signal(signal.SIGTERM)
   try:self.proc.wait(timeout=12)
   except subprocess.TimeoutExpired:self.proc.kill();self.proc.wait()
  shutil.rmtree(self.root)
 def run_app(self,*args,timeout=8):return subprocess.run(['/bin/bash',str(self.script),*args],cwd='/tmp',env=self.env,capture_output=True,text=True,timeout=timeout)
 def snap(self):return json.loads(self.snapshot.read_text())
 def seed(self,entries,pack='a'*64):
  factory=self.app/'factory/Presets'
  for rel,content in entries.items():
   target=factory/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(content)
  (self.app/'factory/presets-pack.id').write_text(pack+'\n')
  return factory
 def launch_wait(self,mode='wait'):
  self.env['PTE_TEST_MODE']=mode
  self.proc=subprocess.Popen(['/bin/bash',str(self.script)],cwd='/tmp',env=self.env)
  deadline=time.monotonic()+5
  while time.monotonic()<deadline and not (self.root/'ready').exists():time.sleep(.02)
  self.assertTrue((self.root/'ready').exists(),self.log.read_text())
 def test_no_portmaster_path_spaces_and_argv(self):
  r=self.run_app('song $literal ; `x`.ptp','media folder');self.assertEqual(r.returncode,0,r.stderr+self.log.read_text())
  s=self.snap();self.assertEqual(s['cwd'],str(self.app));self.assertEqual(s['argv'],['song $literal ; `x`.ptp','media folder'])
  self.assertEqual(s['env']['POCKETTRACKER_HOME'],str(self.data));self.assertEqual((self.data/'own.ptp').read_text(),'KEEP')
  self.assertEqual(s['env']['POCKETTRACKER_VIDEO'],'renderer');self.assertEqual(s['env']['POCKETTRACKER_SYNC_STARTUP'],'1');self.assertEqual(s['env']['POCKETTRACKER_AUDIO_FRAMES'],'512');self.assertEqual(s['env']['POCKETTRACKER_FULLSCREEN'],'1')
  self.assertTrue(self.log.read_text().startswith('\n=== Pocket Tracker Extreme 0.2.0-beta.2-R36S-Community-Test: shell entered'))
  self.assertFalse(list(self.app.glob('.pte-startup-ready.*')))
 def test_first_marker_is_visible_before_external_work(self):
  fake=self.root/'early-tools';fake.mkdir();stub=fake/'wc'
  stub.write_text('#!/bin/sh\nprintf ready > "$PTE_TEST_WC_READY"\nwhile [ ! -e "$PTE_TEST_WC_RELEASE" ]; do sleep 0.05; done\nexec /usr/bin/wc "$@"\n');stub.chmod(0o755)
  release=self.root/'wc-release';ready=self.root/'wc-ready'
  self.env.update(PATH=str(fake)+os.pathsep+self.env['PATH'],PTE_TEST_WC_READY=str(ready),PTE_TEST_WC_RELEASE=str(release))
  self.proc=subprocess.Popen(['/bin/bash',str(self.script)],cwd='/tmp',env=self.env)
  try:
   deadline=time.monotonic()+5
   while not ready.exists() and time.monotonic()<deadline:time.sleep(.02)
   self.assertTrue(ready.exists());self.assertIsNone(self.proc.poll());self.assertFalse(self.snapshot.exists())
   self.assertIn('shell entered',self.log.read_text())
  finally:release.write_text('continue')
  self.assertEqual(self.proc.wait(timeout=5),0)
 def test_log_precedes_sync_and_native_launch(self):
  fake=self.root/'sync-tools';fake.mkdir();stub=fake/'sync'
  stub.write_text('#!/bin/sh\nif [ ! -e "$PTE_SYNC_CAPTURE" ]; then cp "$PTE_SYNC_LOG" "$PTE_SYNC_CAPTURE"; test ! -e "$PTE_TEST_SNAPSHOT" || exit 91; fi\n');stub.chmod(0o755)
  capture=self.root/'initial-sync-log'
  self.env.update(PATH=str(fake)+os.pathsep+self.env['PATH'],PTE_SYNC_LOG=str(self.log),PTE_SYNC_CAPTURE=str(capture))
  self.assertEqual(self.run_app().returncode,0)
  self.assertIn('shell entered',capture.read_text());self.assertNotIn('Launching native',capture.read_text())
 def test_explicit_video_mode_preserved(self):
  self.env['POCKETTRACKER_VIDEO']='surface'
  self.assertEqual(self.run_app().returncode,0)
  self.assertEqual(self.snap()['env']['POCKETTRACKER_VIDEO'],'surface')
 def test_hostile_pm_helpers_are_never_sourced(self):
  pm=self.ports/'PortMaster';pm.mkdir();(pm/'control.txt').write_text('echo SHOULD_NOT_RUN; exit 88\n')
  self.env['POCKETTRACKER_PORTMASTER']=str(pm)
  r=self.run_app();self.assertEqual(r.returncode,0);self.assertNotIn('SHOULD_NOT_RUN',self.log.read_text())
 def test_explicit_maps_and_driver_choices_preserved(self):
  self.env.update(SDL_GAMECONTROLLERCONFIG='MYMAP\nSECOND',SDL_GAMECONTROLLERCONFIG_FILE='/custom missing db',SDL_VIDEODRIVER='user-video',SDL_AUDIODRIVER='user-audio',SDL_RENDER_DRIVER='user-render',LD_LIBRARY_PATH='/user libraries',POCKETTRACKER_AUDIO_FRAMES='1024',POCKETTRACKER_FULLSCREEN='0',POCKETTRACKER_CACERT='/user/ca.pem',POCKETTRACKER_VIDEO='surface')
  self.assertEqual(self.run_app().returncode,0)
  for key in ['SDL_GAMECONTROLLERCONFIG','SDL_GAMECONTROLLERCONFIG_FILE','SDL_VIDEODRIVER','SDL_AUDIODRIVER','SDL_RENDER_DRIVER','LD_LIBRARY_PATH','POCKETTRACKER_AUDIO_FRAMES','POCKETTRACKER_FULLSCREEN','POCKETTRACKER_CACERT','POCKETTRACKER_VIDEO']:self.assertEqual(self.snap()['env'][key],self.env[key])
 def test_explicit_empty_mapping_file_is_preserved(self):
  self.env['SDL_GAMECONTROLLERCONFIG_FILE']='';self.assertEqual(self.run_app().returncode,0);self.assertEqual(self.snap()['env']['SDL_GAMECONTROLLERCONFIG_FILE'],'')
 def test_mapping_file_fallback(self):
  folder=self.home/'.config';folder.mkdir();db=folder/'gamecontrollerdb.txt';db.write_text('userdb')
  self.assertEqual(self.run_app().returncode,0)
  # A real KNULLI /tmp database has precedence if this host has one.
  expected='/tmp/gamecontrollerdb.txt' if Path('/tmp/gamecontrollerdb.txt').is_file() else str(db)
  self.assertEqual(self.snap()['env']['SDL_GAMECONTROLLERCONFIG_FILE'],expected)
 def test_bundled_mapping_fallback(self):
  self.assertEqual(self.run_app().returncode,0)
  expected='/tmp/gamecontrollerdb.txt' if Path('/tmp/gamecontrollerdb.txt').is_file() else str(self.db)
  self.assertEqual(self.snap()['env']['SDL_GAMECONTROLLERCONFIG_FILE'],expected)
 def test_inline_mapping_survives_fallback(self):
  self.env['SDL_GAMECONTROLLERCONFIG']='custom-guid,My Gamepad,a:b0,platform:Linux,\nsecond-map'
  self.assertEqual(self.run_app().returncode,0)
  self.assertEqual(self.snap()['env']['SDL_GAMECONTROLLERCONFIG'],self.env['SDL_GAMECONTROLLERCONFIG'])
 def test_missing_optional_database_is_not_fatal(self):
  self.db.unlink();self.assertEqual(self.run_app().returncode,0)
  if not Path('/tmp/gamecontrollerdb.txt').is_file():self.assertIsNone(self.snap()['env']['SDL_GAMECONTROLLERCONFIG_FILE'])
 def test_native_failure_code_preserved(self):
  self.env['PTE_TEST_RC']='17';self.assertEqual(self.run_app().returncode,17);self.assertIn('status 17',self.log.read_text())
 def test_missing_native_logs_exits_without_gui(self):
  self.binary.unlink();self.assertEqual(self.run_app().returncode,2);self.assertIn('App file',self.log.read_text());self.assertFalse(self.snapshot.exists())
 def test_seed_nested_names_and_ignore_mac_metadata(self):
  self.seed({'Instruments/Drum Lab/Kick 01.pti':'KICK','._ignore.pti':'METADATA','.hidden/ignore.pti':'METADATA','Instruments/.hidden.pti':'METADATA'})
  self.assertEqual(self.run_app().returncode,0,self.log.read_text())
  self.assertEqual((self.data/'Presets/Instruments/Drum Lab/Kick 01.pti').read_text(),'KICK')
  self.assertFalse((self.data/'Presets/._ignore.pti').exists());self.assertFalse((self.data/'Presets/.hidden').exists());self.assertFalse((self.data/'Presets/Instruments/.hidden.pti').exists())
  self.assertEqual((self.data/'.factory-presets-pack.id').read_text(),'a'*64+'\n')
 def test_same_pack_skips_find_and_copy(self):
  self.seed({'A.pti':'A'});self.assertEqual(self.run_app().returncode,0)
  fake=self.root/'fake-tools';fake.mkdir()
  for cmd in ['find','cp','cmp']:
   f=fake/cmd;f.write_text('#!/bin/sh\necho unexpected '+cmd+' >&2\nexit 93\n');f.chmod(0o755)
  self.env['PATH']=str(fake)+os.pathsep+self.env['PATH']
  self.assertEqual(self.run_app().returncode,0,self.log.read_text());self.assertIn('matching pack identifier',self.log.read_text())
 def test_changed_pack_adds_presets_preserves_edits(self):
  self.seed({'A.pti':'A'});self.assertEqual(self.run_app().returncode,0)
  (self.data/'Presets/A.pti').write_text('MY EDIT')
  self.seed({'A.pti':'NEW FACTORY A','B.pti':'B'},'b'*64);self.assertEqual(self.run_app().returncode,0)
  self.assertEqual((self.data/'Presets/A.pti').read_text(),'MY EDIT');self.assertEqual((self.data/'Presets/B.pti').read_text(),'B');self.assertEqual((self.data/'.factory-presets-pack.id').read_text(),'b'*64+'\n')
 def test_existing_symlink_preset_is_not_replaced(self):
  self.seed({'A.pti':'A'});(self.data/'Presets').mkdir();outside=self.root/'own.pti';outside.write_text('OWN');(self.data/'Presets/A.pti').symlink_to(outside)
  self.assertEqual(self.run_app().returncode,0);self.assertTrue((self.data/'Presets/A.pti').is_symlink());self.assertEqual(outside.read_text(),'OWN')
 def test_seed_refuses_symlink_parent(self):
  self.seed({'Category/A.pti':'A'});(self.data/'Presets').mkdir();outside=self.root/'outside';outside.mkdir();(self.data/'Presets/Category').symlink_to(outside,target_is_directory=True)
  self.assertEqual(self.run_app().returncode,2);self.assertFalse((outside/'A.pti').exists());self.assertFalse((self.data/'.factory-presets-pack.id').exists())
 def test_failed_copy_does_not_commit_marker_or_partial_target(self):
  self.seed({'A.pti':'A'});fake=self.root/'fake-tools';fake.mkdir();cmd=fake/'cp';cmd.write_text('#!/bin/sh\nprintf partial > "$3"\nexit 7\n');cmd.chmod(0o755);self.env['PATH']=str(fake)+os.pathsep+self.env['PATH']
  self.assertEqual(self.run_app().returncode,2);self.assertFalse((self.data/'.factory-presets-pack.id').exists());self.assertFalse((self.data/'Presets/A.pti').exists());self.assertFalse(list(self.data.rglob('.pte-*')))
 def test_real_first_frame_disarms_watchdog(self):
  self.env.update(PTE_TEST_MODE='long-ready',PTE_STARTUP_TIMEOUT_SECONDS='1');self.assertEqual(self.run_app().returncode,0,self.log.read_text());self.assertIn('first frame submitted',self.log.read_text())
 def test_invalid_marker_does_not_disable_watchdog(self):
  self.env.update(PTE_TEST_MODE='long-ready',PTE_TEST_BAD_MARKER='1',PTE_STARTUP_TIMEOUT_SECONDS='1')
  self.assertEqual(self.run_app(timeout=6).returncode,124,self.log.read_text())
  self.assertNotIn('startup watchdog complete',self.log.read_text())
 def test_first_frame_timeout_returns_frontend(self):
  self.env.update(PTE_TEST_MODE='no-frame',PTE_STARTUP_TIMEOUT_SECONDS='1');started=time.monotonic();r=self.run_app(timeout=6)
  self.assertEqual(r.returncode,124,self.log.read_text());self.assertLess(time.monotonic()-started,4);self.assertIn('STARTUP TIMEOUT',self.log.read_text());self.assertTrue((self.root/'signal').exists())
 def test_hung_child_is_killed_within_bound(self):
  self.env.update(PTE_TEST_MODE='hang',PTE_STARTUP_TIMEOUT_SECONDS='1');started=time.monotonic();r=self.run_app(timeout=13)
  self.assertEqual(r.returncode,124,self.log.read_text());self.assertLess(time.monotonic()-started,11);self.assertIn('status 137',self.log.read_text())
  with self.assertRaises(ProcessLookupError):os.kill(self.snap()['pid'],0)
 def test_termination_saves_and_preserves_signal_status(self):
  self.launch_wait();self.proc.send_signal(signal.SIGTERM);self.assertEqual(self.proc.wait(timeout=5),143);self.assertEqual((self.root/'signal').read_text(),str(int(signal.SIGTERM)));self.assertIn('Native process stopped with status 0',self.log.read_text())
 def test_invalid_startup_deadline(self):
  for value in ['0','121','-1','bad','1000','1;echo bad']:
   self.env['PTE_STARTUP_TIMEOUT_SECONDS']=value;self.assertEqual(self.run_app().returncode,2,value)
  self.env['PTE_STARTUP_TIMEOUT_SECONDS']='001';self.assertEqual(self.run_app().returncode,0)
 def test_rotation_and_helpers(self):
  previous='OLD\n'+'x'*(1048576+50);self.log.write_text(previous);self.assertEqual(self.run_app().returncode,0)
  self.assertTrue((self.app/'log.previous.txt').read_text().startswith(previous));self.assertNotIn('OLD\n',self.log.read_text())
  for n in ['qjs','yt-dlp','pte-iphone-usb','pte-iphone-pair','pte-usbmuxd']:self.assertTrue(os.access(self.tools/n,os.X_OK))

if __name__=='__main__':
 suite=unittest.defaultTestLoader.loadTestsFromTestCase(Launcher)
 result=unittest.TextTestRunner(verbosity=2).run(suite)
 (HERE/'result.json').write_text(json.dumps({'passed':result.wasSuccessful(),'tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'launcher_sha256':hashlib.sha256(LAUNCHER.read_bytes()).hexdigest(),'method':'Actual standalone Bash launcher and owned native process fixtures; no physical R36S/device/USB/network access'},indent=2)+'\n')
 raise SystemExit(0 if result.wasSuccessful() else 1)
