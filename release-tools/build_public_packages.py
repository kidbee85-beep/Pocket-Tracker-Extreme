#!/usr/bin/env python3
"""Repackage verified 0.9 builds for public review; never rebuild or change engine binaries."""
from pathlib import Path
import collections, hashlib, json, re, shutil, stat, zipfile, xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[3]
PRIVATE=ROOT/'outputs/TrackWerk-0.9-PRIVATE'
SOURCE=ROOT/'work/rebrand-typography/source'
HERE=Path(__file__).resolve().parent
OUT=ROOT/'outputs/TrackWerk-0.9-Launch/Release'
WORK=HERE/'staging'
TAG='v0.9.0-beta'
REPO='https://github.com/kidbee85-beep/Pocket-Tracker-Extreme'
SOURCE_URL=REPO+'/releases/download/'+TAG+'/TrackWerk-0.9-Source.zip'
FREEZE='e3e7c1104ec4fa32800ca23190b6327081887a13b1a03606487bb12c0e8ef6e6'

def sha(data):return hashlib.sha256(data).hexdigest()
def dump(value):return (json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode()
def put(rows,path,data,mode=0o644):rows[path]=(data.encode() if isinstance(data,str) else data,mode)
def entries(path):
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        return {i.filename:(z.read(i),((i.external_attr>>16)&0o777) or 0o644) for i in z.infolist() if not i.is_dir()}
def archive(name,rows):
    target=OUT/name
    with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        folded=set()
        for path,(data,mode) in sorted(rows.items()):
            assert not Path(path).is_absolute() and '..' not in Path(path).parts and '\\' not in path
            key=path.casefold();assert key not in folded;folded.add(key)
            i=zipfile.ZipInfo(path,(2026,11,11,0,0,0));i.create_system=3;i.compress_type=zipfile.ZIP_DEFLATED;i.external_attr=(stat.S_IFREG|mode)<<16;z.writestr(i,data)
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
        for path,(data,_) in rows.items():assert z.read(path)==data
    return {'file':name,'bytes':target.stat().st_size,'sha256':sha(target.read_bytes()),'entries':len(rows)}

def factory():
    rows={};manifest=[];pack=hashlib.sha256();base=SOURCE/'factory/Presets'
    for p in sorted(base.rglob('*'),key=lambda x:x.relative_to(base).as_posix()):
        if not p.is_file() or p.name.startswith('.'):continue
        rel=p.relative_to(base).as_posix();data=p.read_bytes();put(rows,rel,data)
        manifest.append({'path':rel,'bytes':len(data),'sha256':sha(data)});pack.update(rel.encode()+b'\0');pack.update(bytes.fromhex(sha(data)))
    assert len(rows)==2304 and pack.hexdigest()==(SOURCE/'factory/presets-pack.id').read_text().strip()
    return rows,manifest,pack.hexdigest()

def miyoo(name,input_name,profile,app,factory_rows,factory_manifest):
    original=entries(PRIVATE/input_name);payload=dict(original)
    config_name=f'App/{app}/config.json'
    cfg={'label':'TrackWerk' if profile=='mini-offline' else 'TrackWerk - Online','icon':f'/mnt/SDCARD/App/{app}/icon.png','launch':'launch.sh','description':'Kid Bunka / TrackWerk 0.9 beta / '+profile}
    put(payload,config_name,dump(cfg))
    icon=original[f'Roms/PORTS/Imgs/{app}.png'][0];put(payload,f'App/{app}/icon.png',icon)
    if profile=='plus-online':
        launch='#!/bin/sh\n# Onion Ports owns the existing game launch; no duplicate audio/menu restoration.\nexec sh /mnt/SDCARD/Roms/PORTS/Shortcuts/PocketTrackerExtremePlusArchive.port\n'
        put(payload,f'App/{app}/launch.sh',launch,0o755)
    status_name=f'Roms/PORTS/Games/{app}/EXPERIMENTAL-STATUS.json';status=json.loads(payload[status_name][0])
    status['distribution']='PUBLIC REVIEW CANDIDATE / EXPERIMENTAL BETA 0.9';status['publication']='PREPARED, NOT PUBLISHED';status.pop('data_is_separate',None)
    status['factory_library']='2304 synthetic factory presets; personal samples and private demo songs excluded'
    put(payload,status_name,dump(status))
    default_name=f'Roms/PORTS/Games/{app}/miyoo-config.json';defaults=json.loads(payload[default_name][0]);defaults['folders']['instruments']='Presets/Instruments'
    for key,value in list(defaults.items()):
        if isinstance(value,str):defaults[key]=value.replace('PocketTracker','TrackWerk')
    put(payload,default_name,dump(defaults))
    guide=f'''TRACKWERK 0.9 BETA — {'MIYOO MINI (OFFLINE)' if profile=='mini-offline' else 'MIYOO MINI PLUS (ONLINE)'}

Use an existing, fully booted Onion OS card. Offline Mini reference: Onion 4.2.
Older Onion versions are not validated. Onion is a separate project:
https://onionui.github.io/
Do not format or reinstall an operating system to update this music app.
Mini Plus online requires Onion 4.3 and its Ports Collection/TLS runtime.
The normal Mini has no built-in Wi-Fi and uses local samples.

RECOMMENDED FIRST INSTALL OR UPDATE (computer with Python 3.9+)
Extract this complete ZIP to a folder on your computer, not directly over your
card's existing music directory. Keep install_miyoo.py and all its folders together.
Select the ROOT of your Onion SD card in the following commands.
macOS/Linux plan:
  python3 install_miyoo.py --card /Volumes/YOUR_ONION_CARD
Apply after reviewing that the correct card/profile are shown:
  python3 install_miyoo.py --card /Volumes/YOUR_ONION_CARD --apply
Windows: use py instead of python3 and --card E:\\ (your actual card drive).
The installer backs up replaced program files, retains existing samples,
projects, settings and custom presets, and adds only missing factory presets.
It does not copy a private sample library or touch Onion/game files.

START
Wait for all copies to finish, safely eject, insert the card into the powered-off
device and start Onion. Open Apps > {cfg['label']}.
If GameSwitcher is showing only game thumbnails, press B to return to the menu.
The cyan T/magenta W icon uses an explicit full path.

SAMPLES AND PROJECTS
PocketTrackerExtreme/Samples is the prepared local sample folder.
PocketTrackerExtreme/Projects stores songs; Presets stores the factory library
and your edits. The old internal namespace remains for compatibility.
Onion needs no Android-style folder permission grant.
Studio > Style offers 17 fonts and 50 themes; existing preferences stay selected.
Large WAVs open a storage-preview/IN-OUT excerpt workflow before the normal editor.

ONLINE / INPUT
{'Set Wi-Fi and the real date/time, open Studio > Source Sampler, choose Internet Archive, preview a track, mark IN/OUT, Render selection and Import WAV. Start with 10–15 seconds; this experimental profile caps online excerpts at 60 seconds. TLS verification remains enabled. Local sequencing and editing work offline.' if profile=='plus-online' else 'This offline profile has no online archive service. Local WAV preview and chopping work without Internet.'}
Physical microphone/USB recording is disabled in these Miyoo profiles.
Final 0.9 software passed constrained ARM tests; physical display, audio,
controls, Wi-Fi and real-time performance still need device acceptance.

UPDATES / SOURCE
Use this installer for later updates; retain your existing data folders.
GPL-3.0-or-later source: {SOURCE_URL}
Exact SDL source is the adjacent SDL2-mmiyoo-f6319cf9-source.tar.gz release asset.
Credits include conanizer/PocketTracker contributors and the third-party
authors listed in the bundled licenses. TrackWerk is an independent fork.
'''
    for key in [k for k in payload if k.endswith(('START-HERE.txt','START-HIER-DE.txt'))]:put(payload,key,guide)
    # Refresh the in-app checksum list after the metadata/icon-only changes.
    checks=f'Roms/PORTS/Games/{app}/SHA256SUMS.txt'
    prefix=f'Roms/PORTS/Games/{app}/'
    put(payload,checks,'\n'.join(sha(data)+'  '+path[len(prefix):] for path,(data,_) in sorted(payload.items()) if path.startswith(prefix) and path!=checks)+'\n')
    rows={'Card-Payload/'+p:v for p,v in payload.items()}
    manifest={'product':'TrackWerk','version':'0.9.0','profile':profile,'app_directory':app,'production_source_manifest':FREEZE,'files':[{'path':p,'bytes':len(data),'sha256':sha(data),'mode':mode} for p,(data,mode) in sorted(payload.items())]}
    put(rows,'PROGRAM-MANIFEST.json',dump(manifest));put(rows,'FACTORY-MANIFEST.json',dump({'files':factory_manifest}))
    for p,v in factory_rows.items():rows['Factory/Presets/'+p]=v
    put(rows,'Factory/presets-pack.id',(SOURCE/'factory/presets-pack.id').read_bytes())
    put(rows,'install_miyoo.py',(HERE/'install_miyoo.py').read_bytes(),0o755);put(rows,'START-HERE.txt',guide)
    put(rows,'LICENSE',(SOURCE/'LICENSE').read_bytes());put(rows,'SOURCE.txt',SOURCE_URL+'\n')
    core=f'Roms/PORTS/Games/{app}/pockettracker';assert payload[core][0]==original[core][0]
    report=archive(name,rows)
    stage=WORK/profile
    for p,(data,mode) in rows.items():
        q=stage/p;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(data);q.chmod(mode)
    return report

def brick():
    update=entries(PRIVATE/'TrackWerk-0.9-Brick-Program-Factory-Update.zip')
    base=entries(ROOT/'outputs/Pocket-Tracker-Extreme-0.2.0-beta.3/Pocket-Tracker-Extreme-0.2.0-beta.3-Brick.zip')
    rows={}
    for p,v in update.items():
        if p.startswith(('factory/','licenses/')) or p=='pockettracker_extreme.aarch64':rows['pockettracker_extreme/'+p]=v
    for p,v in base.items():
        if p.startswith(('pockettracker_extreme/tools/','pockettracker_extreme/fonts/')):rows[p]=v
    # The launch behavior is byte-identical to the proven working launcher;
    # only the existing canonical log version/comment differs.
    put(rows,'TrackWerk.sh',(SOURCE/'shell/brick/Pocket Tracker Extreme.sh').read_bytes(),0o755)
    picture=update['TrackWerk-0.9-Menu-Background.png'][0]
    put(rows,'pockettracker_extreme/menu-cover-trackwerk-0.9.png',picture);put(rows,'pockettracker_extreme/screenshot.png',picture)
    port=json.loads((SOURCE/'shell/brick/port.json').read_text());port['name']='TrackWerk-0.9-Brick-KNULLI.zip';port['items']=['TrackWerk.sh','pockettracker_extreme'];port['attr']['title']='TrackWerk'
    port['attr']['desc']='TrackWerk 0.9 beta by Kid Bunka: native handheld tracker, 17 fonts, 50 themes, synthesis, punch effects, mastering, archive sampling and long-WAV preview/crop. Device acceptance remains in progress; direct iPhone recording is not verified.'
    put(rows,'pockettracker_extreme/port.json',dump(port));put(rows,'pockettracker_extreme/VERSION.txt','TrackWerk 0.9.0 beta\n')
    tree=ET.Element('gameList');g=ET.SubElement(tree,'game')
    for tag,text in [('path','./TrackWerk.sh'),('name','TrackWerk 0.9'),('image','./pockettracker_extreme/menu-cover-trackwerk-0.9.png'),('thumbnail','./pockettracker_extreme/menu-cover-trackwerk-0.9.png'),('developer','TrackWerk / PocketTracker contributors'),('publisher','Kid Bunka'),('releasedate','20261111T000000')]:ET.SubElement(g,tag).text=text
    put(rows,'pockettracker_extreme/gameinfo.xml',ET.tostring(tree,encoding='utf-8',xml_declaration=True))
    guide='''TRACKWERK 0.9 BETA — FIRST INSTALL ON TRIMUI BRICK / KNULLI

Existing tracker installation? Use TrackWerk-0.9-Brick-Program-Factory-Update.zip
instead. Keep your current launcher, helpers, data and controls.

For a NEW tracker installation on an already working original Brick/Hammer
with KNULLI: copy TrackWerk.sh and the pockettracker_extreme folder into
SHARE/roms/ports/. This package does not contain or install KNULLI.
Official device-specific operating system: https://knulli.org/
Keep any working OS card; never format it for an app update.

Expected paths:
roms/ports/TrackWerk.sh
roms/ports/pockettracker_extreme/pockettracker_extreme.aarch64
roms/ports/pockettracker_extreme/factory/Presets/

NEW MENU IMAGE (computer, Python3.9+)
The new app starts as Ports > TrackWerk. To install the supplied artwork and
friendly title without replacing another port's menu, run the included helper:
  python3 setup_brick_menu.py --target /Volumes/SHARE/roms/ports/pockettracker_extreme
Review the plan, then repeat with --apply. Use your actual card path;
on Windows use py and the appropriate drive letter. The helper adds/updates
only this tracker entry, retains the chosen launcher path and backs up XML.
No root gamelist.xml is supplied for blind copying over your other ports.

PREPARE THE FACTORY LIBRARY BEFORE FIRST LAUNCH (optional)
  python3 /Volumes/SHARE/roms/ports/pockettracker_extreme/tools/prepare_presets.py
This verifies and adds only missing presets. Otherwise the proven launcher
prepares them on first run; initial setup can take longer on a slow card.
Wait for copying to finish, safely eject and boot the powered-off Brick.
Refresh the KNULLI game list if needed, then start TrackWerk.

Your data home is roms/ports/pockettracker_extreme/data/.
Put audio/category folders in data/Samples; projects and edited presets remain
in data/Projects and data/Presets. No Android-style folder grant is needed.
Global preferences and controller mappings stay in their existing files.
Large WAVs can be streamed from storage; mark IN/OUT and crop a new excerpt
before editing. Originals are retained. Source Sampler needs Wi-Fi and a
correct clock. Interface/microphone routing requires a device test; direct
iPhone USB sampling failed in the maintainer's setup and is not certified.

This is beta software. Final 0.9 display/audio/controls/network acceptance is
still open. The package retains the proven software-surface startup behavior.
It has no PortMaster startup dependency and does not update system libraries.
'''
    put(rows,'START-HERE.txt',guide);put(rows,'pockettracker_extreme/START-HERE.txt',guide)
    put(rows,'setup_brick_menu.py',(HERE/'setup_brick_menu.py').read_bytes(),0o755)
    put(rows,'SOURCE.txt',SOURCE_URL+'\n')
    for p in ['KNULLI-Official-Download.url','KNULLI-SHA256.txt']:
        if p in base:rows[p]=base[p]
    full=archive('TrackWerk-0.9-Brick-KNULLI.zip',rows)
    # Public updater supports either legacy or fresh TrackWerk launcher names.
    put(update,'update_menu_art.py',(HERE/'setup_brick_menu.py').read_bytes(),0o755)
    put(update,'SOURCE.txt',SOURCE_URL+'\n')
    doc=update['UPDATE-EXISTING-INSTALL.txt'][0].decode().replace('TrackWerk-0.9-Demos-Presets-PRIVATE.zip contains all 50 private demo projects with\nproject media and all 2,359 preset files. Merge its Projects and Presets into the\nexisting data home. Add missing files and retain any differing file you edited.\nThe large personal sample library already on the card is not duplicated or removed.\nThis private music-content pack is separate from the software/source-code release.','TrackWerk-0.9-Factory-Presets.zip contains the 2,304 public synthetic factory\npreset files (2,048 instruments plus 256 effect/master presets). The program\nupdater includes this same immutable factory library. Personal sample libraries\nand private demo songs are not part of the public download. Your existing\nmedia and custom presets remain at their current paths.')
    doc=doc.replace('Publication remains on hold. The former GitHub website is offline pending the\nindependent TrackWerk redesign and review; use the included local guides.','Public release candidate prepared for 11 November 2026. Use the release page\nand included source/licenses. This file does not imply that publication occurred.')
    doc += '\nMENU NAME / ARTWORK\npython3 update_menu_art.py --target "/Volumes/SHARE/roms/ports/pockettracker_extreme" --art "TrackWerk-0.9-Menu-Background.png"\nReview the plan, then repeat with --apply. Existing launcher paths and other\nPorts entries are retained; legacy and TrackWerk.sh fresh-install names work.\n'
    put(update,'UPDATE-EXISTING-INSTALL.txt',doc)
    return full,archive('TrackWerk-0.9-Brick-Program-Factory-Update.zip',update)

def public_source():
    rows=entries(PRIVATE/'TrackWerk-0.9-Source.zip');prefix='TrackWerk-0.9/'
    name=prefix+'font-authoring/clean-atlas-manifest.json';raw=rows[name][0].decode()
    raw=re.sub(r'/Users/[^/"\s]+/Documents/Codex/\d{4}-\d{2}-\d{2}/[^/"\s]+/','<workspace>/',raw)
    raw=re.sub(r'/Users/[^/"\s]+','<workspace>',raw);put(rows,name,raw)
    # Public device bundles wrap their card files in Card-Payload/ so they
    # cannot be blindly merged over a user's music home. Keep SDK rebuilds
    # compatible with that wrapper and the original bare program-only ZIPs.
    for script,app in [('rebuild-miyoo-offline.py','PocketTrackerExtreme'),('rebuild-miyoo-online.py','PocketTrackerExtremePlusArchive')]:
        name=prefix+'build-tools/'+script;data=rows[name][0].decode();member='Roms/PORTS/Games/'+app+'/libs/libSDL2-2.0.so.0'
        old='z.read('+repr(member)+')';new="z.read(('Card-Payload/' if "+repr('Card-Payload/'+member)+" in z.namelist() else '')+"+repr(member)+')'
        assert old in data;put(rows,name,data.replace(old,new),rows[name][1])
    guide=rows[prefix+'BUILD-TRACKWERK-0.9.txt'][0].decode().replace('matching supplied private deviceZIP','matching supplied public Miyoo ZIP').replace('This is a private review package; public hosting remains on hold.','This is the corresponding source for the public 0.9 beta release candidate.\nRelease packaging helpers are included in release-tools/. Engine production\ninputs remain byte-identical to the recorded freeze; authoring metadata paths\nare portable and no personal samples or signing materials are included.')
    put(rows,prefix+'BUILD-TRACKWERK-0.9.txt',guide)
    for p in ['install_miyoo.py','setup_brick_menu.py','build_public_packages.py']:
        put(rows,prefix+'release-tools/'+p,(HERE/p).read_bytes(),0o755)
    # Verify every frozen production input before emitting any public archive.
    freeze=json.loads((ROOT/'work/rebrand-typography/PRODUCTION-SOURCE-FREEZE-0.9-FINAL.json').read_text())
    assert freeze['manifest_sha256']==FREEZE
    for row in freeze['entries']:
        assert sha(rows[prefix+'source/'+row['path']][0])==row['sha256'],row['path']
    manifest={'version':'0.9.0','freeze':FREEZE,'files':[{'path':p[len(prefix):],'bytes':len(data),'sha256':sha(data)} for p,(data,_) in sorted(rows.items()) if p!=prefix+'SOURCE-FILES.json']}
    put(rows,prefix+'SOURCE-FILES.json',dump(manifest))
    report=archive('TrackWerk-0.9-Source.zip',rows);report['frozen_production_files_verified']=len(freeze['entries']);return report

def main():
    OUT.mkdir(parents=True,exist_ok=True);WORK.mkdir(parents=True,exist_ok=True);reports=[]
    factory_rows,factory_manifest,pack_id=factory()
    for name in ['TrackWerk-0.9-Android.apk','TrackWerk-0.9-Android-Private-Test-Update.apk','SDL2-mmiyoo-f6319cf9-source.tar.gz']:
        shutil.copyfile(PRIVATE/name,OUT/name);reports.append({'file':name,'bytes':(OUT/name).stat().st_size,'sha256':sha((OUT/name).read_bytes()),'engine_binary_unchanged':True})
    reports.append(miyoo('TrackWerk-0.9-Miyoo-Mini-Onion.zip','Miyoo-Mini-Offline/TrackWerk-0.9-Miyoo-Mini-Offline-PRIVATE-Update.zip','mini-offline','PocketTrackerExtreme',factory_rows,factory_manifest))
    reports.append(miyoo('TrackWerk-0.9-Miyoo-Plus-Onion.zip','Miyoo-Plus-Online/TrackWerk-0.9-Miyoo-Plus-Online-PRIVATE-Update.zip','plus-online','PocketTrackerExtremePlusArchive',factory_rows,factory_manifest))
    reports.extend(brick())
    presets={'Factory/Presets/'+p:v for p,v in factory_rows.items()};put(presets,'Factory/presets-pack.id',(pack_id+'\n').encode());put(presets,'prepare_presets.py',(SOURCE/'tools/release/prepare_presets.py').read_bytes(),0o755);put(presets,'LICENSE',(SOURCE/'LICENSE').read_bytes());put(presets,'SOURCE.txt',SOURCE_URL+'\n')
    put(presets,'START-HERE.txt','TRACKWERK 0.9 — PUBLIC FACTORY LIBRARY\n2,048 synthetic instrument presets plus 256 effect/master presets.\nNo private samples or owner demo songs.\n\nPython3 optional missing-only preparation:\npython3 prepare_presets.py --factory Factory/Presets --data /path/to/existing/data\nFor Miyoo use the existing PocketTrackerExtreme home on the SD card.\nFor Brick use roms/ports/pockettracker_extreme/data.\nThe helper verifies the factory pack and retains every existing preset.\n')
    reports.append(archive('TrackWerk-0.9-Factory-Presets.zip',presets));reports.append(public_source())
    release={'product':'TrackWerk','version':'0.9.0','tag':TAG,'planned_release_date':'2026-11-11','timezone':'Europe/Zurich','status':'PUBLIC_PACKAGES_PREPARED_NOT_PUBLISHED','production_source_manifest':FREEZE,'assets':reports,'private_samples_and_demos_included':False,'private_card_identifiers_included':False,'publication_performed':False}
    (OUT/'PUBLIC-RELEASE-MANIFEST.json').write_bytes(dump(release));(OUT/'SHA256SUMS.txt').write_text('\n'.join(r['sha256']+'  '+r['file'] for r in reports)+'\n')
    print(json.dumps({'status':release['status'],'assets':reports},indent=2))

if __name__=='__main__':main()
