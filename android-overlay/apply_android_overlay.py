#!/usr/bin/env python3
"""Prepare the exact TrackWerk 1.0.0 Android source in a new directory."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import tempfile


OVERLAY = Path(__file__).resolve().parent


def file_record(path, relative):
    if not path.is_file() or path.is_symlink() or path.resolve() != path:
        raise ValueError(f"Expected a regular source file: {relative}")
    return {"path": relative, "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def digest(entries):
    canonical = json.dumps(entries, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(canonical).hexdigest()


def files(source, *, string_order=False):
    candidates = [path for path in source.rglob("*") if path.is_file() and path.name != ".DS_Store"]
    if string_order:
        candidates.sort(key=lambda path: path.relative_to(source).as_posix())
    else:
        candidates.sort()
    return [file_record(path, path.relative_to(source).as_posix()) for path in candidates]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("common_source", type=Path, help="the unmodified source/ directory from the archive")
    parser.add_argument("target", type=Path, help="an empty destination name outside source/")
    args = parser.parse_args()
    common = args.common_source.resolve(strict=True)
    target = args.target.resolve()
    if not common.is_dir() or target.exists() or target == common or common in target.parents:
        raise ValueError("Use a new destination directory outside the shared source tree")

    manifest = json.loads((OVERLAY / "OVERLAY.json").read_text())
    common_entries = files(common)
    if len(common_entries) != manifest["shared_source_files"] or digest(common_entries) != manifest["shared_source_manifest_sha256"]:
        raise ValueError("The shared source differs from the frozen 1.0.0 tree")
    entries = {entry["path"]: entry for entry in common_entries}
    overlays = {}
    for pair in manifest["entries"]:
        expected = pair["overlay"]
        relative = PurePosixPath(expected["path"])
        name = relative.as_posix()
        if relative.is_absolute() or not relative.parts or ".." in relative.parts or name in overlays:
            raise ValueError(f"Unexpected overlay path: {relative}")
        replacement = OVERLAY / "files" / relative
        if entries.get(name) != pair["source"] or file_record(replacement, name) != expected:
            raise ValueError(f"Android input failed verification: {relative}")
        overlays[name] = replacement
    if len(overlays) != 10:
        raise ValueError("Expected the ten exact Android overlay files")

    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="trackwerk-android-source-", dir=target.parent) as temporary:
        stage = Path(temporary) / "source"
        for name in entries.keys() | overlays.keys():
            destination = stage / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(overlays.get(name, common / name), destination)
        merged = files(stage, string_order=True)
        actual = digest(merged)
        if len(merged) != manifest["android_source_files"] or actual != manifest["android_source_manifest_sha256"]:
            raise ValueError("The assembled Android source failed readback")
        stage.rename(target)
    print(f"Verified {len(merged)} Android source files: {actual}")


if __name__ == "__main__":
    main()
