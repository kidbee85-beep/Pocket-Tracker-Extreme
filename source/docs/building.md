# Building TrackWerk

The public source contains the application, the frozen NewPipe Java sources, Mutable DSP sources, iPhone helper sources and the matching license notices. Version names come from `app/build.gradle.kts` for Android and the native shell.

## Android

Requirements: JDK 21, Android SDK platform 36, NDK 28.2.13676358, SDK CMake 3.22.1, and the Gradle 8.14.3 wrapper. `targetSdk` remains 34 for this beta; `minSdk` is 26. Maven/Google dependencies are resolved by Gradle on the first build.

Create an untracked `local.properties` containing your SDK location, or set the normal Android SDK environment variables. Then run:

```sh
./gradlew :app:assembleDebug
./gradlew :app:assembleRelease
```

A fresh clone builds an **unsigned** release; it never silently signs a release with a debug key. For a maintainer build, keep the keystore and properties outside the repository and use:

```sh
./gradlew -PptSigningProperties=/private/path/release-signing.properties :app:assembleRelease
```

For a maintainer who still has the original development signing key, `-PptLegacyPrivateUpdate=true :app:assembleRelease` creates an optimized update for the earlier `.studio.debug` identity. This opt-in route uses that existing development key and retains R8/native release optimization. It is a separate private migration artifact, not the public `.studio` release. A matching package ID, original signing certificate and increasing version code are required for an in-place update; do not uninstall the old app to make a mismatched package install.

The properties keys are `storeFile`, `storePassword`, `keyAlias` and `keyPassword`; use the example at the repository root as a template. Never commit the actual file or key. Keep the release key for every future update of application ID `com.conanizer.pockettracker.studio`.

The APK includes `arm64-v8a`, `armeabi-v7a` and `x86_64`. The two 64-bit targets use 16 KiB ELF load alignment; all uncompressed native APK entries are 16 KiB ZIP aligned. ARMv7 remains a legacy 32-bit target. Run `python3 tools/release/check_apk.py /path/to/app-release.apk` to inspect the actual output. These build checks do not certify a physical Android device.

Software instrumentation can be built with `:app:assembleDebugAndroidTest`. The framework-only runners include `CaptureSmoke`, `SourceSmoke`, `RhinoSmoke` and `ReleaseBridgeSmoke`. The capture runner is explicitly restricted to an emulator with a virtual audio input. For release-classpath tests use `-PptTestBuildType=release`; these tests do not imply physical interface compatibility.

## TrimUI Brick / KNULLI

Requirements: Python3, CMake3.22 or newer, Make, Zig0.14.1, and a matching ARM64Linux SDL2 link SDK. KNULLI supplies the actual graphics/audio SDL2 library on the device; the SDK library is **never** copied into the package.

```sh
python3 tools/brick/build.py --zig /path/to/zig --sdl-sdk /path/to/sdk/usr --cmake cmake --out build/brick --tests
```

The output is `build/brick/pockettracker_extreme.aarch64`. Target: `aarch64-linux-gnu.2.31`, Cortex-A53. The standalone launcher is `shell/brick/Pocket Tracker Extreme.sh`. Preserve executable permissions when packaging it. Its default surface display mode is a compatibility choice for the tested KNULLI setup; it does not replace any device driver.

Run the launcher process tests from the source root:

```sh
python3 tests/brick-launcher/check_launcher.py
```

The factory parameter files are in `factory/Presets`. Copy them to the package's `factory/Presets`, together with `factory/presets-pack.id`; the launcher adds missing presets without replacing edited user files.

## Native development

Install SDL2 development headers/library and configure the native shell:

```sh
cmake -S shell -B build/host -DCMAKE_BUILD_TYPE=Release -DPT_STUDIO_CHECK=ON
cmake --build build/host -j4
```

The engine and UI are the same native code shared with the Android application. Developer/demo capture modes, if used for screenshots or films, are not a claim that physical-device footage was recorded.

## Companion processes

`external/build-quickjs.py` rebuilds the bundled QuickJS from its included source archive. `tools/brick/build_iphone.py` builds the iPhone companion processes with Go1.27.3, Zig0.14.1, Make and pkg-config; its source dependencies and Go vendor trees are included. The `--libusb-source` option supports rebuilding with a modified LGPL libusb copy.

See [HELPER-SOURCES.md](HELPER-SOURCES.md) for the scope and exact source identities. System `curl`, FFmpeg and SDL are provided by KNULLI and are not redistributed in the Brick package.
