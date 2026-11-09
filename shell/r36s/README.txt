POCKET TRACKER EXTREME 0.2.0-beta.1
R36S COMMUNITY TEST — EXPERIMENTAL
Kid Bunka / PocketTracker contributors

This is a native Linux application, not an Android APK or an operating-system
image. It uses the same ARM64 application binary as the public Brick beta with
a separate firmware-neutral launcher. The R36S hardware has NOT been tested.
Display, controls, audio latency, performance, recording and USB compatibility
still need community testing. An emulated Cortex-A35 CPU test passed 24 groups;
that does not establish physical-device compatibility.

REQUIREMENTS
64-bit aarch64 Linux userspace, glibc 2.31 or newer, SDL2 2.0.18 or newer,
working SDL video/audio/game-controller support, Bash and standard shell tools.
The system supplies SDL2, curl and FFmpeg. The package does not replace system
SDL, libc, firmware or controller configuration. A 64-bit CPU alone is not
enough if the installed userspace is 32-bit. R36S clones differ.

INSTALL ON AN ALREADY WORKING R36S SYSTEM
1. Back up existing projects and the pockettracker_extreme/data folder first.
2. Extract the whole ZIP, preserving the two sibling items:
     Pocket Tracker Extreme.sh
     pockettracker_extreme/
   Put them inside your firmware's ports folder:
     KNULLI R36S: SHARE/roms/ports/
     ArkOS-style: EASYROMS/ports/ (normally /roms/ports or /roms2/ports)
   Do not create an extra nested package folder. Do not replace a complete
   existing ports list or delete your existing data folder during an update.
3. Safely eject the SD card, insert it, and refresh the frontend's games list
   if necessary. Open Ports -> Pocket Tracker Extreme.
4. The first launch prepares 2,048 factory presets in the local data folder.
   This can take time on a slow SD card. Watch the log if it fails to launch.
   Subsequent launches skip preparation when the pack identifier matches.

OPTIONAL: PREPARE PRESETS ON YOUR COMPUTER
If Python 3 is installed, open a terminal in pockettracker_extreme/ and run:
  python3 tools/prepare_presets.py
This verifies the factory pack and copies only missing presets to data/Presets.
Existing edited presets remain intact. Wait for completion, then safely eject.
See tools/prepare_presets-HOWTO.txt. Preparation is optional; the launcher can
also do it on the handheld. Do not run both at the same time.

KNULLI OPERATING SYSTEM — SEPARATE DOWNLOAD
The official KNULLI Scarab 20260511 release lists Game Console R36S:
  https://github.com/knulli-cfw/knulli-linux/releases/tag/20260511
R36S-specific asset: knulli-rk3326-r36s-scarab-20260510.img.gz
Use the firmware maker's R36S installation instructions and confirm your exact
hardware variant before flashing. An OS image flash erases the target card.
NEVER flash the TrimUI Brick image to an R36S. This application ZIP contains
no OS image and is not a claim that KNULLI or ArkOS have certified this port.

YOUR SAMPLES, SONGS AND PRESETS
The app's writable media folder is pockettracker_extreme/data/:
  Samples/       Put your own audio here; keep any useful category subfolders.
  Presets/       Writable instrument and effect presets, prepared on first run.
  Projects/      Suggested location for your saved .ptp songs.
  Samples/Clips  Imported online excerpts (created when needed).
  Samples/Recordings  Input recordings (created when needed).
Create Samples and Projects if needed. Open INSTRUMENT -> LOAD -> Samples / Audio,
enter a folder with A, audition with START and load with A. No Android folder
permission or separate library scan is needed. B closes the browser; R + Left
or the '..' entry goes to the parent folder. R + Up/Down changes sample-editor
zoom. For M8 imports, include the samples with the song and retain their folder
structure; only the implemented import subset is supported, not every M8 engine
or effect. Keep the original M8 project and samples as a separate backup.

CONTROLS AND CONTROLLER MAPPINGS
D-pad moves; mapped A confirms/edits; mapped B backs out. L/R are context
modifiers; START plays/stops or auditions in the browser. SELECT opens context
help. Follow the in-app help for combinations. The SDL mappings may make the
physical button labels differ, so test navigation before editing a song.

The launcher preserves SDL_GAMECONTROLLERCONFIG, SDL_GAMECONTROLLERCONFIG_FILE,
SDL video/audio/render-driver settings and LD_LIBRARY_PATH. An explicitly empty
SDL_GAMECONTROLLERCONFIG_FILE stays empty. Only when that variable is UNSET does
it try /tmp/gamecontrollerdb.txt, then $HOME/.config/gamecontrollerdb.txt, then
the bundled pinned SDL_GameControllerDB. Inline frontend mappings remain intact.
The bundled database includes related controller mappings but does not guarantee
correct input on every R36S clone. It is unmodified; its zlib license and pinned
source information are in controllerdb/. No PortMaster/control script is sourced.

DEFAULTS AND TROUBLESHOOTING
Default video mode is the normal SDL renderer; audio buffer is 512 frames.
The launcher does not force a system video or audio driver. For a deliberate
diagnostic test you can set POCKETTRACKER_VIDEO=surface in the launch environment;
this is an alternative path, not an R36S fix established by hardware testing.

Inspect pockettracker_extreme/log.txt (previous large log: log.previous.txt).
The first marker is flushed before native graphics start. If no first presented
frame appears within 25 seconds after native launch, the launcher sends TERM to
its own child, waits up to eight seconds, then sends KILL. It never targets an
unrelated process. A kernel driver stall can still prevent prompt return; this
is not a guarantee against every firmware hang. Preset preparation is separate
from the native first-frame deadline. Do not interrupt an SD write by removing
the card. If an error persists, share the log plus firmware, model/clone,
screen/controller details and the steps that reproduce it. Exclude personal
sample files, pairing records and credentials from reports.

ONLINE SOURCES AND RECORDING
Online browsing needs a working network connection and compatible system tools.
Source availability and rights vary; use material you are authorized to sample.
Microphone/interface/iPhone USB input paths remain unverified on R36S. This
package makes no direct iPhone or class-compliant USB hardware success claim.

LICENSE AND SOURCE
Application: GPL-3.0-or-later; see licenses/ and the matching public source bundle.
Independent bundled tools retain their own licenses; see HELPER-SOURCES.md and
licenses/. Controller database: zlib license, controllerdb/LICENSE.txt.
This is a community-test build, not an official PortMaster catalog release.
Do not infer physical testing from the emulator/launcher evidence in validation/.
