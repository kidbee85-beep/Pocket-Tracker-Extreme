#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build only new clean assets; never writes a production header or old masters."""
from pathlib import Path
import hashlib
import importlib.util
import itertools
import json
import unicodedata
from PIL import Image,ImageDraw

R=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('original_atlas_helpers',R/'generate_atlases.py')
helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)

def image(rows):
    im=Image.new('L',(7,11));im.putdata([255 if b=='1' else 0 for row in rows for b in row]);return im

def clean_label_glyph(cp,glyphs,width,height):
    # Vertical master coordinates retain a real lowercase x-height and descenders.
    # Existing helper supplies the same Latin1 composition/metric convention;
    # the glyph dictionary here contains only the ten newly authored alphabets.
    raw=helpers.label_glyph(cp,glyphs,width,height)
    if not width:return raw
    root=unicodedata.normalize('NFD',chr(cp))[0]
    if root not in 'Iiljft':return raw
    # Exact legacy label metrics allow only two pixels for I/i/l at size12
    # (i/l are still two at size18). A naive nearest resize can miss the
    # central source column entirely. Optically construct these narrow
    # glyphs on their target pixel grid, preserving meaningful stems.
    out=Image.frombytes('L',(width,height),raw);d=ImageDraw.Draw(out)
    top=2 if height==14 else 3;body_h=11 if height==14 else 17
    yy=lambda y:top+round(y*(body_h-1)/10)
    cap=glyphs[ord('H')].getbbox()[1];xt=glyphs[ord('n')].getbbox()[1]
    center=(width-1)//2;end=width-1
    accents=len(unicodedata.normalize('NFD',chr(cp)))>1
    clear_from=yy(xt+1) if root=='i' and accents else top
    d.rectangle((0,clear_from,end,height-1),fill=0)
    def line(points):d.line(points,fill=255,width=1)
    if root=='I':
        line([(0,yy(cap)),(end,yy(cap))]);line([(center,yy(cap)),(center,yy(8))]);line([(0,yy(8)),(end,yy(8))])
    elif root=='i':
        if not accents:d.point((center,yy(1)),fill=255)
        line([(center,yy(xt+1)),(center,yy(8))])
        if sum(v>0 for v in glyphs[ord('i')].crop((0,8,7,9)).getdata())>1:
            line([(0,yy(8)),(end,yy(8))])
    elif root=='l':
        line([(0,yy(cap)),(0,yy(7)),(end,yy(8))])
    elif root=='j':
        d.point((end,yy(1)),fill=255);line([(end,yy(xt+1)),(end,yy(9)),(max(0,end-1),yy(10)),(0,yy(10))])
    elif root=='f':
        line([(end,yy(cap)),(min(end,center+1),yy(cap)),(center,yy(cap+1)),(center,yy(8))]);line([(0,yy(xt)),(end,yy(xt))])
    else:
        line([(center,yy(cap+1)),(center,yy(7)),(min(end,center+1),yy(8)),(end,yy(8))]);line([(0,yy(xt)),(end,yy(xt))])
    return out.tobytes()

def build_clean_atlases(metric=None):
    masters=json.loads((R/'clean-masters.json').read_text())
    metric=helpers.metrics() if metric is None else metric
    assert [s['id'] for s in masters['styles']]==list(range(7,17))
    grids={2:[],3:[]};labels={12:[],18:[]}
    for style in masters['styles']:
        glyphs={int(cp):image(rows) for cp,rows in style['glyphs'].items()}
        for scale,w,h in [(2,10,11),(3,15,17)]:
            data=bytearray()
            for cp in range(32,127):
                canvas=Image.new('L',(w,h));canvas.paste(glyphs[cp].resize((w-1,h),Image.Resampling.NEAREST),(0,0));data.extend(canvas.tobytes())
            grids[scale].append(bytes(data))
        for size in (12,18):
            atlas=bytearray(metric[size]['pixel_size'])
            for cp,(offset,width,_,_) in zip(helpers.CPS,metric[size]['glyphs']):
                ink=clean_label_glyph(cp,glyphs,width,metric[size]['height'])
                assert len(ink)==width*metric[size]['height']
                atlas[offset:offset+len(ink)]=ink
            labels[size].append(bytes(atlas))
    return masters,metric,grids,labels

def draw_label(canvas,text,x,y,style,size,metric,labels,color=(224,235,243)):
    data=labels[size][style];h=metric[size]['height'];table=dict(zip(helpers.CPS,metric[size]['glyphs']))
    for ch in text:
        offset,w,left,advance=table[ord(ch)]
        if w:
            mask=Image.frombytes('L',(w,h),data[offset:offset+w*h])
            canvas.paste(Image.new('RGB',(w,h),color),(x+left,y),mask)
        x+=advance
    return x

def main():
    protected=[R/'pixel-masters.json',R/'generate_atlases.py',R.parent/'source/native/ui/demo_fonts.h']
    before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    masters,metric,grids,labels=build_clean_atlases()
    rows=[]
    comparisons=[]
    for a,b in itertools.combinations(masters['styles'],2):
        changes=sum(a['glyphs'][str(cp)]!=b['glyphs'][str(cp)] for cp in range(48,123) if chr(cp).isalnum())
        assert changes>=20,(a['name'],b['name'],changes)
        comparisons.append({'a':a['name'],'b':b['name'],'alphanumeric_glyphs_different':changes})
    for i,style in enumerate(masters['styles']):
        for cp,bitmap in style['glyphs'].items():
            assert len(bitmap)==11 and all(len(row)==7 and set(row)<=set('01') for row in bitmap)
            assert any('1' in row for row in bitmap)==(int(cp)!=32)
        for cp in map(ord,'gjpqy'):
            assert any('1' in row for row in style['glyphs'][str(cp)][9:]),(style['name'],chr(cp))
        pair_checks={}
        for a,b in [('0','O'),('1','I'),('I','l'),('1','l'),('B','8'),('S','5'),('E','F'),('F','#'),('n','h'),('a','o')]:
            for scale,w,h in [(2,10,11),(3,15,17)]:
                data=grids[scale][i];sa=data[(ord(a)-32)*w*h:(ord(a)-31)*w*h];sb=data[(ord(b)-32)*w*h:(ord(b)-31)*w*h]
                assert sa!=sb,(style['name'],scale,a,b)
            for size in (12,18):
                table=dict(zip(helpers.CPS,metric[size]['glyphs']));h=metric[size]['height']
                aoff,aw,_,aa=table[ord(a)];boff,bw,_,ba=table[ord(b)]
                sa=labels[size][i][aoff:aoff+aw*h];sb=labels[size][i][boff:boff+bw*h]
                assert (aw,aa,sa)!=(bw,ba,sb),(style['name'],size,a,b)
            pair_checks[a+'/'+b]=True
        for size in (12,18):
            table=dict(zip(helpers.CPS,metric[size]['glyphs']));h=metric[size]['height']
            for ch in 'Iiljft':
                off,w,_,_=table[ord(ch)];im=Image.frombytes('L',(w,h),labels[size][i][off:off+w*h])
                rows_with_ink=sum(any(im.getpixel((x,y)) for x in range(w)) for y in range(h))
                assert rows_with_ink>=6,(style['name'],size,ch,'lost vertical stem')
        rows.append({'id':style['id'],'name':style['name'],'description':style['description'],
                     'glyphs':95,'all_nonspace_have_ink':True,'true_lowercase_descenders':True,'data_pairs':pair_checks,
                     'grid2_sha256':hashlib.sha256(grids[2][i]).hexdigest(),'grid3_sha256':hashlib.sha256(grids[3][i]).hexdigest(),
                     'label12_sha256':hashlib.sha256(labels[12][i]).hexdigest(),'label18_sha256':hashlib.sha256(labels[18][i]).hexdigest()})
    outputs={}
    for kind,arrays in [('grid2',grids[2]),('grid3',grids[3]),('label12',labels[12]),('label18',labels[18])]:
        data=b''.join(arrays);path=R/('clean-atlas-'+kind+'.bin');path.write_bytes(data)
        outputs[kind]={'path':path.name,'styles':10,'bytes_per_style':len(arrays[0]),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    # Exact target atlas rasters at 640px native canvas width; no invented vector preview.
    contact=Image.new('RGB',(640,480),(16,22,30));d=ImageDraw.Draw(contact)
    for i,style in enumerate(masters['styles']):
        y=i*48;d.line([(0,y+47),(639,y+47)],fill=(45,57,70))
        draw_label(contact,f"{style['id']:02d} {style['name']}",10,y+1,i,12,metric,labels,(143,222,204))
        draw_label(contact,'TrackWerk  0O 1Il B8 S5 E F # 0123456789',200,y+1,i,12,metric,labels)
        draw_label(contact,'Archive sampler / IN 00:04 OUT 00:18',10,y+17,i,12,metric,labels)
        draw_label(contact,'jazz groovy rhythm / pqy gj nm',10,y+32,i,12,metric,labels)
    contact.save(R/'clean-label12-contact.png')
    contact.resize((1280,960),Image.Resampling.NEAREST).save(R/'clean-label12-contact-2x.png')
    for i,style in enumerate(masters['styles']):
        sheet=Image.new('RGB',(640,480),(16,22,30))
        draw_label(sheet,style['name']+' / TrackWerk',12,8,i,18,metric,labels,(143,222,204))
        samples=['ABCDEFGHIJKLMNOPQRSTUVWXYZ','abcdefghijklmnopqrstuvwxyz','0123456789 0O 1Il B8 S5 E F #',
                 'Archive sampler / Import WAV','IN 00:04 OUT 00:18 / 92 BPM','jazz bass groove rhythm pqy gj nm',
                 'A#4 C-5 OFF KIL 00 7F 80 FF','ÄÖÜ äöü ß àéñø æ £ ¥ © …']
        for j,text in enumerate(samples):
            draw_label(sheet,text,12,42+j*24,i,12,metric,labels)
            draw_label(sheet,text,12,248+j*27,i,18,metric,labels)
        sheet.save(R/('clean-'+str(style['id'])+'-'+style['name'].replace(' ','-')+'-atlas.png'))
        # All 95 original master cells, legible at an integer review zoom.
        master=Image.new('RGB',(16*9,6*14),(16,22,30))
        for j,cp in enumerate(range(32,127)):
            mask=image(style['glyphs'][str(cp)]);master.paste(Image.new('RGB',(7,11),(224,235,243)),((j%16)*9,(j//16)*14),mask)
        master.resize((576,336),Image.Resampling.NEAREST).save(R/('clean-'+str(style['id'])+'-masters.png'))
    after={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected};assert before==after
    total=sum(v['bytes'] for v in outputs.values());combined=total+6*88764
    assert combined<1536*1024
    result={'ok':True,'styles':rows,'outputs':outputs,'master_cell':[7,11],'grid_cells':{'2':[10,11],'3':[15,17]},
            'label_metrics':{str(k):{'height':v['height'],'pixel_size':v['pixel_size'],'glyphs':192} for k,v in metric.items()},
            'label_metadata_sha256':hashlib.sha256(json.dumps(metric,sort_keys=True).encode()).hexdigest(),
            'new_atlas_bytes':total,'existing_six_atlas_bytes':6*88764,'combined_16_atlas_bytes':combined,'budget_bytes':1536*1024,
            'pairwise_distinctness':comparisons,'minimum_alphanumeric_glyphs_different':min(x['alphanumeric_glyphs_different'] for x in comparisons),
            'protected_files_unchanged_sha256':after,'source_ui_headers_modified':False,
            'raster_origin':'Original clean masters; shared helper used only for metrics and original Latin1/diacritic composition.',
            'narrow_label_hinting':'I/i/l/j/f/t constructed directly on fixed target pixel grid, including accented I/i; preserves stems that nearest resampling can erase at width2. Existing six atlases unchanged.',
            'review_scope':'Generated atlas pixels at exact native cell/label sizes and unchanged advances; production Canvas integration pending owner merge, not physical-device testing.'}
    (R/'clean-atlas-manifest.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'ok':True,'styles':10,'new_bytes':total,'combined_bytes':combined,'minimum_distinct_alnum':result['minimum_alphanumeric_glyphs_different'],'first_six_untouched':True}))

if __name__=='__main__':main()
