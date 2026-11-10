# TrackWerk 0.9 beta — corresponding source

TrackWerk is an independent Kid Bunka fork of PocketTracker. Original authors
and dependency notices remain in LICENSE, CREDITS.md and docs/licenses/.

The public Android application ID is `com.kidbunka.trackwerk`. Its Android
styles are Theme.TrackWerk and its help panels use the project's own TW mark.
The shared Java/JNI source package names remain implementation details.

Use the parent BUILD-TRACKWERK-0.9.txt with source/, font-authoring/ and
brand-authoring/ kept together. The public release includes all corresponding
engine sources and the preferred editable geometry and font inputs.

This identity installs beside older PocketTracker/Extreme Android packages.
Keep the older app and export any internally stored work before migrating.
Grant the new app the existing shared media folder. Existing samples and
projects need not be deleted or rearranged. Private app settings and folder
permissions are not automatically inherited across Android application IDs.

Optional ptLegacyApplicationId and ptLegacyPrivateUpdate Gradle settings are
maintainer compatibility routes for existing private installs only. Their APKs
are not included in the public release. Signing keys are never source assets.

Public guide: https://kidbee85-beep.github.io/Pocket-Tracker-Extreme/
