# Frozen NewPipe integration

Runtime Java source is frozen from TeamNewPipe/NewPipeExtractor v0.27.0.
Two record declarations (Description, SubscriptionItem) are converted to equivalent
immutable Serializable value classes for API26/D8 compatibility; their constructors,
component accessors and value semantics are preserved. Extraction logic is unchanged.
See PROVENANCE.json for the full commit, archive SHA-256, source URLs, dependency
versions/hashes and the command to regenerate the committed Protobuf-lite Java.

The local Java-library build compiles upstream extractor + timeago parser,
upstream Nanojson's pinned Java sources, and the generated Protobuf-lite files.
It uses Java 17 bytecode. The Android app keeps its existing JVM target 11 and
enables desugar_jdk_libs_nio for API 26 support, as required by upstream.
No upstream publishing, checkstyle or network test fixtures run during app builds.

YouTubeFacade uses only public metadata, playlist entries and audio-only progressive
stream descriptors. Its metadata Downloader rejects media hosts and audio/video
bodies. The source sampler's audio backend consumes only the selected audio URL.
It provides no account login, user cookies, DRM keys or authentication bypass.
Search accepts genre keywords; this is keyword search, not a YouTube genre API.

Live JVM verification on 2026-11-08: search yielded 20 items (9 playlists),
one playlist yielded the bounded 200 entries, and the public Blender Foundation
Big Buck Bunny video resolved to M4A/AAC. A range request read 1,024 audio bytes
with HTTP 206 and Content-Type audio/mp4. No video media was requested.
This verifies current public access from the test network, not permanent service
availability. Android playback and sample rendering require app integration tests.
