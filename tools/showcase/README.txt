Native showcase capture utilities

These optional development tools render the actual TrackerLayout and AudioEngine
used by Pocket Tracker Extreme. They are not features of the shipping application.
They create scripted software captures, not physical-device camera footage.

stills.cpp renders menu illustrations from a supplied real .ptp project.
capture.cpp renders a 640x480,24fps RGB stream and diagnostic float-stereo audio.
source_capture.cpp uses the shipping network backend to save genuine Wikimedia
Commons search/preview/marker/render states. Its example is Cloud rap beat by
Zanahary, CC0-1.0: https://commons.wikimedia.org/wiki/File:Cloud_rap_beat.ogg

Build the native libraries first with CMake (shell/ or native/). Link these tools
against pt-ui, songcore, pockettracker, opusfile, opus, ogg, faad2 and pt-mutable;
include native/. source_capture.cpp additionally includes shell/ and links the
shell/linux/source-sampler.cpp object. Use the same C++17 compiler and build options
as the libraries. The website and review-package manifests record the production
capture inputs, project, rights information and software-only scope.

Usage:
  stills PROJECT.ptp APPDATA OUTPUT
  source_capture OUTPUT
  capture PROJECT.ptp APPDATA SOURCE_SNAPSHOTS OUTPUT FRAME_COUNT

The supplied Mint Afterhours demo project is a separate release asset. Prepare its
original samples in APPDATA/Samples and the factory preset pack in APPDATA/Presets.
The capture script also expects Kid_Bunka_Original_Crate_Source.wav in Samples/;
this is an original native-engine render supplied in the showcase music package.
The source snapshots are recorded before rendering and replayed for edited timing;
no fictional network result or unavailable feature is drawn. Media permissions
remain specific to each source. None of these tools publishes content.
