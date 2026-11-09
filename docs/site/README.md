# Pocket Tracker Extreme — GitHub Pages site

This directory is a self-contained English website and illustrated field guide.
It is plain HTML, CSS and JavaScript. There is no package install, build step,
tracking, external font service or ChatGPT hosting dependency.

## Preview locally

From this directory, run `python3 -m http.server 8765`, then open
`http://localhost:8765/`. Do not expose the preview server outside your computer.

## Publish after approval

1. Publish the approved source and release artifacts in
   `kidbee85-beep/Pocket-Tracker-Extreme`.
2. Use release tag `v0.2.0-beta.2` and the exact filenames in the download links.
3. In repository Settings → Pages, choose **GitHub Actions**.
4. Run the **Publish approved website** workflow manually. It publishes only
   `docs/site`, not the full source tree or private production files.

The expected URL is:
`https://kidbee85-beep.github.io/Pocket-Tracker-Extreme/`.

The workflow is manual by design: no push automatically publishes a new draft.
The film section remains an honest review notice until a video is approved.

## Asset notes

Images are local and can be replaced at their stable asset names. The primary Studio, sample editor and preset
illustrations are fresh canonical beta-renderer captures in the ULTRAVIOLET theme.
The Android image is a current signed-beta emulator capture. The Input image is a
representative earlier actual app capture of the shared interface. The Digging illustrations use a real
Wikimedia Commons CC0 source, with verified HTTPS search, preview and a ten-second
PCM region render. Record updated capture provenance when
replacing them. The hero is project artwork, not a photograph of physical hardware.

Do not add a public video URL until approval. Keep project claims aligned with
the beta status table and actual hardware evidence.

The compatibility section separates Android CPU/OS requirements from native Linux
packages, and separates emulator/build evidence from physical-device testing.
The R36S community package is explicitly experimental. A structured handheld
feedback form is provided in `.github/ISSUE_TEMPLATE/handheld-compatibility.yml`.

## First start and support

The Brick start-here path links the official, untouched KNULLI Scarab 20260511
image for original Brick/Hammer and the verified reference digest. KNULLI is not
inside this site or app ZIP. Existing working cards can skip directly to the
standalone app installation. The optional computer preset preparer and the
first-launch preparation phase are documented separately from native startup.

The PayPal button is an ordinary external link for optional support to Kid Bunka.
There is no embedded PayPal script, paid feature gate or automatic payment.
The public release checklist is HOW-TO-FIRST-START.txt.
