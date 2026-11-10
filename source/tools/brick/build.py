#!/usr/bin/env python3
"""Build the Brick executable with an explicit libc floor and the device's SDL ABI.

No packages are installed and no downloads are performed. Requires CMake, Make,
Zig 0.14.1, and an ARM64 Linux SDL2 SDK (headers and shared link library).
The SDL link library is an SDK input only and MUST NOT be shipped with the port.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import hashlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", required=True, type=Path)
    parser.add_argument("--sdl-sdk", required=True, type=Path,
                        help="SDK root containing include/SDL2 and lib (or usr/)")
    parser.add_argument("--out", type=Path, default=Path("build/brick"))
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--tests", action="store_true")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    zig = args.zig.resolve()
    if subprocess.check_output([str(zig), "version"], text=True).strip() != "0.14.1":
        raise SystemExit("Use the recorded Zig 0.14.1 toolchain for this build.")
    sdk = args.sdl_sdk.resolve()
    if (sdk / "usr/include/SDL2/SDL.h").is_file():
        sdk = sdk / "usr"
    if not (sdk / "include/SDL2/SDL.h").is_file():
        raise SystemExit("SDL2 SDK headers missing")
    libraries = sorted((sdk / "lib").glob("libSDL2-2.0.so.0*"))
    if not libraries:
        raise SystemExit("ARM64 SDL2 shared link library missing")
    library = libraries[0].resolve()
    elf = library.read_bytes()[:64]
    if elf[:6] != b"\x7fELF\x02\x01" or int.from_bytes(elf[18:20], "little") != 183:
        raise SystemExit("SDL2 SDK library must be aarch64 little-endian ELF64")
    scripts = out / "toolchain"
    scripts.mkdir(exist_ok=True)
    for name, command in [("cc", "cc"), ("cxx", "c++"), ("ar", "ar"), ("ranlib", "ranlib")]:
        target = " -target aarch64-linux-gnu.2.31 -mcpu=cortex_a53" if command in ("cc", "c++") else ""
        script = scripts / name
        script.write_text("#!/bin/sh\nexec " + shlex.quote(str(zig)) + " " + command + target + ' "$@"\n')
        script.chmod(0o755)
    toolchain = scripts / "toolchain.cmake"
    toolchain.write_text("\n".join([
        "set(CMAKE_SYSTEM_NAME Linux)", "set(CMAKE_SYSTEM_PROCESSOR aarch64)",
        *['set(%s "%s")' % (key, str(scripts / value).replace('\\', '/')) for key, value in [
            ("CMAKE_C_COMPILER", "cc"), ("CMAKE_CXX_COMPILER", "cxx"),
            ("CMAKE_AR", "ar"), ("CMAKE_RANLIB", "ranlib")]],
        "set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)", ""]))
    sdl = scripts / "sdl2"
    sdl.mkdir(exist_ok=True)
    (sdl / "SDL2Config.cmake").write_text(
        'add_library(SDL2::SDL2 SHARED IMPORTED)\n'
        'set_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_LOCATION "%s" '
        'INTERFACE_INCLUDE_DIRECTORIES "%s")\nset(SDL2_FOUND TRUE)\n' %
        (library.as_posix(), (sdk / "include/SDL2").as_posix()))
    env = os.environ.copy()
    env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-global")
    env["ZIG_LOCAL_CACHE_DIR"] = str(out / "zig-local")
    build = out / "cmake"
    configure = [args.cmake, "-S", str(source / "shell"), "-B", str(build),
                 "-G", "Unix Makefiles", "-DCMAKE_BUILD_TYPE=Release",
                 "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain), "-DPT_HANDHELD=ON",
                 "-DPT_STUDIO_CHECK=" + ("ON" if args.tests else "OFF"),
                 "-DSDL2_DIR=" + str(sdl),
                 "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--allow-shlib-undefined,--strip-all"]
    subprocess.run(configure, env=env, check=True)
    subprocess.run([args.cmake, "--build", str(build), "-j", str(max(1, args.jobs))], env=env, check=True)
    binary = out / "pockettracker_extreme.aarch64"
    shutil.copyfile(build / "pockettracker-sdl", binary)
    binary.chmod(0o755)
    (out / "build-info.json").write_text(json.dumps({
        "target": "aarch64-linux-gnu.2.31", "cpu": "cortex_a53", "zig": "0.14.1",
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "sdl_sdk_sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
        "device_sdl_required": True, "physical_brick_tested": False,
        "configure_command": configure,
    }, indent=2) + "\n")
    print(binary)


if __name__ == "__main__":
    main()
