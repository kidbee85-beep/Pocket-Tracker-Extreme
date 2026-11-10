# TrimUI Brick beta

TrackWerk 0.9.0 is a standalone native KNULLI port. See the [current illustrated guide](../site/guide.html), [quick start](../quick-start-guide.md), and [build instructions](../building.md).

Copy your audio to `SHARE/roms/ports/pockettracker_extreme/data/Samples`. On the device this is `/userdata/roms/ports/pockettracker_extreme/data/Samples`. `SHARE` is the volume, not an extra nested directory.

Existing musical workflows have been user-tested on a physical Brick with no issues reported so far. The full acceptance workbook and USB/interface tests are still in progress. A kernel/driver stall can prevent a launcher timeout from returning to the frontend; a timeout is not a hardware recovery guarantee.

The direct iPhone helpers remain experimental. Direct iPhone capture failed in the user's repeated hardware test with the current setup. Both Brick USB ports were reported to charge only; iPhone model/cable are unspecified. Class-compliant interfaces remain a separate, pending hardware test. They do not prove that a USB-C cable alone makes arbitrary phones or USB audio devices compatible. Existing iPhone pairing records stay local; do not include them in bug reports or releases.

This package does not modify KNULLI, PortMaster, bootloaders, kernels or graphics drivers. SOLIS and other separately installed music apps are not part of TrackWerk.
