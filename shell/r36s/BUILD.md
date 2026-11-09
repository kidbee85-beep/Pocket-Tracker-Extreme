# R36S community-test package

This target repackages the privacy-reviewed public Brick payload without changing
the ARM64 application or companion executables. It does not build an OS image.
The separate launcher defaults to SDL's renderer and preserves frontend settings.

From the repository root:

```sh
python3 tests/r36s-launcher/check_launcher.py
python3 shell/r36s/package.py --brick-stage /path/to/clean/brick-stage \
  --output-dir /path/to/deliverables \
  --cpu-validation /path/to/cortex-a35-validation
```

Only a clean, approved public stage is acceptable input. The generator checks the
exact pinned native ELF, preset pack, controller database and its license, rejects
symlinks and mutable data, replaces target-specific metadata, and checks ZIP CRCs.
It intentionally excludes Brick-specific OS download shortcuts. Computer preset
preparation remains optional; `tools/prepare_presets.py` is reused unchanged.

The resulting ZIP contains a sibling `Pocket Tracker Extreme.sh` and
`pockettracker_extreme/`, suitable for the firmware's ports directory. The source
files in this directory are part of the matching GPL source release. The pinned
database source and zlib license are in `controllerdb/`.

Tests exercise the actual Bash launcher with owned process fixtures. An optional
record of exact-binary Cortex-A35 emulation is packaged as separate evidence.
Neither is a physical R36S display, controller, audio or USB acceptance test.
