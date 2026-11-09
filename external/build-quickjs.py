#!/usr/bin/env python3
"""Build the bundled MIT QuickJS helper without installing or downloading tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tarfile


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zig',type=Path,required=True)
    p.add_argument('--host-cc',default='cc')
    p.add_argument('--out',type=Path,default=Path('build/quickjs'))
    a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    archive=Path(__file__).resolve().parent/'quickjs-2026-06-04.tar.xz'
    expected='b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a'
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=expected:raise SystemExit('QuickJS source hash differs')
    with tarfile.open(archive) as t:
        for member in t.getmembers():
            try:(out/member.name).resolve().relative_to(out)
            except ValueError:raise SystemExit('Unsafe source archive path')
            if member.issym() or member.islnk():raise SystemExit('Source archive link refused')
        t.extractall(out)
    source=out/'quickjs-2026-06-04'
    flags=['-ffile-prefix-map='+str(Path.home())+'=/build','-ffile-prefix-map='+str(source)+'=/src/quickjs','-O2','-D_GNU_SOURCE','-DCONFIG_VERSION="2026-06-04"','-I'+str(source),'-funsigned-char']
    common=[source/n for n in ['quickjs.c','dtoa.c','libregexp.c','libunicode.c','cutils.c','quickjs-libc.c']]
    env=os.environ.copy();env['ZIG_GLOBAL_CACHE_DIR']=str(out/'zig-global');env['ZIG_LOCAL_CACHE_DIR']=str(out/'zig-local')
    def run(command,cwd=None):subprocess.run([str(x) for x in command],env=env,cwd=cwd,check=True)
    run([*shlex.split(a.host_cc),*flags,source/'qjsc.c',*common,'-lm','-lpthread','-o',out/'qjsc-host'])
    run([out/'qjsc-host','-c','-o',out/'repl.c','-m','repl.js'],cwd=source)
    run([a.zig.resolve(),'cc','-target','aarch64-linux-gnu.2.31','-mcpu=cortex_a53',*flags,
         source/'qjs.c',out/'repl.c',*common,'-lm','-ldl','-lpthread','-Wl,--strip-all','-o',out/'qjs'])
    binary=out/'qjs'
    (out/'build-info.json').write_text(json.dumps({'version':'2026-06-04','source_sha256':expected,
       'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'bytes':binary.stat().st_size,
       'target':'aarch64-linux-gnu.2.31 cortex_a53'},indent=2)+'\n')
    print(binary)


if __name__=='__main__':main()
