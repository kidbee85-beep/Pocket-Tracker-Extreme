#!/usr/bin/env python3
"""Rasterize TrackWerk's own editable TW geometry for the two native help panels.

The legacy internal sprite names and dimensions remain ABI/layout compatible;
the original PocketTracker mascot artwork is not used in either bitmap.
"""
from pathlib import Path
import hashlib, json
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent/'source'
geometry = json.loads((HERE/'TrackWerk-Mark-Geometry.json').read_text())['marks']['B']

def render(size):
    im = Image.new('1',(size,size),0)
    d = ImageDraw.Draw(im)
    pad = size/16
    scale = (size-2*pad)/32
    point = lambda xy: tuple(round(pad+v*scale) for v in xy)
    for color, kind, coords in geometry:
        fill = 0 if color == 'erase' else 1
        if kind == 'poly':
            d.polygon([point(v) for v in coords],fill=fill)
        elif kind == 'rect':
            x,y,w,h = coords
            d.rectangle([point((x,y)),point((x+w,y+h))],fill=fill)
        else:
            raise ValueError(kind)
    return im

records = []
for size, file, const, prefix, fn in [
    (64,'mascot_sprite.h','MASCOT_NEUTRAL','MASCOT','mascot_pixel'),
    (128,'mascot_sprite_large.h','MASCOT_LARGE','MASCOT_LARGE','mascot_large_pixel')]:
    im = render(size)
    data = im.tobytes()
    assert len(data) == size*size//8
    rows = ['    '+', '.join(f'0x{x:02X}' for x in data[y*size//8:(y+1)*size//8])+',' for y in range(size)]
    args = 'const uint8_t* sprite, int x, int y' if size == 64 else 'int x, int y'
    array = 'sprite' if size == 64 else const
    code = f'''#pragma once
// Original TrackWerk TW help mark, generated from TrackWerk-Mark-Geometry.json.
// Run sibling brand-authoring/generate_help_marks.py to regenerate. MSB first.
// Internal symbol names/dimensions retained solely for existing help layout.
#include <cstdint>
namespace pt::ui {{
inline constexpr int {prefix}_W = {size};
inline constexpr int {prefix}_H = {size};
inline constexpr uint8_t {const}[{prefix}_H * ({prefix}_W / 8)] = {{
{chr(10).join(rows)}
}};
inline constexpr bool {fn}({args}) {{
    if (x < 0 || y < 0 || x >= {prefix}_W || y >= {prefix}_H) return false;
    return ({array}[y * ({prefix}_W / 8) + (x >> 3)] & (0x80u >> (x & 7))) != 0;
}}
}} // namespace pt::ui
'''
    target = SOURCE/'native/ui'/file
    target.write_text(code)
    im.convert('RGB').save(HERE/f'TrackWerk-Help-Mark-{size}.png')
    records.append({'file':str(target.relative_to(SOURCE)),'dimensions':[size,size],
                    'bitmap_bytes':len(data),'lit_pixels':sum(v!=0 for v in im.getdata()),
                    'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
(HERE/'TRACKWERK-HELP-MARKS.json').write_text(json.dumps({'original_geometry':'TrackWerk-Mark-Geometry.json / B',
    'original_pockettracker_mascot_used':False,'assets':records},indent=2)+'\n')
print(json.dumps(records,indent=2))
