#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Three original TrackWerk vector studies. No TTF, external image or old logo input."""
from pathlib import Path
import hashlib,html,json,math,os,xml.etree.ElementTree as ET
from PIL import Image,ImageDraw

R=Path(__file__).resolve().parent;ROOT=R.parents[2]
OUT=Path(os.environ.get('TRACKWERK_DESIGN_OUTPUT',str(R.parent/'design-review')))
MASTERS=json.loads((R.parent/'font-authoring/pixel-masters.json').read_text())
FONTS={s['id']:s['glyphs'] for s in MASTERS['styles']}
SS=4
CONCEPTS={
 'A':{'name':'STENCIL / INDUSTRIAL','bg':'#121716','panel':'#1B2420','ink':'#DDF449','muted':'#93A393','second':'#E6EAD8','font':5,'slant':0.,'spacing':.85,
      'description':'CNC GATES / OFFSET STEMS / ACID INK','tag':'TRACKER / SYNTH / SAMPLE'},
 'B':{'name':'RASTER / CRACKTRO','bg':'#0B0D20','panel':'#151A32','ink':'#63E7EF','muted':'#939AB5','second':'#EF61CE','font':4,'slant':.18,'spacing':.8,
      'description':'SWEPT SPURS / THREE INK BANDS / RASTER','tag':'CODE / RHYTHM / CHROME'},
 'C':{'name':'NULL / TRANSMISSION','bg':'#11151C','panel':'#1B222D','ink':'#ECE9D8','muted':'#9A9FA9','second':'#FF7957','font':3,'slant':0.,'spacing':1.,
      'description':'BROKEN CARRIER / ANGULAR FORMS / SIGNAL','tag':'CUT / ASSEMBLE / PLAY'}}

def rgb(color):return tuple(int(color[k:k+2],16) for k in (1,3,5))
class Scene:
 def __init__(self,w,h,bg=None,title='TrackWerk original vector study'):
  self.w=w;self.h=h;self.svg=[];self.im=Image.new('RGBA',(w*SS,h*SS),(0,0,0,0));self.d=ImageDraw.Draw(self.im)
  self.title=title
  if bg:self.rect(0,0,w,h,bg)
 def rect(self,x,y,w,h,color):
  if w<=0 or h<=0:return
  self.svg.append(f'<path fill="{color}" d="M{x:g},{y:g}h{w:g}v{h:g}h{-w:g}Z"/>')
  self.d.rectangle((round(x*SS),round(y*SS),round((x+w)*SS)-1,round((y+h)*SS)-1),fill=rgb(color)+(255,))
 def poly(self,points,color):
  self.svg.append('<path fill="'+color+'" d="M'+'L'.join(f'{x:g},{y:g}' for x,y in points)+'Z"/>')
  self.d.polygon([(round(x*SS),round(y*SS)) for x,y in points],fill=rgb(color)+(255,))
 def save(self,stem):
  text='<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="'+str(self.w)+'" height="'+str(self.h)+'" viewBox="0 0 '+str(self.w)+' '+str(self.h)+'" role="img"><title>'+html.escape(self.title)+'</title><metadata>Original TrackWerk study, Kid Bunka / project contributors. GPL-3.0-or-later. Private review; not released. All lettering uses original vector paths; no linked font.</metadata>'+''.join(self.svg)+'</svg>\n'
  (OUT/(stem+'.svg')).write_text(text)
  self.im.resize((self.w,self.h),Image.Resampling.LANCZOS).save(OUT/(stem+'.png'))

def word(scene,text,x,y,scale,font=1,color='#E8EFEA',spacing=1.,slant=0.,bands=None,broken=False):
 cursor=x
 for ch in text:
  rows=FONTS[font][str(ord(ch))]
  for yy,row in enumerate(rows):
   xx=0
   while xx<len(row):
    if row[xx]=='0':xx+=1;continue
    end=xx+1
    while end<len(row) and row[end]=='1':end+=1
    if True:
     ink=bands(yy) if bands else color
     shift=(.22 if broken and yy==4 else 0.)
     yt=yy+(.18 if broken and yy==5 else 0.)
     p=[(cursor+(xx+slant*(8-yy)+shift)*scale,y+yt*scale),
        (cursor+(end+slant*(8-yy)+shift)*scale,y+yt*scale),
        (cursor+(end+slant*(7-yy)+shift)*scale,y+(yy+1)*scale),
        (cursor+(xx+slant*(7-yy)+shift)*scale,y+(yy+1)*scale)]
     scene.poly(p,ink)
    xx=end
  cursor+=(7+spacing)*scale
 return cursor-x-spacing*scale

def geometry(which):
 # Original normalized32x32 compositions; all shapes are simple native-draw candidates.
 if which=='A':return [
  ('main','rect',[2,3,17,4]),('main','rect',[8,7,4,22]),
  ('main','poly',[(14,9),(18,9),(20,21),(24,15),(27,21),(29,9),(32,9),(29,29),(26,29),(23,23),(20,29),(17,29)]),
  ('erase','rect',[7,14,6,1.5])]
 if which=='B':return [
  ('main','poly',[(2,3),(30,3),(29,8),(19,8),(17,18),(12,18),(14,8),(1,8)]),
  ('second','poly',[(2,14),(7,14),(10,25),(16,18),(21,25),(26,14),(31,14),(25,30),(20,30),(16,25),(11,30),(6,30)])]
 return [
  ('main','poly',[(1,4),(6,4),(10,21),(16,12),(22,21),(27,4),(32,4),(25,30),(21,30),(16,23),(11,30),(7,30)]),
  ('erase','rect',[0,14,32,2]),
  ('second','poly',[(5,17),(8,17),(9,21),(6,21)]),
  ('second','poly',[(24,17),(28,17),(27,21),(23,21)])]

def mark(scene,which,x,y,size,framed=True,mono=False):
 p=CONCEPTS[which];bg=p['bg'];s=size/32
 if framed:
  scene.poly([(x+size*.12,y),(x+size*.88,y),(x+size,y+size*.12),(x+size,y+size*.88),(x+size*.88,y+size),(x+size*.12,y+size),(x,y+size*.88),(x,y+size*.12)],p['panel'])
  # Keep a restrained corner datum; identity lives in the original TW/W silhouette.
  scene.rect(x+size*.12,y+size*.07,size*.23,max(1,size*.015),p['ink'])
  inset=size*.16;x+=inset;y+=inset;s=(size-2*inset)/32
 for layer,op,args in geometry(which):
  color=bg if layer=='erase' else p['ink'] if layer=='main' or mono else p['second']
  if op=='rect':scene.rect(x+args[0]*s,y+args[1]*s,args[2]*s,args[3]*s,color)
  else:scene.poly([(x+xx*s,y+yy*s) for xx,yy in args],color)

def logo(scene,which,x,y,scale,mono=False):
 p=CONCEPTS[which]
 bands=(lambda yy:p['ink'] if yy<3 else '#ECF1E7' if yy<5 else p['second']) if which=='B' and not mono else None
 if which=='C' and not mono:bands=lambda yy:p['second'] if yy==4 else p['ink']
 return word(scene,'TRACKWERK',x,y,scale,p['font'],p['ink'],p['spacing'],p['slant'],bands,which=='C')

def backdrop(scene,p):
 for y in range(28,900,28):scene.rect(0,y,1600,1,'#1C252A' if p is CONCEPTS['A'] else '#1B2031')
 for x in range(28,1600,56):scene.rect(x,0,1,900,'#172026' if p is CONCEPTS['A'] else '#171C28')
 # An original mechanical route, deliberately sparse around the wordmark.
 scene.rect(52,122,1496,2,p['muted']);scene.rect(52,806,1496,1,p['muted'])

def board(which):
 p=CONCEPTS[which];s=Scene(1600,900,p['bg'],f'TrackWerk {which} — '+p['name'])
 backdrop(s,p);word(s,'TRACKWERK / MARK STUDIES',56,42,2,1,p['ink']);word(s,which+' / '+p['name'],56,84,1.5,1,p['muted'])
 mark(s,which,88,192,300);word(s,'APP MARK',105,530,1.6,1,p['muted'])
 logo(s,which,490,240,12);word(s,p['tag'],494,392,2,1,p['muted']);s.rect(494,444,930,2,p['ink'])
 word(s,p['description'],494,474,1.5,1,p['muted'])
 word(s,'SMALL / 64 PX',494,586,1.5,1,p['ink']);mark(s,which,494,626,64)
 word(s,'ONE INK',690,586,1.5,1,p['muted']);mark(s,which,690,626,96,mono=True)
 word(s,'LETTERFORMS',910,586,1.5,1,p['muted']);logo(s,which,910,640,4,mono=True)
 word(s,'ORIGINAL GLYPHS + GEOMETRY / NO LINKED FONT',56,836,1.5,1,p['muted']);word(s,'KID BUNKA / PRIVATE REVIEW',1062,836,1.3,1,p['muted'])
 s.save(which+'-TrackWerk-Design-Board')

def main():
 OUT.mkdir(exist_ok=True)
 for which,p in CONCEPTS.items():
  board(which)
  for suffix,bg in [('Dark',p['bg']),('Transparent',None)]:
   s=Scene(1000,170,bg,'TrackWerk original wordmark '+which);logo(s,which,42,36,12);s.save(which+'-TrackWerk-Wordmark-'+suffix)
  for size in [64,256]:
   s=Scene(size,size,p['bg'],'TrackWerk original app icon '+which);mark(s,which,0,0,size);s.save(which+'-TrackWerk-App-Icon-'+str(size))
 # Compact native proposal:32 rows per design, x0 uses the most significant bit.
 masks=[]
 for which in CONCEPTS:
  scene=Scene(32,32,'#000000');saved=CONCEPTS[which].copy();CONCEPTS[which].update(ink='#FFFFFF',second='#FFFFFF',bg='#000000');mark(scene,which,0,0,32,framed=False,mono=True);CONCEPTS[which].update(saved)
  tiny=scene.im.resize((32,32),Image.Resampling.NEAREST).convert('L');rows=[]
  for y in range(32):rows.append(sum((1<<(31-x)) for x in range(32) if tiny.getpixel((x,y))>127))
  masks.append(rows)
 header='// SPDX-License-Identifier: GPL-3.0-or-later\n// Original optional mark proposal; no app integration is performed here.\n#pragma once\n#include <cstdint>\nnamespace trackwerk_brand_studies {\ninline constexpr uint32_t markRows[3][32]={\n'
 for rows in masks:header+='{'+','.join('0x%08xu'%row for row in rows)+'},\n'
 header+='};\n} // namespace trackwerk_brand_studies\n'
 (R/'TrackWerk-Mark-Masks.h').write_text(header)
 (R/'TrackWerk-Mark-Geometry.json').write_text(json.dumps({'coordinate_space':[32,32],'marks':{k:geometry(k) for k in CONCEPTS}},indent=2)+'\n')
 # Native64px assets are shown at1x and4x on an extra comparison, with no interpolation.
 contact=Image.new('RGB',(1600,3*900))
 for i,k in enumerate(CONCEPTS):contact.paste(Image.open(OUT/(k+'-TrackWerk-Design-Board.png')).convert('RGB'),(0,i*900))
 contact.save(OUT/'TrackWerk-Three-Logo-Studies.png')
 icons=Image.new('RGB',(960,320),rgb('#0B1119'))
 for i,k in enumerate(CONCEPTS):
  im=Image.open(OUT/(k+'-TrackWerk-App-Icon-64.png')).convert('RGB');icons.paste(im,(i*320+24,128));icons.paste(im.resize((192,192),Image.Resampling.NEAREST),(i*320+112,64))
 icons.save(OUT/'TrackWerk-64px-Icon-Comparison.png')
 reports=[]
 for path in sorted(OUT.glob('*.svg')):
  root=ET.parse(path).getroot();text=path.read_text();assert '<text' not in text and 'font-family' not in text and '<image' not in text and 'href=' not in text
  assert 'TrackWerk' in text and not any(n in text for n in ['Pocket','Extreme','TrakWerk'])
  reports.append({'file':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'external_fonts_or_images':False})
 for k in CONCEPTS:
  im=Image.open(OUT/(k+'-TrackWerk-App-Icon-64.png'));assert im.size==(64,64) and im.getbbox()
 report={'passed':True,'name':'TrackWerk','publication':'HELD / PRIVATE DESIGN REVIEW','vector_assets':reports,'original_glyph_master_sha256':hashlib.sha256((R.parent/'font-authoring/pixel-masters.json').read_bytes()).hexdigest(),'font_source_files_modified':False,'old_logo_reused':False,'native_mark_mask_bytes':3*32*4}
 (OUT/'DESIGN-QA.json').write_text(json.dumps(report,indent=2)+'\n')
 (OUT/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in sorted(OUT.iterdir()) if p.is_file() and p.name!='SHA256SUMS.txt'))
 print(json.dumps({'boards':3,'SVG_assets':len(reports),'output':str(OUT),'private_review_only':True}))
if __name__=='__main__':main()
