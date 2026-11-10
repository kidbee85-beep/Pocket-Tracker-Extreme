# Public beta release procedure

The review set consists of a signed Android APK, native Brick ZIP, factory presets, matching full source and checksums. Preparing these files does not publish them.

1. Freeze one source revision and version for both platforms. Preserve upstream and third-party notices.
2. Build Android with the maintainer's stable release key outside this repository. Verify the certificate and package/version metadata with `apksigner` and `aapt`.
3. Build the Brick executable against the documented ABI and package its matching launcher, assets, factory presets, notices and helper-source information.
4. Run software checks, install/start the APK in the recorded test environment, and record the distinction between emulator and physical hardware results.
5. Confirm no personal media, pairing records, signing material, private absolute paths or local configuration enter the source/artifact set. Check every ZIP and all shipped license assets.
6. Review the English website, tutorial and feature/test matrix. Resolve every broken internal link. Downloads must name the actual release artifacts.
7. Review the final3–4minute film and soundtrack locally. The author must explicitly approve it before any YouTube or other public upload. The rendered device frame is a presentation mask around genuine application output, not physical-device camera footage.
8. After approval, create the GitHub release, attach the reviewed files and SHA256SUMS, and enable GitHub Pages for `docs/site` through the supplied Pages workflow. No credentials belong in this repository.

The optional validation workflow builds developer APKs and host checks; a CI/debug key is not the release identity. Signing and public release publication remain maintainer steps.
