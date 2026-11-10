#!/usr/bin/env python3
"""Offline ARM64 build of the experimental iPhone companion processes.
Requires Go 1.27.3, Zig 0.14.1, Make and pkg-config/pkgconf. No system install.
The main app is built separately with build.py and the firmware's SDL2 ABI.
"""
import argparse,os,shlex,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--go',type=Path,required=True);p.add_argument('--zig',type=Path,required=True)
p.add_argument('--pkg-config',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
p.add_argument('--libusb-source',type=Path,help='Optional replacement libusb source for LGPL relinking')
a=p.parse_args();s=Path(__file__).resolve().parents[2];out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
deps=s/'external/apple-usb-deps';prefix=out/'sdk';wrappers=out/'toolchain';wrappers.mkdir(exist_ok=True)
env=os.environ.copy();env.update(ZIG_GLOBAL_CACHE_DIR=str(out/'zig-global'),ZIG_LOCAL_CACHE_DIR=str(out/'zig-local'),GOTOOLCHAIN='local',GOCACHE=str(out/'go-cache'),GOPATH=str(out/'go-path'),GOMODCACHE=str(out/'go-mod'),PKG_CONFIG=str(a.pkg_config.resolve()),PKG_CONFIG_PATH=str(prefix/'lib/pkgconfig'),ac_cv_func_malloc_0_nonnull='yes',ac_cv_func_realloc_0_nonnull='yes')
for name,command in [('cc','cc'),('cxx','c++'),('ar','ar'),('ranlib','ranlib')]:
 target=' -target aarch64-linux-gnu.2.31 -mcpu=cortex_a53' if command in ['cc','c++'] else ''
 w=wrappers/name;w.write_text('#!/bin/sh\nexec '+shlex.quote(str(a.zig.resolve()))+' '+command+target+' "$@"\n');w.chmod(0o755)
env.update(CC=str(wrappers/'cc'),CXX=str(wrappers/'cxx'),AR=str(wrappers/'ar'),RANLIB=str(wrappers/'ranlib'),LDFLAGS='-Wl,--strip-all',CFLAGS='-O2 -ffile-prefix-map='+str(Path.home())+'=/build -ffile-prefix-map='+str(s)+'=/src/pocket-tracker-extreme')
for name,flags in [('libusb-1.0.30',['--disable-udev']),('libplist-2.8.1',['--without-cython','--without-tests','--without-tools']),('usbmuxd-1.1.1',['--without-preflight','--without-systemd','--with-udevrulesdir=no'])]:
 src=a.libusb_source.resolve() if name.startswith('libusb-') and a.libusb_source else deps/name
 d=out/name;d.mkdir(exist_ok=True);e=env.copy()
 if name.startswith('usbmuxd'):
  e.update(libusb_LIBS=str(prefix/'lib/libusb-1.0.a')+' -lpthread',libplist_LIBS=str(prefix/'lib/libplist-2.0.a')+' -lm')
 for call in [[str(src/'configure'),'--host=aarch64-linux-gnu','--prefix='+str(prefix),'--disable-shared','--enable-static',*flags],['make','-j4'],['make','install']]:subprocess.run(call,cwd=d,env=e,check=True)
result=out/'tools';result.mkdir(exist_ok=True)
shutil.copyfile(out/'usbmuxd-1.1.1/src/usbmuxd',result/'pte-usbmuxd')
for folder,name,cgo,extra in [('iphone-usb','pte-iphone-usb','1',['-tags','libusb','./cmd/pte-iphone-usb']),('iphone-pair','pte-iphone-pair','0',['.'])]:
 e=env.copy();e.update(GOOS='linux',GOARCH='arm64',CGO_ENABLED=cgo)
 subprocess.run([str(a.go.resolve()),'build','-mod=vendor','-trimpath','-ldflags=-s -w','-o',str(result/name),*extra],cwd=s/'external'/folder,env=e,check=True)
for f in result.iterdir():f.chmod(0o755)
print('iPhone companion processes:',result)
