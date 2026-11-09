# Pocket Tracker Extreme

**A hands-on music studio for Android and TrimUI Brick / KNULLI.**

Public beta candidate **0.2.0-beta.2**, by **Kid Bunka** and contributors.

Pocket Tracker Extreme is an independent fork of [PocketTracker by conanizer and contributors](https://github.com/conanizer/pockettracker), with an expanded synthesis studio, punch-in performance effects, detailed sample editing, groove generation, mastering, and online digging. The original authors and all third-party copyright notices remain credited in [CREDITS.md](CREDITS.md).

[Illustrated website](https://kidbee85-beep.github.io/Pocket-Tracker-Extreme/) · [English tutorial](https://kidbee85-beep.github.io/Pocket-Tracker-Extreme/guide.html) · [Offline website source](docs/site/index.html) · [Community & press](https://kidbee85-beep.github.io/Pocket-Tracker-Extreme/community/) · [License](LICENSE) · [Third-party notices](docs/licenses/THIRD-PARTY-NOTICES.md) · [Build instructions](docs/building.md)

## Make music with your hands

- **Tracker sequencing:** eight stereo tracks, pattern and song arrangement, note and effect editing, resampling, mix and stem export.
- **Synthesis studio:** sound models and articulations using open DSP adapted from Mutable Instruments Braids, Plaits, Rings, Elements, Clouds and Warps, plus drum and other sound models. Model/articulation choices are not a claim of that many independent DSP engines.
- **Live sound design:** engine-specific controls, preset browsing, live instrument updates and note audition.
- **Punch-in performance:** 64 momentary gestures in eight visible eight-pad banks; L + SELECT opens Punch directly.
- **Sample laboratory:** waveform editing, trim, zoom, transient-oriented slicing, slice markers, destructive tools and instrument loading.
- **Drumlab and groove:** build patterns from a selected kit and distribute generated parts across tracker tracks.
- **Mastering and tone:** equalization, dynamics, saturation, delay and reverb, with presets and mix/export tools.
- **Digging:** search supported online sources, audition, place in/out markers and turn an excerpt into a local WAV for editing. Availability depends on the source, network and platform.
- **Recording:** visible RECORD entries on Instrument and Sample Editor, frame/input diagnostics, Stop/Keep and Use Take into Edit & Slice. Android microphone/USB-audio paths and experimental Brick iPhone helpers remain hardware-dependent; see the status matrix.
- **Personal appearance:** 32 palettes, 12 surface materials, visualizers and the animated Extreme splash. Existing theme IDs, defaults and custom controls are preserved.
- **M8 import:** imports the supported sampler/arrangement subset, reports omissions and keeps source files intact. This is not a full M8 synth/effect emulator.
- **Presets:** an extensive built-in studio library and an additional categorized 2,048-instrument factory pack. The Brick launcher prepares missing pack files on first launch, or you can use the optional computer-side preparer; Android users can extract the separately supplied pack into their selected media folder.

## Downloads

Each release provides these separate artifacts:

| File | Purpose |
| --- | --- |
| `Pocket-Tracker-Extreme-0.2.0-beta.2-Android.apk` | Signed Android application: ARM64, ARMv7 (32-bit) and x86-64 |
| `Pocket-Tracker-Extreme-0.2.0-beta.2-Brick.zip` | Complete native ARM64 package for a new Brick / KNULLI installation |
| `Pocket-Tracker-Extreme-0.2.0-beta.2-Brick-Program-Update.zip` | One-program update with hash verification, backup and rollback; existing data and launcher retained |
| `Pocket-Tracker-Extreme-0.2.0-beta.2-R36S-Community-Test.zip` | Separate Linux community-test build for R36S-class handhelds; hardware validation pending |
| `Pocket-Tracker-Extreme-0.2.0-beta.2-Factory-Presets.zip` | Additional presets, particularly for Android storage folders |
| `Pocket-Tracker-Extreme-0.2.0-beta.2-Source.zip` | Matching application, helper sources, notices and build instructions |
| `Pocket-Tracker-Extreme-0.2.0-beta.2-Demo-Project.zip` | Original editable Mint Afterhours demonstration song and samples, CC BY 4.0 |
| `HOW-TO-FIRST-START.txt` | Simple standalone Brick / KNULLI installation guide |
| `SHA256SUMS.txt` | Integrity hashes for the published artifacts |

These packages do not contain KNULLI, PortMaster, SOLIS, proprietary Polyend libraries, another vendor's firmware or a personal sample library. A Polyend Tracker Mini version is not part of this beta.

## Updating without a reset

For an existing Brick installation, use the program-only update kit and [its instructions](docs/brick/UPDATE-EXISTING-INSTALL.txt). It replaces only the native executable. Keep the working KNULLI card, launcher, samples, projects, presets, themes, controller files and settings; no reflash, folder replacement or reset is needed. A verified previous-program backup supports rollback.

The signed public APK updates the public beta.1 identity in place. Earlier private `.studio.debug` builds need their own matching-key update, supplied separately to their owners. Do not uninstall or clear storage to force a mismatched APK installation. [The illustrated upgrade guide](https://kidbee85-beep.github.io/Pocket-Tracker-Extreme/guide.html#updating) explains the distinction.

## Install on Android

1. Back up your existing media folder and projects.
2. Install the Android APK. Allow installation from your chosen file manager if Android asks.
3. Open the app, open a file browser and choose **ADD FOLDER…**. Select a writable media folder using Android's folder picker.
4. Place your own audio under its `Samples` subfolder and projects under `Projects`.
5. If using the optional factory pack, extract it into that media folder so that `Presets/Instruments/PocketStudio_MegaPack` exists.
6. Choose **INSTRUMENT → LOAD → Samples / Audio** or **Instrument Presets (.pti)**.

This public package has application ID `com.conanizer.pockettracker.studio` and a dedicated release signature. Earlier private test APKs used the separate `.debug` package and can remain installed alongside it. Select the desired media folder in each app; Android's folder grant is per application. Do not uninstall a private test build until you have backed up its settings and media.

## Install on TrimUI Brick / KNULLI

1. Fully shut down the Brick, remove its microSD card and open the `SHARE` volume on your computer.
2. Extract the Brick ZIP into `SHARE/roms/ports/`, preserving any existing `pockettracker_extreme/data` folder.
3. Safely eject the card, return it to the powered-off Brick and boot KNULLI.
4. Optionally run `tools/prepare_presets.py` from the extracted app folder on your computer with Python 3. It adds missing factory presets without replacing edits. Otherwise the first launch prepares 2,048 files before the native window/watchdog starts; no universal SD-card preparation time is claimed.
5. Open **Ports → Pocket Tracker Extreme**. Refresh KNULLI's game list if the entry is missing.
6. Copy your samples into `SHARE/roms/ports/pockettracker_extreme/data/Samples`.

The native launcher does not require PortMaster scripts to start. It uses KNULLI's SDL2/audio support and retains the supplied controller mappings. Its display compatibility mode renders through an SDL software surface and clears the whole output, including the borders.

## Beta feature and test status

| Area | Status and practical limit |
| --- | --- |
| Brick launch and musical workflows | The preceding private build/startup path was user-tested with no errors reported. Beta.2 has software/VM validation; physical acceptance of this new version remains pending. |
| Android | ARM64, ARMv7 (32-bit) and x86-64 libraries are packaged. Installation and release runtime checks passed on an ARM64 emulator; every handheld, OS/driver combination and interface is not certified. Android 8/API 26 or newer is required. |
| R36S-class Linux handhelds | Separate community-test package. This is not Android; system SDL2 and the documented Linux ABI are required. Physical R36S controls/audio tests are pending. |
| Synthesis, samples, effects and mastering | Shared native implementation. CPU load, polyphony and very long files remain device-dependent. |
| Online digging | Implemented; online services may change or restrict access. Only material you are authorized to download/sample should be used. |
| USB audio / class-compliant interfaces | Implemented input paths; compatibility with individual devices still needs hardware testing. |
| Direct iPhone USB capture on Brick | Failed in the user's repeated hardware test with the current iPhone/Brick setup. Both USB ports were reported to charge only; the iPhone model/cable are unspecified. The experimental helper remains included, but direct capture is not working in this tested setup. Class-compliant interfaces require a separate, still-pending test. |
| M8 songs | Supported sampler/arrangement subset only. Unsupported synths/effects are reported or omitted. |

No DSP-latency, battery-life or maximum-polyphony claims are made from emulation alone.

## Rights, sources and privacy

The application is distributed under **GNU GPL version 3 or later**. Matching corresponding source and third-party notices must accompany redistributed binaries. See [LICENSE](LICENSE), [CREDITS.md](CREDITS.md), [THIRD-PARTY-NOTICES.md](docs/licenses/THIRD-PARTY-NOTICES.md), and [helper sources](docs/HELPER-SOURCES.md).

Online access and permission to redistribute source code do not automatically grant rights to a song, sample, artwork or video. YouTube and archive services have their own terms. The application does not grant permission to bypass access restrictions or publish another person's work. The public packages contain no downloaded commercial music. Your own sample library is not included in the project.

Android's microphone permission is used for recording. Online sources require internet access. The app has no project-operated analytics or account backend; external source providers receive requests when you use their services. Pairing records for experimental iPhone support remain local and must not be committed or shared.

## Contributing

Report your app version, device, OS version, the operation that failed and a minimal example you are allowed to share. Remove private file paths, pairing records and copyrighted media before uploading logs or projects. Feature reports should distinguish confirmed behavior from device-specific experiments.

Keep the physical controls predictable and document changed key bindings. Preserve original credits and notices. Performance changes need measured evidence and a repeatable test. Public contributions and release notes should use the artist name **Kid Bunka** where a public author credit is needed.


## Support the project

All application features remain free. If you would like to support further development,
[leave an optional PayPal contribution](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=kidbee85%40gmail.com&currency_code=CHF&item_name=Pocket%20Tracker%20Extreme%20development&locale.x=en_US).
Contributions support project development; this is not a charity claim and no payment is required to use or download the software.
