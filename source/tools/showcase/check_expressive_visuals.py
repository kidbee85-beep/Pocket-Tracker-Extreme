#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Link the native visual contract to an existing, current host build and run it.

Configure/build native with PT_STUDIO_CHECK=ON first. No network, audio device,
personal media or window system is required. Screens are drawn by production UI.
"""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--cxx', default='clang++')
args = parser.parse_args()
source = Path(__file__).resolve().parents[2]
native = source / 'native'
build = args.build.resolve()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
libraries = [build / name for name in (
    'libpt-ui.a', 'libsongcore.a', 'libpockettracker.a', 'libopusfile.a',
    'vendor/opus/libopus.a', 'vendor/ogg/libogg.a', 'libfaad2.a',
    'studio/libpt-mutable.a')]
for library in libraries:
    if not library.is_file():
        parser.error('Missing native library: ' + str(library))
executable = out / 'expressive-visuals'
subprocess.run([args.cxx, '-std=c++17', '-O2', '-UNDEBUG', '-I' + str(native),
                str(source / 'tests/studio/expressive_visuals.cpp'),
                *map(str, libraries), '-o', str(executable)], check=True)
subprocess.run([str(executable), str(out / 'frames')], check=True)
