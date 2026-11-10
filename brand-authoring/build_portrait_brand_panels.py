#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create complete original portrait branding panels; retain skin IDs and exact geometry.
No legacy bitmap pixels or font file are inputs. Only vector/pixel project masters.
"""
from pathlib import Path
import hashlib,importlib.util,json
from PIL import Image
R=Path(__file__).resolve().parent;S=R.parent/'source';spec=importlib.util.spec_from_file_location('original_art',R/'build_studies.py');art=importlib.util.module_from_spec(spec);spec.loader.exec_module(art)
rows=[]
for skin,base,rule,muted,ink in [('amiga','#D9CFBC','#BCAF99','#685F54','#26354A'),('amiga-2','#34424F','#1A2637','#A6B4C5','#63E7EF')]:
 scene=art.Scene(1080,181,base,'TrackWerk 0.9 / original portrait branding panel')
 b=art.rgb(base)
 for y in range(181):
  factor=1.0 if y<128 else .64+.32*(y-128)/53
  color='#'+''.join(f'{max(0,min(255,round(v*factor))):02X}' for v in b)
  scene.rect(0,y,1080,1,color)
 for x in range(0,1080,24):scene.rect(x,0,1,127,rule)
 for y in range(12,128,12):scene.rect(0,y,1080,1,rule)
 scene.rect(0,0,1080,2,'#EFE6D6' if skin=='amiga' else '#607586');scene.rect(0,127,1080,2,'#80735F' if skin=='amiga' else '#101A28')
 art.mark(scene,'B',20,16,98)
 if skin=='amiga':art.word(scene,'TRACKWERK',154,33,8.8,4,ink,.8,.18)
 else:art.logo(scene,'B',154,33,8.8)
 art.word(scene,'KID BUNKA',856,27,1.9,1,muted)
 art.word(scene,'0.9',906,66,3.4,1,ink)
 art.word(scene,'MUSIC / CODE / RHYTHM / CHROME',303,151,1.6,1,muted)
 out=S/f'app/src/main/assets/themes/{skin}/bg_branding_panel.png'
 im=scene.im.resize((1080,181),Image.Resampling.LANCZOS);im.save(out)
 rows.append(dict(skin_id=skin,path=str(out.relative_to(S)),size=[1080,181],sha256=hashlib.sha256(out.read_bytes()).hexdigest(),button_assets_and_positions_unchanged=True))
(R/'PORTRAIT-BRAND-PANELS.json').write_text(json.dumps(dict(original_new_art=True,legacy_pixel_input=False,assets=rows),indent=2)+'\n');print('Created two original branding panels; skin selection/layout IDs retained.')
