#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reproduce the authored 256-preset expansion; never modifies the instrument pack.

The reviewed numeric settings live in effects_catalog.json. No randomness, DSP
algorithm changes, user folders or installation writes are involved.
"""
import argparse,hashlib,json,re
from pathlib import Path
S=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');a=p.parse_args()
c=json.loads((Path(__file__).parent/'effects_catalog.json').read_text())
assert c['format']==1
expected={'reverbs':64,'delays':64,'studio_fx':96,'masters':32}
outputs={};files=[]
def put(path,data):outputs[S/path]=data.encode()
def quote(v):return json.dumps(v,ensure_ascii=True)
def label(v):return v.split('_',1)[-1].replace('_',' ')
for kind,count in expected.items():
 rows=c[kind];assert len(rows)==count
 names=set();params=set()
 for r in rows:
  assert r['name'] not in names;names.add(r['name'])
  assert re.fullmatch(r'[A-Za-z0-9_ /-]+',r['name'])
  keys=('algo','decay','size','damp','pre','width','mod') if kind=='reverbs' else ('pong','sync','time','feedback','tone','wobble','reverbSend') if kind=='delays' else ('master',)
  signature=tuple(tuple(r[k]) if isinstance(r[k],list) else r[k] for k in keys)
  assert signature not in params,(kind,r['name']);params.add(signature)
  if kind=='reverbs':
   assert 0<=r['algo']<5 and 0<=r['decay']<=218
   if r['algo'] in [3,4]:assert r['size']==r['mod']==0
  elif kind=='delays':assert 0<=r['feedback']<224 and 0<=r['time']<=(11 if r['sync'] else 255)
  else:assert len(r['master'])==15 and all(isinstance(v,int) and 0<=v<=255 for v in r['master']) and r['master'][0]==1
  cat=r['category'];stem=r['name'].replace(' ','_').replace('/','_')
  if kind=='reverbs':folder='Reverbs';ext='ptr';payload={k:r[k] for k in ('name','algo','size','decay','damp','pre','width','mod')}
  elif kind=='delays':folder='Delays';ext='ptd';payload={k:r[k] for k in ('name','pong','sync','time','feedback','tone','wobble','reverbSend')}
  else:folder='StudioFX' if kind=='studio_fx' else 'Mastering';ext='ptfx' if kind=='studio_fx' else 'ptmaster';payload={'version':1,'kind':'effects' if kind=='studio_fx' else 'master','name':r['name'],'master':r['master']}
  payload.update(category=label(cat),description=r['description'])
  path=Path('factory/Presets')/folder/'Extreme_Collection'/cat/(stem+'.'+ext)
  put(path,json.dumps(payload,indent=2)+'\n');files.append({'path':path.relative_to('factory').as_posix(),'kind':kind,'name':r['name'],'category':label(cat),'sha256':hashlib.sha256(outputs[S/path]).hexdigest()})
for kind,filename in [('reverbs','reverb-presets-extra.inc'),('delays','delay-presets-extra.inc')]:
 lines=['// SPDX-License-Identifier: GPL-3.0-or-later','// Generated from tools/presets/effects_catalog.json; append-only factory expansion.']
 for r in c[kind]:
  keys=('algo','decay','size','damp','pre','width','mod') if kind=='reverbs' else ('pong','sync','time','feedback','tone','wobble','reverbSend')
  values=[quote(r['name'])]+[str(r[k]).lower() for k in keys]+[quote(label(r['category'])),quote(r['description'])]
  lines.append('    {'+', '.join(values)+'},')
 put(Path('native/effects/modules')/filename,'\n'.join(lines)+'\n')
lines=['// SPDX-License-Identifier: GPL-3.0-or-later','// Generated from tools/presets/effects_catalog.json. Existing factory IDs are never reordered.','#pragma once','#include "rack.h"','namespace studio::catalog {','struct NamedPreset { const char* name; const char* category; const char* description; std::array<int,COUNT> values; };']
for kind,array in [('studio_fx','effects'),('masters','masters')]:
 lines.append('inline constexpr NamedPreset '+array+'[] = {')
 for r in c[kind]:lines.append('    {'+', '.join([quote(r['name']),quote(label(r['category'])),quote(r['description']),'{'+','.join(map(str,r['master']))+'}'])+'},')
 lines.append('};')
lines+=['inline constexpr int effectCount=sizeof(effects)/sizeof(effects[0]);','inline constexpr int masterCount=sizeof(masters)/sizeof(masters[0]);','} // namespace studio::catalog']
put(Path('native/studio/preset_catalog.h'),'\n'.join(lines)+'\n')
manifest={'format':1,'creator':c['creator'],'license':c['license'],'new_presets':256,'counts':expected,'native_totals':{'send_reverb':84,'send_delay':84,'studio_fx':256,'mastering':64},'files':files}
put(Path('factory/effect-catalog.json'),json.dumps(manifest,indent=2)+'\n')
for path,data in outputs.items():
 if a.check:
  assert path.is_file() and path.read_bytes()==data,'Outdated generated file: '+str(path.relative_to(S))
 else:path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
print(('Verified' if a.check else 'Wrote')+f' {len(files)} new categorized presets and 4 generated catalogs; existing presets untouched.')
