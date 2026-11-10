# Pocket Tracker Extreme — Quick Start

Version 0.2.0-beta.3. Use the [illustrated English tutorial](site/guide.html) for the complete workflow.

## Start and choose storage

**Android:** install the signed beta APK, open a file browser and choose **ADD FOLDER…**. Pick a writable media folder. Microphone recording may request microphone permission. Internet-source functions use your network connection.

**TrimUI Brick/KNULLI:** extract the Brick package into `SHARE/roms/ports/`, preserving your existing `pockettracker_extreme/data` folder. Launch **Ports → Pocket Tracker Extreme**. Storage is already set to that `data` folder; there is no Android-style folder grant.

## Make a first beat

1. Open the song, create or choose a chain and phrase, then select a free instrument.
2. Open **INSTRUMENT → LOAD → Samples / Audio**, enter a folder with **A** and highlight a supported audio file.
3. Press **START** to audition; **A** loads it into the selected instrument.
4. Enter notes in the phrase. **START** plays/stops the sequence.
5. Open the instrument's sample editor to trim, zoom or slice. **R + Up/Down** changes zoom in the editor.
6. Save a project copy and export a WAV when ready.

In the file browser **B** closes it. For a parent folder, use **R + Left** or select `..` and press **A**. Files appear when you open their folder; no separate library scan is required.

## Explore the studio

Open the studio's synth section for engine-specific controls, presets and note preview. Load the sound into the current instrument, then use live editing to hear changes while the sequence plays. A synth model and an articulation variant are choices within an engine, not separate independent engines.

Use **INSTRUMENT → LOAD** to reach **Synthesizers / Factory Presets**, **Instrument Presets (.pti)**, **Samples / Audio** and **Soundfonts (.sf2 / .sf3)**. The separate 2,304-file pack belongs in `Presets/` under the selected media folder: 2,048 instruments in `Instruments/PocketStudio_MegaPack`, plus 256 categorized Reverb/Delay/StudioFX/Mastering files. The Brick package prepares it automatically without replacing edited presets.

## Dig a sample from an online source

Use the studio's digging/browser section to search or explore supported sources, audition a result, place in/out markers and render a short excerpt. Imported clips are local WAV files under `Samples/Clips`; the editor can then trim and slice them. Keep source information beside generated clips when backing them up.

Source availability can change. Use material you are authorized to download and sample, and respect each provider's terms. A free-to-listen track or free sample pack is not automatically licensed for redistribution. The release contains no commercial sample library.

## Record an input

Open **INSTRUMENT → RECORD** or **RECORD** in the Sample Editor. This opens the recorder with Start focused; press A on START RECORDING to capture. Choose an available input and record a short test before starting an important session. Waiting for audio and receiving frames are distinct states. Stop keeps a separate WAV; Use Take opens Edit & Slice in a free instrument and protects unsaved edits. There is no software input monitoring. Input recordings are stored under `Samples/Recordings`. Android microphone and USB-audio paths are implemented; compatibility with individual interfaces remains a hardware test. Direct iPhone input on Brick failed in the user's repeated hardware test with the current setup. Both ports were reported to charge only; iPhone model/cable are unspecified. The experimental helper remains included, but direct capture is not working in this tested setup. A class-compliant interface remains a separate, pending hardware test.

## Back up

Copy the entire selected media folder, including Projects, Samples and Presets. Keep a project's original sample paths intact. Android content-URI references do not automatically transfer to Linux; reassign media after moving a project if needed.

## New in beta.2

L + SELECT opens the 64-gesture Punch deck from musical tracker screens. L/R changes the eight-pad bank; D-pad selects and A is a momentary hold. Release A to restore the mix. Style now has 32 palettes and 12 materials, appended after existing IDs. The original STUDIO/OCTA default and saved custom controls remain intact. Use the program-only Brick updater for an existing installation; see `docs/brick/UPDATE-EXISTING-INSTALL.txt`.

## New in beta.3

Set project swing/shuffle on the project screen. The sample browser remembers its previous folder and selection; context help documents navigation. Existing theme/material selections and controls remain unchanged: Style now offers 40 palettes and 18 materials. SELECT opens the paged Studio help.

**Internal resampling:** L + R + START starts/stops a post-FX recording of the tracker mix while playback continues. In Punch, B + Down cycles FREE/1/2/4/8-bar lengths (60-second cap). After recording has stopped, stop song playback before B + Up uses the saved take in a free instrument and opens the editor. This records the app itself; physical microphone/USB input is a separate workflow and test boundary.

**Effect libraries:** EFFECTS > REVERB/DELAY preset > LOAD opens the appropriate file browser. In Studio, choose FX Rack > FX LIBRARY or Master > MASTER LIBRARY. Open `Extreme_Collection`, pick a category and press A to load; B cancels. FX loading preserves master controls and master loading preserves FX controls; ACTIVE remains shared. See `factory/Effect-Collection-Guide.txt` in the app/source package. The 256 new settings also appear after the existing native preset choices.

Existing Brick owners should use the verified Program-Factory-Update kit rather than replace the app folder. Its optional computer preparer adds only missing data/Presets files and keeps edited presets; no reset is required. See [update instructions](brick/UPDATE-EXISTING-INSTALL.txt).
