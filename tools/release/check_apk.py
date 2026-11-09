#!/usr/bin/env python3
"""Read the actual APK: verify license bytes, architecture and Android16KiB load alignment."""
from pathlib import Path
import argparse,json,re,struct,zipfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('apk',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
build=(root/'app/build.gradle.kts').read_text()
block=build.split('val licensePayload = mapOf(',1)[1].split('\n)',1)[0]
licenses=dict(re.findall(r'"([^"]+)"\s+to\s+"([^"]+)"',block))
assert len(licenses)>=19
assert {"LICENSE","CREDITS.md","THIRD-PARTY-NOTICES.md","DaisySP-GPL-CONVERSION.txt","Mutable-Instruments-MIT.txt","NewPipe-Extractor-GPL3.txt"} <= licenses.keys()
report={'apk':args.apk.name,'license_assets':len(licenses),'native':[]}
with zipfile.ZipFile(args.apk) as z:
    assert z.testzip() is None,'APK ZIP integrity failure'
    for name,path in licenses.items():
        assert z.read('assets/licenses/'+name)==(root/path).read_bytes(),name+' differs from source'
    for abi,machine in [('arm64-v8a',183),('armeabi-v7a',40),('x86_64',62)]:
        for name in ['libpockettracker.so','libpockettracker-sdl.so','libSDL2.so','liboboe.so','libc++_shared.so']:
            path='lib/'+abi+'/'+name
            data=z.read(path)
            entry=z.getinfo(path)
            if entry.compress_type==zipfile.ZIP_STORED:
                with args.apk.open('rb') as container:
                    container.seek(entry.header_offset+26)
                    name_length,extra_length=struct.unpack('<HH',container.read(4))
                data_offset=entry.header_offset+30+name_length+extra_length
                assert data_offset%16384==0,path+' uncompressed APK entry is not16KiB aligned'
            assert data[:4]==b'\x7fELF' and data[5]==1
            assert data[4]==(1 if abi=='armeabi-v7a' else 2)
            assert struct.unpack_from('<H',data,18)[0]==machine,path+' architecture'
            is32=data[4]==1
            offset=struct.unpack_from('<I' if is32 else '<Q',data,28 if is32 else 32)[0]
            size,count=struct.unpack_from('<HH',data,42 if is32 else 54)
            aligns=[]
            for i in range(count):
                base=offset+i*size
                if struct.unpack_from('<I',data,base)[0]==1:
                    alignment=struct.unpack_from('<I' if is32 else '<Q',data,base+(28 if is32 else 48))[0]
                    assert alignment>=(4096 if is32 else 16384),path+' load alignment belowABI requirement'
                    aligns.append(alignment)
            assert aligns,path+' has no load segments'
            report['native'].append({'path':path,'load_alignments':aligns})
report['passed']=True
print(json.dumps(report,indent=2))
