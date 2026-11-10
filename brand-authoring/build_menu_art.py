#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original TrackWerk menu artwork, drawn from editable vector/pixel masters."""
from pathlib import Path
import argparse,importlib.util
R=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('original_art',R/'build_studies.py');art=importlib.util.module_from_spec(spec);spec.loader.exec_module(art)
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=R.parent/'design-review/Artwork');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);art.OUT=a.output
s=art.Scene(640,480,'#0B0D20','TrackWerk 0.9 / Kid Bunka / original menu artwork')
for y in range(16,480,16):s.rect(0,y,640,1,'#161B2E')
for x in range(16,640,32):s.rect(x,0,1,480,'#13192B')
for i in range(12):
 y=212+i*17;s.poly([(18,y),(34,y),(50,y+8),(50,y+10),(33,y+2),(18,y+2)],'#1D2940')
 s.poly([(622,y),(606,y),(590,y+8),(590,y+10),(607,y+2),(622,y+2)],'#1D2940')
s.rect(22,28,92,2,'#63E7EF');s.rect(526,28,92,2,'#EF61CE')
art.word(s,'KID BUNKA / MUSIC SYSTEM',154,37,1.65,1,'#A8B6CF')
art.logo(s,'B',66,91,7.1)
art.word(s,'CODE / RHYTHM / CHROME',179,171,1.6,1,'#939AB5')
art.mark(s,'B',253,214,134)
s.rect(101,369,438,1,'#35415C')
art.word(s,'SYNTH / SAMPLE / TRACK / PUNCH',107,391,1.7,1,'#C7D4E8')
art.word(s,'VERSION 0.9',24,449,1.3,1,'#63E7EF');art.word(s,'DEMOSCENE AUDIO LAB',413,449,1.3,1,'#EF61CE')
s.save('TrackWerk-0.9-Menu-Background');print(a.output)
