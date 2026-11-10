#!/usr/bin/env python3
"""Rebuild the private Plus Archive candidate from supplied source and exact SDL SDK."""
from pathlib import Path
import argparse,hashlib,os,shlex,subprocess,tarfile,zipfile
p=argparse.ArgumentParser();p.add_argument('--zig',type=Path,required=True);p.add_argument('--cmake',default='cmake');p.add_argument('--source',type=Path,required=True);p.add_argument('--app-zip',type=Path,required=True);p.add_argument('--sdl-source',type=Path,required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args()
r=a.work.resolve();r.mkdir(parents=True,exist_ok=True);sdk=r/'sdk';(sdk/'include/SDL2').mkdir(parents=True,exist_ok=True);(sdk/'lib/cmake/SDL2').mkdir(parents=True,exist_ok=True);tools=r/'tools';tools.mkdir(exist_ok=True)
assert hashlib.sha256(a.sdl_source.read_bytes()).hexdigest()=='b4c2416b244ad5d9c208fa5939e865331880df9fb8f9e8fca3a5d2d5017af549'
with tarfile.open(a.sdl_source) as t:
 for m in t.getmembers():
  parts=Path(m.name).parts
  if len(parts)==3 and parts[1]=='include' and parts[2].endswith('.h') and m.isfile():(sdk/'include/SDL2'/parts[2]).write_bytes(t.extractfile(m).read())
with zipfile.ZipFile(a.app_zip) as z:
 data=z.read(('Card-Payload/' if 'Card-Payload/Roms/PORTS/Games/PocketTrackerExtremePlusArchive/libs/libSDL2-2.0.so.0' in z.namelist() else '')+'Roms/PORTS/Games/PocketTrackerExtremePlusArchive/libs/libSDL2-2.0.so.0');assert hashlib.sha256(data).hexdigest()=='36addcd853b63e9a7041e78d0e1a613a4fdfff95b213779ee9bbfeffa586561d';(sdk/'lib/libSDL2-2.0.so.0').write_bytes(data)
for name,cmd in [('cc','cc'),('cxx','c++'),('ar','ar'),('ranlib','ranlib')]:
 args=' -target arm-linux-gnueabihf.2.28 -mcpu=cortex_a7' if name in ['cc','cxx'] else ''
 f=tools/name;f.write_text('#!/bin/sh\nexport ZIG_GLOBAL_CACHE_DIR='+shlex.quote(str(r/'zig-global'))+'\nexport ZIG_LOCAL_CACHE_DIR='+shlex.quote(str(r/'zig-local'))+'\nexec '+shlex.quote(str(a.zig.resolve()))+' '+cmd+args+' "$@"\n');f.chmod(0o755)
tc=tools/'toolchain.cmake';tc.write_text('set(CMAKE_SYSTEM_NAME Linux)\nset(CMAKE_SYSTEM_PROCESSOR arm)\n'+''.join('set('+k+' "'+str(tools/v)+'")\n' for k,v in [('CMAKE_C_COMPILER','cc'),('CMAKE_CXX_COMPILER','cxx'),('CMAKE_AR','ar'),('CMAKE_RANLIB','ranlib')])+'set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)\nset(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--allow-shlib-undefined -s")\n')
(sdk/'lib/cmake/SDL2/SDL2Config.cmake').write_text('get_filename_component(_s "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)\nset(SDL2_FOUND TRUE)\nadd_library(SDL2::SDL2 SHARED IMPORTED)\nset_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_LOCATION "${_s}/lib/libSDL2-2.0.so.0" INTERFACE_INCLUDE_DIRECTORIES "${_s}/include/SDL2")\n')
(sdk/'lib/cmake/SDL2/SDL2ConfigVersion.cmake').write_text('set(PACKAGE_VERSION 2.0.20)\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\n')
subprocess.run([a.cmake,'-S',str(a.source.resolve()/'shell'),'-B',str(r/'build'),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_TOOLCHAIN_FILE='+str(tc),'-DSDL2_DIR='+str(sdk/'lib/cmake/SDL2'),'-DPT_MIYOO_OFFLINE=OFF','-DPT_MIYOO_PLUS_ARCHIVE=ON','-DCMAKE_SKIP_RPATH=ON'],check=True)
subprocess.run([a.cmake,'--build',str(r/'build'),'--target','pockettracker-sdl','-j','4'],check=True)
subprocess.run([str(tools/'cc'),'-O2','-s','-shared','-Wl,-soname,libneonarmmiyoo.so',str(a.source.resolve()/'shell/miyoo/neon_compat.c'),'-o',str(r/'libneonarmmiyoo.so')],check=True)
print('Built:',r/'build/pockettracker-sdl');print('GPL helper:',r/'libneonarmmiyoo.so')
