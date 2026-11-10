#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original vector geometry and original display masters -> fixed native artwork."""
from pathlib import Path
import argparse,json
from PIL import Image,ImageDraw
R=Path(__file__).resolve().parent
m=json.loads((R.parent/'font-authoring/pixel-masters.json').read_text());glyphs=next(s['glyphs'] for s in m['styles'] if s['id']==4)
g=json.loads((R/'TrackWerk-Mark-Geometry.json').read_text())['marks']['B'];masks=[]
for layer in ['main','second']:
 im=Image.new('1',(32,32));d=ImageDraw.Draw(im)
 for l,op,args in g:
  if l==layer:
   assert op=='poly';d.polygon([tuple(v) for v in args],fill=1)
 masks.append([sum(1<<(31-x) for x in range(32) if im.getpixel((x,y))) for y in range(32)])
s='// SPDX-License-Identifier: GPL-3.0-or-later\n// Original TrackWerk B geometry and lettering; fixed data, no linked font.\n#pragma once\n#include <cstdint>\nnamespace pt::ui::brand::art {\ninline constexpr uint32_t markLayers[2][32]={\n'
for rows in masks:s+='{'+','.join('0x%08xu'%v for v in rows)+'},\n'
s+='};\ninline constexpr uint8_t wordmarkRows[9][9]={\n'
for ch in 'TRACKWERK':s+='{'+','.join(str(int(row,2)) for row in glyphs[str(ord(ch))][:9])+'}, // '+ch+'\n'
s+='};\n}\n';p=R.parent/'source/native/ui/trackwerk_art.h'
a=argparse.ArgumentParser();a.add_argument('--check',action='store_true');args=a.parse_args()
if args.check:assert p.read_text()==s,'Native artwork differs from preferred sources'
else:p.write_text(s)
print('Native artwork matches original preferred sources.')
