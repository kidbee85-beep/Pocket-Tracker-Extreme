#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ten original clean alphabets, specified independently of the six display faces.

Only these geometric recipes are inputs. No installed font, ROM, existing font
atlas or existing display-master glyph is read to construct a letterform.
"""
from pathlib import Path
import hashlib
import json

HERE=Path(__file__).resolve().parent
FACES=[
 # id, name, left/right, cap top, x-height top, corner, crossbar, construction
 (7,'CLEAR MONO',0,5,0,3,1,4,'Open humanist mono; double-storey a, flagged 1 and hooked l.'),
 (8,'STUDIO SANS',0,6,0,3,1,4,'Rounded studio sans; large counters, double-storey a/g, restrained terminals.'),
 (9,'COMPACT GRID',1,5,0,2,0,4,'Narrow square grid; tall x-height, open 4 and compact shoulders.'),
 (10,'SOFT PIXEL',0,6,0,3,2,5,'Soft octagonal bowls; single-storey a/g and low rounded shoulders.'),
 (11,'ISO TERMINAL',0,6,0,2,1,4,'Technical chamfers; flat-topped A, straight-sided lowercase and crossbar 7.'),
 (12,'WIDE SCREEN',0,6,1,3,1,5,'Extended low-profile alphabet; wide apertures and single-storey a.'),
 (13,'PAPER TYPE',1,5,0,3,1,4,'Original small slab-serif; bracket-free feet, double-storey a/g.'),
 (14,'LCD CLEAN',0,6,0,3,0,4,'Continuous rectangular digital alphabet; unbroken stems, single-storey a.'),
 (15,'RAIL MONO',1,5,0,2,1,3,'Tall technical mono; high x-height, rising R leg and offset terminals.'),
 (16,'NIGHT EDITOR',0,6,0,2,2,4,'Medium upright stems with open bowls; high x-height and single-storey a.'),
]

def glyph(face,ch):
    sid,name,l,r,top,xt,corner,mid,_=face
    bottom=8;c=(l+r)//2
    pixels=[[0]*7 for _ in range(11)]

    def point(x,y):
        assert 0<=x<7 and 0<=y<11,(sid,ch,x,y)
        pixels[y][x]=1

    def line(points,weight=1):
        for (x0,y0),(x1,y1) in zip(points,points[1:]):
            dx=abs(x1-x0);sx=1 if x0<x1 else -1
            dy=-abs(y1-y0);sy=1 if y0<y1 else -1;err=dx+dy
            while True:
                point(x0,y0)
                if weight==2 and x0<r:point(x0+1,y0)
                if x0==x1 and y0==y1:break
                e2=2*err
                if e2>=dy:err+=dy;x0+=sx
                if e2<=dx:err+=dx;y0+=sy
        if len(points)==1:point(*points[0])

    def stem(x,a,b,medium=False):
        line([(x,a),(x,b)],2 if medium and sid==16 else 1)

    def bowl(a,b,d,e,rad=None):
        rad=min(corner if rad is None else rad,(d-a)//2,(e-b)//2)
        line([(a+rad,b),(d-rad,b),(d,b+rad),(d,e-rad),
              (d-rad,e),(a+rad,e),(a,e-rad),(a,b+rad),(a+rad,b)])

    def open_bowl(a,b,d,e):
        rad=max(1,min(corner,(d-a)//2,(e-b)//2))
        line([(d,b+rad),(d-rad,b),(a+rad,b),(a,b+rad),
              (a,e-rad),(a+rad,e),(d-rad,e),(d,e-rad)])

    def serif(x,a,b):
        if sid==13:
            line([(max(0,x-1),a),(min(6,x+1),a)])
            line([(max(0,x-1),b),(min(6,x+1),b)])

    def s_shape(a,b,d,e):
        m=(b+e)//2;rad=max(1,min(corner,(d-a)//2))
        line([(d,b+1),(d-rad,b),(a+rad,b),(a,b+1),(a,m-1),
              (a+1,m),(d-1,m),(d,m+1),(d,e-1),(d-1,e),(a,e)])

    if ch==' ':pass
    elif ch=='A':
        if sid in (9,11,14):
            shoulder=top+2 if sid!=14 else top
            line([(l,bottom),(l,shoulder),(l+1,top),(r-1,top),(r,shoulder),(r,bottom)])
        elif sid in (8,10):line([(l,bottom),(l,top+3),(c,top),(c+1,top),(r,top+3),(r,bottom)])
        else:line([(l,bottom),(c,top),(r,bottom)])
        line([(l+1 if sid in (7,12,13,15) else l,mid),(r-1 if sid in (7,12,13,15) else r,mid)])
        serif(l,bottom,bottom);serif(r,bottom,bottom)
    elif ch=='B':
        stem(l,top,bottom,True);bowl(l,top,r,mid);bowl(l,mid,r,bottom)
        serif(l,top,bottom)
    elif ch=='C':open_bowl(l,top,r,bottom)
    elif ch=='D':
        stem(l,top,bottom,True)
        rad=2 if sid in (7,8,10,12) else 1
        line([(l,top),(r-rad,top),(r,top+rad),(r,bottom-rad),(r-rad,bottom),(l,bottom)])
        serif(l,top,bottom)
    elif ch in 'EF':
        stem(l,top,bottom,True);line([(l,top),(r,top)]);line([(l,mid),(r-1 if sid in (7,8,10,12) else r,mid)])
        if ch=='E':line([(l,bottom),(r,bottom)])
        serif(l,top,bottom)
    elif ch=='G':
        open_bowl(l,top,r,bottom);line([(c,mid+1),(r,mid+1),(r,bottom-1)])
        if sid in (11,14,15):line([(r-1,bottom),(r,bottom)])
    elif ch=='H':
        stem(l,top,bottom,True);stem(r,top,bottom);line([(l,mid),(r,mid)])
        serif(l,top,bottom);serif(r,top,bottom)
    elif ch=='I':
        line([(l,top),(r,top)]);stem(c,top,bottom);line([(l,bottom),(r,bottom)])
    elif ch=='J':
        line([(c,top),(r,top),(r,bottom-1),(r-1,bottom),(l+1,bottom),(l,bottom-1),(l,bottom-3)])
    elif ch=='K':
        stem(l,top,bottom,True);line([(r,top),(l,mid),(r,bottom)]);serif(l,top,bottom)
    elif ch=='L':
        stem(l,top,bottom,True);line([(l,bottom),(r,bottom)]);serif(l,top,bottom)
    elif ch=='M':
        valley=top+3 if sid in (7,8,10,13) else top+4
        line([(l,bottom),(l,top),(c,valley),(r,top),(r,bottom)])
        serif(l,top,bottom);serif(r,top,bottom)
    elif ch=='N':
        if sid in (11,14):line([(l,bottom),(l,top),(l+1,top),(r,bottom),(r,top)])
        else:line([(l,bottom),(l,top),(r,bottom),(r,top)])
        serif(l,top,bottom);serif(r,top,bottom)
    elif ch in 'OQ':
        bowl(l,top,r,bottom)
        if ch=='Q':line([(c,6),(r,9)])
    elif ch in 'PR':
        stem(l,top,bottom,True);bowl(l,top,r,mid)
        if ch=='R':line([(c if sid!=15 else l+1,mid),(r,bottom)])
        serif(l,top,bottom)
    elif ch=='S':s_shape(l,top,r,bottom)
    elif ch=='T':
        line([(l,top),(r,top)]);stem(c,top,bottom);serif(c,bottom,bottom)
    elif ch=='U':
        rad=max(1,corner)
        line([(l,top),(l,bottom-rad),(l+rad,bottom),(r-rad,bottom),(r,bottom-rad),(r,top)])
    elif ch=='V':line([(l,top),(c,bottom),(r,top)])
    elif ch=='W':
        if sid in (9,11,14,15):line([(l,top),(l,bottom),(c,5),(r,bottom),(r,top)])
        else:line([(l,top),(l+1,bottom),(c,5),(r-1,bottom),(r,top)])
    elif ch=='X':line([(l,top),(r,bottom)]);line([(r,top),(l,bottom)])
    elif ch=='Y':line([(l,top),(c,mid),(r,top)]);stem(c,mid,bottom)
    elif ch=='Z':line([(l,top),(r,top),(l,bottom),(r,bottom)])
    elif ch=='0':
        bowl(l,top,r,bottom);line([(l+1,bottom-2),(r-1,top+2)])
    elif ch=='1':
        line([(max(l,c-1),top+2),(c,top),(c,bottom)]);line([(l,bottom),(r,bottom)])
    elif ch=='2':
        line([(l,top+1),(l+1,top),(r-1,top),(r,top+1),(r,mid-1),(l,bottom),(r,bottom)])
    elif ch=='3':
        line([(l,top),(r-1,top),(r,top+1),(r,mid-1),(r-1,mid),(c,mid)])
        line([(r-1,mid),(r,mid+1),(r,bottom-1),(r-1,bottom),(l,bottom)])
    elif ch=='4':
        if sid in (8,10,13,16):line([(r-1,bottom),(r-1,top),(l,5),(r,5)])
        else:line([(l,top),(l,5),(r,5)]);stem(r-1,top,bottom)
    elif ch=='5':
        line([(r,top),(l,top),(l,mid),(r-1,mid),(r,mid+1),(r,bottom-1),(r-1,bottom),(l,bottom)])
    elif ch=='6':
        line([(r-1,top),(l+1,top),(l,top+2),(l,bottom-1),(l+1,bottom),(r-1,bottom),(r,bottom-1),(r,mid+1),(r-1,mid),(l,mid)])
    elif ch=='7':
        line([(l,top),(r,top),(c-1,bottom)])
        if sid in (11,13,15):line([(l+1,mid),(r-1,mid)])
    elif ch=='8':
        line([(l+1,top),(r-1,top),(r,top+1),(r,mid-1),(r-1,mid),(r,mid+1),
              (r,bottom-1),(r-1,bottom),(l+1,bottom),(l,bottom-1),(l,mid+1),
              (l+1,mid),(l,mid-1),(l,top+1),(l+1,top)])
        line([(l+1,mid),(r-1,mid)])
    elif ch=='9':
        bowl(l,top,r,mid);line([(r,mid),(r,bottom-2),(r-1,bottom),(l+1,bottom)])
    elif ch=='a':
        if sid in (10,12,14,16):
            bowl(l,xt+1 if sid==14 else xt,r,bottom);stem(r,xt,bottom)
            if sid==16:line([(r,bottom),(min(6,r+1),bottom)])
        else:
            line([(l,xt+1),(l+1,xt),(r-1,xt),(r,xt+1),(r,bottom)])
            bowl(l,(xt+bottom)//2,r,bottom,1 if sid!=9 else 0)
    elif ch=='b':stem(l,top,bottom);bowl(l,xt,r,bottom)
    elif ch=='c':open_bowl(l,xt,r,bottom)
    elif ch=='d':stem(r,top,bottom);bowl(l,xt,r,bottom)
    elif ch=='e':
        bowl(l,xt,r,bottom)
        # Open lower-right aperture: deliberately constructed open, not a distress mask.
        pixels[bottom-1][r]=0
        line([(l,(xt+bottom)//2),(r,(xt+bottom)//2)])
    elif ch=='f':
        line([(min(r,c+2),top),(c,top),(c-1,top+1),(c-1,bottom)])
        line([(l,xt),(r-1,xt)])
    elif ch=='g':
        if sid in (8,13):
            bowl(l,xt,r,6,1);line([(r,xt),(r,8),(l+1,8),(l,9),(l+1,10),(r-1,10),(r,9),(r-1,8)])
        else:
            bowl(l,xt,r,7,1 if sid!=14 else 0)
            line([(r,xt),(r,9),(r-1,10),(l+1,10),(l,9)])
    elif ch=='h':
        stem(l,top,bottom);line([(l,xt+1),(l+1,xt),(r-1,xt),(r,xt+1),(r,bottom)])
    elif ch=='i':
        point(c,top+1);stem(c,xt+1,bottom)
        if sid in (7,9,11,13,15):line([(max(l,c-1),xt+1),(c,xt+1)]);line([(max(l,c-1),bottom),(min(r,c+1),bottom)])
    elif ch=='j':
        point(c+1,top+1);line([(c,xt+1),(c+1,xt+1),(c+1,9),(c,10),(l+1,10),(l,9)])
    elif ch=='k':stem(l,top,bottom);line([(r,xt),(l,6),(r,bottom)])
    elif ch=='l':
        line([(max(l,c-1),top),(c,top),(c,7),(c+1,bottom),(r,bottom)])
        if sid==13:line([(max(0,c-1),bottom),(min(6,r+1),bottom)])
    elif ch=='m':
        line([(l,bottom),(l,xt),(l+1,xt),(c,xt+1),(r-1,xt),(r,xt+1),(r,bottom)])
        stem(c,xt+1,bottom)
    elif ch=='n':
        stem(l,xt,bottom);line([(l,xt+1),(l+1,xt),(r-1,xt),(r,xt+1),(r,bottom)])
    elif ch=='o':bowl(l,xt,r,bottom)
    elif ch=='p':stem(l,xt,10);bowl(l,xt,r,7)
    elif ch=='q':stem(r,xt,10);bowl(l,xt,r,7)
    elif ch=='r':
        stem(l,xt,bottom);line([(l,xt+1),(l+2,xt),(r-1,xt),(r,xt+1)])
    elif ch=='s':s_shape(l,xt,r,bottom)
    elif ch=='t':
        line([(c,top+1),(c,7),(c+1,bottom),(r,bottom)]);line([(l,xt),(r,xt)])
    elif ch=='u':
        line([(l,xt),(l,7),(l+1,bottom),(r-1,bottom),(r,7),(r,xt)]);stem(r,xt,bottom)
    elif ch=='v':line([(l,xt),(c,bottom),(r,xt)])
    elif ch=='w':line([(l,xt),(l+1,bottom),(c,6),(r-1,bottom),(r,xt)])
    elif ch=='x':line([(l,xt),(r,bottom)]);line([(r,xt),(l,bottom)])
    elif ch=='y':line([(l,xt),(c,7),(r,xt)]);line([(r,xt),(r,8),(c,10),(l,10)])
    elif ch=='z':line([(l,xt),(r,xt),(l,bottom),(r,bottom)])
    else:
        punctuation={
         '!':[[(3,0),(3,5)],[(3,8)]], '"':[[(1,0),(1,2)],[(5,0),(5,2)]],
         '#':[[(2,1),(1,8)],[(5,1),(4,8)],[(0,3),(6,3)],[(0,6),(6,6)]],
         '$':[[(5,1),(1,1),(0,2),(0,3),(5,5),(6,6),(5,7),(0,7)],[(3,0),(3,8)]],
         '%':[[(0,8),(6,0)],[(0,0),(1,0),(1,2),(0,2),(0,0)],[(5,6),(6,6),(6,8),(5,8),(5,6)]],
         '&':[[(6,8),(1,3),(1,1),(2,0),(4,0),(5,1),(5,2),(0,6),(0,7),(1,8),(3,8),(6,5)]],
         "'": [[(3,0),(3,2)]], '(':[[(4,0),(2,2),(2,6),(4,8)]], ')':[[(2,0),(4,2),(4,6),(2,8)]],
         '*':[[(3,2),(3,6)],[(1,3),(5,5)],[(1,5),(5,3)]], '+':[[(0,4),(6,4)],[(3,1),(3,7)]],
         ',':[[(3,8),(2,10)]], '-':[[(1,4),(5,4)]], '.':[[(3,8)]], '/':[[(0,8),(6,0)]],
         ':':[[(3,3)],[(3,7)]], ';':[[(3,3)],[(3,7),(2,9)]],
         '<':[[(5,1),(1,4),(5,7)]], '=':[[(0,3),(6,3)],[(0,6),(6,6)]], '>':[[(1,1),(5,4),(1,7)]],
         '?':[[(0,1),(1,0),(5,0),(6,1),(6,3),(3,5),(3,6)],[(3,8)]],
         '@':[[(6,7),(1,7),(0,6),(0,1),(1,0),(5,0),(6,1),(6,5),(4,5),(4,2),(2,2),(2,5),(4,5)]],
         '[':[[(4,0),(1,0),(1,8),(4,8)]], '\\':[[(0,0),(6,8)]], ']':[[(2,0),(5,0),(5,8),(2,8)]],
         '^':[[(0,3),(3,0),(6,3)]], '_':[[(0,10),(6,10)]], '`':[[(2,0),(4,2)]],
         '{':[[(5,0),(3,0),(2,1),(2,3),(0,4),(2,5),(2,7),(3,8),(5,8)]], '|':[[(3,0),(3,8)]],
         '}':[[(1,0),(3,0),(4,1),(4,3),(6,4),(4,5),(4,7),(3,8),(1,8)]], '~':[[(0,5),(2,3),(4,5),(6,3)]],
        }
        assert ch in punctuation,(sid,ch)
        for p in punctuation[ch]:line(p)
    return [''.join(map(str,row)) for row in pixels]

def main():
    old=HERE/'pixel-masters.json';before=hashlib.sha256(old.read_bytes()).hexdigest()
    styles=[{'id':f[0],'name':f[1],'description':f[-1],
             'glyphs':{str(cp):glyph(f,chr(cp)) for cp in range(32,127)}} for f in FACES]
    doc={'license':'GPL-3.0-or-later','author':'Kid Bunka / project contributors',
         'origin':'Original clean geometric glyph recipes, independently constructed; no font/ROM raster inputs and no transformations of the six existing display alphabets.',
         'master_cell':[7,11],'cap_rows':[0,8],'descender_rows':[9,10],'styles':styles,
         'existing_display_master_sha256_unchanged':before}
    (HERE/'clean-masters.json').write_text(json.dumps(doc,indent=2)+'\n')
    assert hashlib.sha256(old.read_bytes()).hexdigest()==before
    print('Authored ten original clean alphabets (IDs7–16), existing six untouched.')

if __name__=='__main__':main()
