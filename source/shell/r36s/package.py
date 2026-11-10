#!/usr/bin/env python3
"""Package the reviewed public payload for an experimental R36S community test."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
VERSION = "0.2.0-beta.3"
NAME = f"Pocket-Tracker-Extreme-{VERSION}-R36S-Community-Test"
LAUNCHER = "Pocket Tracker Extreme.sh"
APP = "pockettracker_extreme"
ELF_SHA = "5e1fef394d9988b871fb1a73cdf6ae8122be07ddd41e1c540358d5984263acc2"
DB_SHA = "4ef169fcee14ef9563e36ebf731dc6950494e553bf8b56b5fc8aa70181f0d25c"
LICENSE_SHA = "97f35b302b361680ec1e891e95d2d52097bb95abff361434916d99dc1305f127"
ZIP_TIME = (2026, 11, 9, 0, 0, 0)


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")


def files_under(root: Path) -> list[Path]:
    paths = sorted(root.rglob("*"))
    require(not any(path.is_symlink() for path in paths), "Symlink in input payload")
    return [path for path in paths if path.is_file()]


def inspect_stage(stage: Path) -> dict[str, str]:
    app = stage / APP
    require(stage.is_dir() and not stage.is_symlink(), "Clean stage folder missing")
    require(app.is_dir() and not app.is_symlink(), "Clean application payload missing")
    files = files_under(stage)
    forbidden = {"data", ".git", ".DS_Store", "__pycache__", "private-signing", "log.txt", "log.previous.txt"}
    for path in stage.rglob("*"):
        require(not any(part in forbidden for part in path.relative_to(stage).parts),
                "Mutable, private or development data in input stage")
    binary = app / "pockettracker_extreme.aarch64"
    require(binary.is_file() and sha(binary) == ELF_SHA, "Native ELF differs from the public CPU-validated build")
    with binary.open("rb") as stream:
        header = stream.read(20)
    require(header[:6] == b"\x7fELF\x02\x01" and header[18:20] == b"\xb7\x00", "Expected aarch64 ELF64 LE")
    presets = [p for p in files_under(app / "factory/Presets")]
    require(len(presets) == 2304, "Expected 2,048 instrument and 256 effect/master factory presets")
    pack = hashlib.sha256()
    for path in sorted(presets, key=lambda p: p.relative_to(app / "factory/Presets").as_posix()):
        pack.update(path.relative_to(app / "factory/Presets").as_posix().encode("utf-8") + b"\0")
        pack.update(bytes.fromhex(sha(path)))
    require((app / "factory/presets-pack.id").read_text().strip() == pack.hexdigest(),
            "Factory preset pack identifier mismatch")
    for needed in ["tools/prepare_presets.py", "tools/qjs", "tools/yt-dlp", "tools/cacert.pem", "licenses/LICENSE", "HELPER-SOURCES.md"]:
        require((app / needed).is_file(), f"Required clean payload file missing: {needed}")
    for path in files:
        require(path.name not in {"libSDL2.so", "libSDL2-2.0.so.0", "libc.so.6", "ld-linux-aarch64.so.1"},
                "Do not bundle system SDL or libc replacements")
    return {p.relative_to(stage).as_posix(): sha(p) for p in files}


def build(stage: Path, output: Path, cpu_validation: Path | None) -> Path:
    input_hashes = inspect_stage(stage)
    require(sha(HERE / "controllerdb/gamecontrollerdb.txt") == DB_SHA, "Controller database hash mismatch")
    require(sha(HERE / "controllerdb/LICENSE.txt") == LICENSE_SHA, "Controller license hash mismatch")
    fixture_path = REPO / "tests/r36s-launcher/result.json"
    fixtures = json.loads(fixture_path.read_text())
    require(fixtures.get("passed") is True and fixtures.get("tests", 0) >= 26, "Run the R36S launcher fixtures first")
    require(fixtures.get("launcher_sha256") == sha(HERE / LAUNCHER), "Launcher changed since the recorded tests")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pte-r36s-package-") as scratch:
        root = Path(scratch)
        app = root / APP
        shutil.copytree(stage / APP, app)
        # Only target-specific documents and metadata change; executable bytes do not.
        for path in list(app.iterdir()):
            if path.name.startswith("KNULLI-Official-") or path.name in {"HOW-TO-FIRST-START.txt", "UPDATE-EXISTING-INSTALL.txt"}:
                require(path.is_file(), "Unexpected OS shortcut directory")
                path.unlink()
        shutil.copyfile(HERE / LAUNCHER, root / LAUNCHER)
        (root / LAUNCHER).chmod(0o755)
        shutil.copytree(HERE / "controllerdb", app / "controllerdb")
        guide = (HERE / "README.txt").read_text()
        (root / "R36S-COMMUNITY-TEST.txt").write_text(guide)
        (app / "README.txt").write_text(guide)
        (app / "QUICK-START.md").write_text("# R36S community test\n\n" + guide)
        (app / "VERSION.txt").write_text(f"Pocket Tracker Extreme {VERSION}\nExperimental R36S community test; no physical R36S validation.\n")
        (app / "tools/prepare_presets-HOWTO.txt").write_text(
            "OPTIONAL COMPUTER PREPARATION\n\nWith Python 3 installed, open a terminal in pockettracker_extreme/ and run:\n"
            "  python3 tools/prepare_presets.py\n\nThe script verifies the full factory pack and copies only missing presets.\n"
            "Edited presets remain intact. It may create data/Presets and a pack marker.\n"
            "Do not run the handheld launcher at the same time. Wait until it finishes\n"
            "and safely eject the SD card. If you skip this, the handheld prepares the\n"
            "same presets on first launch. See ../README.txt for the complete setup.\n")
        metadata = {
            "format": "pocket-tracker-extreme-community-package-v1", "version": VERSION,
            "title": "Pocket Tracker Extreme — R36S Community Test", "creator": "Kid Bunka",
            "target": "R36S / compatible aarch64 Linux userspace", "experimental": True,
            "physical_hardware_tested": False, "official_portmaster_catalog_entry": False,
            "requirements": {"architecture": "aarch64", "libc": "glibc >= 2.31", "SDL2": ">= 2.0.18", "shell": "Bash"},
            "install": {"KNULLI_R36S": "SHARE/roms/ports", "ArkOS_style": "EASYROMS/ports; /roms/ports or /roms2/ports"},
            "binary_sha256": ELF_SHA, "controllerdb_sha256": DB_SHA,
            "controllerdb_commit": "07af1d50187e94c861c9f9f5d8a55b148f11306b",
            "controllerdb_policy": "Fallback only when SDL_GAMECONTROLLERCONFIG_FILE is unset; preserve explicit empty and frontend inline mappings",
            "OS_image_included": False, "system_libraries_replaced": False,
            "USB_iPhone_R36S_validation": "pending physical hardware testing",
            "source": "Matching Pocket Tracker Extreme 0.2.0-beta.3 source bundle; shell/r36s/package.py",
        }
        write_json(app / "community-test.json", metadata)
        write_json(app / "port.json", {
            "version": 4, "name": NAME + ".zip", "items": [LAUNCHER, APP], "items_opt": [],
            "attr": {"title": "Pocket Tracker Extreme (R36S Community Test)", "porter": ["Kid Bunka"],
                "desc": "Experimental native ARM64 Linux music studio. No physical R36S testing; see README.txt.",
                "inst": "Extract complete ZIP into your firmware's ports folder. Preserve existing data. No PortMaster startup dependency.",
                "genres": ["other"], "rtr": False, "exp": True, "runtime": [], "store": [], "availability": "full",
                "reqs": [], "arch": ["aarch64"], "min_glibc": "2.31"}})
        (app / "gameinfo.xml").write_text('''<?xml version="1.0" encoding="UTF-8"?>
<gameList><game><path>./Pocket Tracker Extreme.sh</path>
<name>Pocket Tracker Extreme (R36S Community Test)</name>
<desc>Experimental native ARM64 Linux music studio by Kid Bunka. R36S physical display, controls, performance and recording remain untested. Read README.txt.</desc>
<image>./pockettracker_extreme/screenshot.png</image><releasedate>20261109T000000</releasedate>
<developer>PocketTracker contributors / Pocket Tracker Extreme</developer><publisher>Kid Bunka community beta</publisher><genre>Other</genre><players>1</players>
</game></gameList>
''')
        validation = app / "validation"
        validation.mkdir()
        write_json(validation / "launcher-fixtures.json", fixtures)
        if cpu_validation:
            result = json.loads((cpu_validation / "result.json").read_text())
            require(result.get("passed") is True and result.get("binary_sha256") == ELF_SHA,
                    "CPU validation does not match this binary")
            require(result.get("physical_R36S_tested") is False, "CPU evidence must not claim physical testing")
            write_json(validation / "cortex-a35-result.json", result)
        (validation / "README.txt").write_text(
            "Launcher tests use host Bash and owned dummy processes. The optional Cortex-A35\n"
            "record tests the exact native ELF in an emulated ARM64 Linux environment.\n"
            "Neither is physical R36S display/controller/audio/performance/USB validation.\n")
        require(sha(app / "pockettracker_extreme.aarch64") == ELF_SHA, "Native executable was altered")
        require(inspect_stage(stage) == input_hashes, "Input stage changed during packaging; retry from a stable stage")
        payload_files = files_under(root)
        manifest = {
            "package": NAME, "binary_sha256": ELF_SHA, "physical_R36S_tested": False,
            "files": [{"path": p.relative_to(root).as_posix(), "bytes": p.stat().st_size, "sha256": sha(p)} for p in payload_files],
        }
        write_json(app / "payload-manifest.json", manifest)
        archive = output / (NAME + ".zip")
        temporary_archive = output / (NAME + ".zip.tmp")
        try:
            with zipfile.ZipFile(temporary_archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
                for path in files_under(root):
                    info = zipfile.ZipInfo(path.relative_to(root).as_posix(), ZIP_TIME)
                    info.create_system = 3
                    executable = path.name == LAUNCHER or bool(path.stat().st_mode & stat.S_IXUSR)
                    info.external_attr = ((stat.S_IFREG | (0o755 if executable else 0o644)) << 16)
                    info.compress_type = zipfile.ZIP_DEFLATED
                    with path.open("rb") as stream, z.open(info, "w", force_zip64=True) as target:
                        shutil.copyfileobj(stream, target, 1024 * 1024)
            with zipfile.ZipFile(temporary_archive) as z:
                require(z.testzip() is None, "ZIP CRC check failed")
                require(hashlib.sha256(z.read(APP + "/pockettracker_extreme.aarch64")).hexdigest() == ELF_SHA, "Archived native ELF mismatch")
                require(not any("/data/" in n or n.startswith("../") or n.startswith("/") for n in z.namelist()), "Unexpected mutable or unsafe ZIP path")
            os.replace(temporary_archive, archive)
        finally:
            if temporary_archive.exists():
                temporary_archive.unlink()
        (output / (NAME + ".sha256")).write_text(sha(archive) + "  " + archive.name + "\n")
        write_json(output / (NAME + "-manifest.json"), manifest)
        print(json.dumps({"archive": str(archive), "bytes": archive.stat().st_size,
                          "sha256": sha(archive), "files": len(payload_files) + 1,
                          "presets": 2304, "physical_R36S_tested": False}))
        return archive


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--brick-stage", required=True, type=Path, help="Stable, privacy-reviewed public payload stage")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--cpu-validation", type=Path, help="Optional exact-binary Cortex-A35 emulation result directory")
    args = parser.parse_args()
    try:
        build(args.brick_stage.resolve(), args.output_dir.resolve(), args.cpu_validation.resolve() if args.cpu_validation else None)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"R36S packaging failed: {error}\n")


if __name__ == "__main__":
    main()
