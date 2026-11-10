#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pixel masters -> static native atlases. Existing font_ui contributes METRICS ONLY."""
from pathlib import Path
import argparse,hashlib,json,re,unicodedata,subprocess,sys
from PIL import Image,ImageDraw
R=Path(__file__).resolve().parent;SOURCE=R.parent/'source';OUT=R/'previews'
CPS=list(range(32,127))+list(range(160,256))+[0x2026]

def metrics():
    text=(SOURCE/'native/ui/font_ui.h').read_text();out={}
    for size in [12,18]:
        body=re.search(r'uiFont'+str(size)+r'Glyphs\[192\]=\{(.*?)\};',text,re.S).group(1)
        rows=[tuple(map(int,m)) for m in re.findall(r'\{(\d+),(\d+),(\d+),(\d+)\}',body)]
        height=int(re.search(r'uiFont'+str(size)+r'Height=(\d+)',text).group(1))
        count=int(re.search(r'uiFont'+str(size)+r'Pixels\[(\d+)\]',text).group(1))
        assert len(rows)==192 and height in [14,21]
        assert all(a[0]+a[1]*height==b[0] for a,b in zip(rows,rows[1:]))
        assert rows[-1][0]+rows[-1][1]*height==count
        out[size]={'glyphs':rows,'height':height,'pixel_size':count}
    return out

def img(rows):
    im=Image.new('L',(len(rows[0]),len(rows)))
    im.putdata([255 if v=='1' else 0 for row in rows for v in row]);return im

def make_extra(cp,glyphs):
    # Original non-ASCII constructions; accented Latin1 letters are composed below.
    base=Image.new('L',(7,11));d=ImageDraw.Draw(base)
    def line(points):d.line(points,fill=255,width=1)
    if cp in (160,):return base
    if cp==0x2026:
        for x in [0,3,6]:d.point((x,8),fill=255)
    elif cp==0xdf:line([(0,8),(0,2),(1,0),(4,0),(5,1),(5,3),(3,4),(5,5),(6,6),(6,7),(5,8),(3,8)])
    elif cp in (0xc6,0xe6):
        a=glyphs[ord('A' if cp==0xc6 else 'a')];e=glyphs[ord('E' if cp==0xc6 else 'e')];base=Image.new('L',(13,11));base.paste(a,(0,0));base.paste(e,(6,0))
    elif cp in (0xd0,0xf0):
        base=glyphs[ord('D' if cp==0xd0 else 'd')].copy();d=ImageDraw.Draw(base);d.line([(0,4),(4,4)] if cp==0xd0 else [(2,0),(5,2)],fill=255)
    elif cp in (0xde,0xfe):
        line([(0,0),(0,10)]);line([(0,3),(5,3),(6,4),(6,6),(5,7),(0,7)])
    elif cp in (0xd8,0xf8):
        base=glyphs[ord('O' if cp==0xd8 else 'o')].copy();ImageDraw.Draw(base).line([(0,8),(6,0 if cp==0xd8 else 3)],fill=255)
    elif cp==0xa1:line([(3,3),(3,8)]);line([(3,0),(3,0)])
    elif cp==0xbf:line([(3,0),(3,0)]);line([(3,2),(3,3),(0,5),(0,7),(1,8),(5,8),(6,7)])
    elif cp==0xa2:line([(5,3),(1,3),(0,4),(0,7),(1,8),(5,8)]);line([(3,1),(3,10)])
    elif cp==0xa3:line([(5,1),(4,0),(2,0),(1,1),(1,8),(6,8)]);line([(0,4),(4,4)])
    elif cp==0xa4:line([(1,2),(5,2),(5,6),(1,6),(1,2)]);line([(0,1),(1,2)]);line([(5,2),(6,1)]);line([(0,7),(1,6)]);line([(5,6),(6,7)])
    elif cp==0xa5:line([(0,0),(3,4),(6,0)]);line([(3,4),(3,8)]);line([(0,5),(6,5)]);line([(0,7),(6,7)])
    elif cp==0xa6:line([(3,0),(3,3)]);line([(3,5),(3,8)])
    elif cp==0xa7:line([(6,1),(1,0),(0,2),(6,6),(5,8),(0,7)]);line([(1,3),(0,4),(1,5),(5,5),(6,4),(5,3),(1,3)])
    elif cp==0xa8:line([(1,1),(1,1)]);line([(5,1),(5,1)])
    elif cp in (0xa9,0xae):
        line([(2,0),(4,0),(6,2),(6,6),(4,8),(2,8),(0,6),(0,2),(2,0)])
        if cp==0xa9:line([(4,2),(2,2),(2,6),(4,6)])
        else:line([(2,6),(2,2),(4,2),(4,4),(2,4),(4,6)])
    elif cp in (0xaa,0xba):
        a=glyphs[ord('a' if cp==0xaa else 'o')].crop((0,3,7,9)).resize((5,4),Image.Resampling.NEAREST);base.paste(a,(1,0));line([(0,5),(6,5)])
    elif cp in (0xab,0xbb):
        for x in [0,3]:line([(x+2,2),(x,4),(x+2,6)] if cp==0xab else [(x,2),(x+2,4),(x,6)])
    elif cp==0xac:line([(0,4),(6,4),(6,7)])
    elif cp==0xad:line([(1,4),(5,4)])
    elif cp==0xaf:line([(0,0),(6,0)])
    elif cp==0xb0:line([(2,0),(4,0),(5,1),(5,3),(4,4),(2,4),(1,3),(1,1),(2,0)])
    elif cp==0xb1:line([(0,3),(6,3)]);line([(3,0),(3,6)]);line([(0,8),(6,8)])
    elif cp in (0xb2,0xb3,0xb9):
        base.paste(glyphs[ord({0xb2:'2',0xb3:'3',0xb9:'1'}[cp])].crop((0,0,7,9)).resize((4,5),Image.Resampling.NEAREST),(1,0))
    elif cp==0xb4:line([(2,2),(4,0)])
    elif cp==0xb5:
        base=glyphs[ord('u')].copy();ImageDraw.Draw(base).line([(0,3),(0,10)],fill=255)
    elif cp==0xb6:line([(4,8),(4,0),(1,0),(0,1),(0,3),(1,4),(4,4)]);line([(6,0),(6,8)])
    elif cp==0xb7:line([(3,4),(3,4)])
    elif cp==0xb8:line([(3,8),(4,9),(2,10)])
    elif cp in (0xbc,0xbd,0xbe):
        base=Image.new('L',(11,11));d=ImageDraw.Draw(base)
        n,den=('3','4') if cp==0xbe else ('1','2' if cp==0xbd else '4')
        base.paste(glyphs[ord(n)].crop((0,0,7,9)).resize((4,5),Image.Resampling.NEAREST),(0,0));base.paste(glyphs[ord(den)].crop((0,0,7,9)).resize((4,5),Image.Resampling.NEAREST),(7,6));d.line([(2,9),(8,1)],fill=255)
    elif cp==0xd7:line([(0,2),(6,8)]);line([(6,2),(0,8)])
    elif cp==0xf7:line([(0,4),(6,4)]);line([(3,1),(3,1)]);line([(3,7),(3,7)])
    else:raise ValueError('Unimplemented original Latin1 shape '+hex(cp))
    return base

def base_for(cp,glyphs):
    if cp in glyphs:return glyphs[cp],[]
    nfd=unicodedata.normalize('NFD',chr(cp))
    if len(nfd)>1 and ord(nfd[0]) in glyphs:
        base=glyphs[ord(nfd[0])].copy()
        if nfd[0] in 'ij':ImageDraw.Draw(base).rectangle((0,0,6,2),fill=0)
        return base,list(nfd[1:])
    return make_extra(cp,glyphs),[]

def label_glyph(cp,glyphs,width,height):
    out=Image.new('L',(max(width,1),height));base,accents=base_for(cp,glyphs)
    if width==0:return b''
    bounds=base.getbbox()
    if not bounds:return bytes(width*height)
    # Only trim horizontally. Vertical master coordinates preserve x-height/descenders.
    x0,_,x1,_=bounds;body=base.crop((x0,0,x1,11))
    top=2 if height==14 else 3;body_height=11 if height==14 else 17
    # Deterministic nearest-pixel hinting; no scanline/shadow overlay or temporal noise.
    out.paste(body.resize((width,body_height),Image.Resampling.NEAREST),(0,top))
    if accents:
        d=ImageDraw.Draw(out);box=out.getbbox();first=box[1] if box else top
        markheight=2 if height==14 else 3;y=max(0,first-markheight-1)
        for mark in accents:
            if mark=='\u0308':
                for x in {max(0,width//4),min(width-1,3*width//4)}:d.rectangle((x,y,x,y+(1 if height==21 else 0)),fill=255)
            elif mark=='\u0301':d.line([(max(0,width//2-1),y+markheight-1),(min(width-1,width//2+1),y)],fill=255)
            elif mark=='\u0300':d.line([(max(0,width//2-1),y),(min(width-1,width//2+1),y+markheight-1)],fill=255)
            elif mark=='\u0302':d.line([(max(0,width//2-2),y+markheight-1),(width//2,y),(min(width-1,width//2+2),y+markheight-1)],fill=255)
            elif mark=='\u0303':d.line([(0,y+1),(width//3,y),(2*width//3,y+1),(width-1,y)],fill=255)
            elif mark=='\u030a':d.rectangle((max(0,width//2-1),y,min(width-1,width//2+1),y+markheight-1),outline=255)
            elif mark=='\u0327':d.line([(width//2,min(height-3,box[3]-1)),(min(width-1,width//2+1),height-2),(max(0,width//2-1),height-1)],fill=255)
            else:raise ValueError('Unknown accent '+repr(mark))
    return out.tobytes()

def array(name,shape,styles):
    # Hex is fixed-width and leaves generated source reasonably reviewable.
    dims=''.join('['+str(n)+']' for n in shape);lines=['inline constexpr uint8_t '+name+dims+'={']
    for data in styles:
        lines.append('{')
        for i in range(0,len(data),32):lines.append(','.join(str(v) for v in data[i:i+32])+',')
        lines.append('},')
    lines.append('};');return '\n'.join(lines)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');a=parser.parse_args()
    masters=json.loads((R/'pixel-masters.json').read_text());m=metrics();OUT.mkdir(exist_ok=True)
    grids={2:[],3:[]};labels={12:[],18:[]};proof=[]
    for style in masters['styles']:
        glyphs={int(cp):img(rows) for cp,rows in style['glyphs'].items()}
        for scale,w,h in [(2,10,11),(3,15,17)]:
            data=bytearray()
            for cp in range(32,127):
                canvas=Image.new('L',(w,h));ink=glyphs[cp].resize((w-1,h),Image.Resampling.NEAREST);canvas.paste(ink,(0,0));data.extend(canvas.tobytes())
            grids[scale].append(bytes(data))
        for size in [12,18]:
            atlas=bytearray(m[size]['pixel_size'])
            for cp,(offset,width,_,_) in zip(CPS,m[size]['glyphs']):
                value=label_glyph(cp,glyphs,width,m[size]['height']);atlas[offset:offset+len(value)]=value
            labels[size].append(bytes(atlas))
        # A literal pixel-master sheet remains editable and inspectable beside JSON.
        sheet=Image.new('RGB',(16*9,6*14),(8,14,24));draw=ImageDraw.Draw(sheet)
        for j,cp in enumerate(range(32,127)):
            ink=glyphs[cp];tile=Image.new('RGB',ink.size,(215,244,231));sheet.paste(tile,((j%16)*9,(j//16)*14),ink)
        sheet.resize((576,336),Image.Resampling.NEAREST).save(OUT/(style['name']+'-pixel-masters.png'))
        proof.append({'id':style['id'],'name':style['name'],'grid2_sha256':hashlib.sha256(grids[2][-1]).hexdigest(),'grid3_sha256':hashlib.sha256(grids[3][-1]).hexdigest(),'label12_sha256':hashlib.sha256(labels[12][-1]).hexdigest(),'label18_sha256':hashlib.sha256(labels[18][-1]).hexdigest()})
    # The first six faces are frozen. Appended cleaner alphabets have their own
    # optical label hinting; reuse no raster from the classic project font.
    frozen=json.loads((R/'frozen-six-atlases.json').read_text())
    assert proof==frozen['styles'],'Original six glyph atlases changed'
    assert hashlib.sha256((R/'pixel-masters.json').read_bytes()).hexdigest()==frozen['pixel_masters_sha256']
    subprocess.run([sys.executable,str(R/'clean-generate.py')],check=True)
    clean=json.loads((R/'clean-atlas-manifest.json').read_text())
    assert clean['ok'] and [s['id'] for s in clean['styles']]==list(range(7,17))
    shapes=[('grid2',grids[2],95*110),('grid3',grids[3],95*255),
            ('label12',labels[12],m[12]['pixel_size']),('label18',labels[18],m[18]['pixel_size'])]
    for name,arrays,length in shapes:
        data=(R/('clean-atlas-'+name+'.bin')).read_bytes()
        assert len(data)==10*length,(name,len(data))
        for i,style in enumerate(clean['styles']):
            atlas=data[i*length:(i+1)*length]
            assert hashlib.sha256(atlas).hexdigest()==style[name+'_sha256'],(name,style['id'])
            arrays.append(atlas)
    proof.extend(clean['styles']);count=len(proof);assert count==16
    head='''// SPDX-License-Identifier: GPL-3.0-or-later
// Original project pixel alphabets, Kid Bunka / project contributors.
// Generated from original pixel-masters.json and clean-masters.json.
// Reproduce with font-authoring/generate_atlases.py (runs clean-generate.py).
// Existing font_ui supplies metrics only. No font/ROM raster has been copied.
// Static alpha atlases: no allocations, timers, drawing or audio-thread work.
#pragma once
#include <cstdint>
#include "font_ui.h"
namespace pt::ui::detail {
'''
    for scale,w,h in [(2,10,11),(3,15,17)]:head+=array('demoGrid'+str(scale),[count,95*w*h],grids[scale])+'\n'
    for size in [12,18]:head+=array('demoLabel'+str(size),[count,m[size]['pixel_size']],labels[size])+'\n'
    total=sum(len(d) for arrays in list(grids.values())+list(labels.values()) for d in arrays)
    head+='''inline constexpr unsigned demoFontAtlasBytes = sizeof(demoGrid2)+sizeof(demoGrid3)+sizeof(demoLabel12)+sizeof(demoLabel18);
static_assert(demoFontAtlasBytes < 1536u*1024u, "Keep all sixteen typefaces below 1.5 MiB");
static_assert(sizeof(demoLabel12[0])==sizeof(uiFont12Pixels) && sizeof(demoLabel18[0])==sizeof(uiFont18Pixels), "Label atlas layout changed");
inline const uint8_t* demo_grid_pixels(int style,int font_scale,uint32_t cp) {
    if(style<1 || style>16 || cp<32 || cp>126)return nullptr;
    if(font_scale==2)return demoGrid2[style-1]+(cp-32)*110u;
    if(font_scale==3)return demoGrid3[style-1]+(cp-32)*255u;
    return nullptr;
}
inline const uint8_t* demo_label_pixels(int style,int size) {
    if(style<1 || style>16)return nullptr;
    return size>=15?demoLabel18[style-1]:demoLabel12[style-1];
}
} // namespace pt::ui::detail
'''
    target=SOURCE/'native/ui/demo_fonts.h'
    if a.check:assert target.read_text()==head,'Generated header differs from masters/metrics'
    else:
        temporary=target.with_suffix('.h.tmp');temporary.write_text(head);temporary.replace(target)
    report={'styles':proof,'atlas_bytes':total,'budget_bytes':1572864,'frozen_six_byte_exact':True,'grid_cells':{'scale2':[10,11],'scale3':[15,17]},'label_metrics':{str(k):{'height':v['height'],'pixel_size':v['pixel_size'],'glyphs':192} for k,v in m.items()},'original_raster_font_input_used':False,'layout_metrics_input_sha256':hashlib.sha256(json.dumps(m,sort_keys=True).encode()).hexdigest()}
    (R/'atlas-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'generated':str(target),'atlas_bytes':total,'styles':count,'metrics_preserved':True,'frozen_six_byte_exact':True}))
if __name__=='__main__':main()
