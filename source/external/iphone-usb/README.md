# Experimental direct iPhone USB audio helper

`pte-iphone-usb` is an audio-only companion for TrackWerk on Linux.
It uses an Apple-specific bulk USB capture protocol, **not USB Audio Class**.
An iPhone is not a standard USB audio interface when plugged into the Brick.
Physical compatibility with the Brick and current iOS versions still needs a
device test. Silence in a playing app may also reflect that app's own capture
restrictions; this helper does not bypass those restrictions.

The transport and outbound templates are based on Daniel Paulus's
[quicktime_video_hack](https://github.com/danielpaulus/quicktime_video_hack), MIT,
commit `d81396e2e7758d98c2a594853b64f98b54a8a871` (see `LICENSE-QVH`). The
recorded protocol fixtures in `internal/protocol/testdata` come from that commit.
This fork implements a small bounded audio parser instead of importing the
upstream media/dictionary parsers. It never requests video, stores video,
modifies the status bar, resets the phone, or changes its security settings.
The sole Go dependency is the BSD-licensed `github.com/google/gousb v1.1.3`,
vendored with its license. A release additionally links libusb; include its
LGPL license, corresponding source and relinking instructions with the build.

## Build and tests

Go 1.23 or later. Set Go's cache/module paths to writable task-local paths.
The pure protocol, audio and simulated cancellation tests need no USB library:

```sh
CGO_ENABLED=0 go test ./...
go test ./internal/protocol -fuzz FuzzAudioParser -fuzztime 20s
```

The **release** needs libusb headers/library and the explicit build tag:

```sh
CGO_ENABLED=1 go build -mod=vendor -trimpath -tags libusb \
  -o pte-iphone-usb ./cmd/pte-iphone-usb
```

For the Brick use `GOOS=linux GOARCH=arm64`, an ARM64 Linux C compiler, and a
matching libusb `pkg-config` file. A build without `-tags libusb` intentionally
reports USB unavailable and is only suitable for portable tests.

The tests validate upstream recorded stereo PCM16 packets, malformed lengths
and all truncations, 44.1/48 kHz conversion, mono/stereo, float clipping, chunk
independence, duration limits, recoverable WAV headers, protected destinations,
cancellation and disconnects. They **do not prove physical iPhone capture**.

## Process contract (version 1)

`pte-iphone-usb list` writes one JSON object and exits zero:

```json
{"protocol_version":1,"devices":[{"id":"USB serial","name":"iPhone"}]}
```

An inaccessible device/library can add an `error` string; no devices yields an
empty array and explanatory `message`. Discovery reads Apple device descriptors
only. It does not activate the capture mode or claim an interface.

Only an explicit `record` activates capture mode:

```sh
pte-iphone-usb record --udid SERIAL --output /path/new-take.wav \
  --rate 48000 --channels 2 --max-seconds 600
```

Supported output is 44100 or 48000 Hz, one or two channels, signed PCM16 WAV.
Maximum duration is 1–600 seconds. Source format must explicitly identify
interleaved packed signed PCM16 or float32, mono/stereo, 8–192 kHz. Other
formats and mid-take format changes produce an error with a valid partial take
when any audio was received. Conversion uses bounded streaming linear
resampling; input NaN/infinities are zeroed and overshoots clipped.

The output must be nonexistent or an empty regular file reserved by the UI.
Existing takes, directories and symlinks are refused. Standard output contains
newline-delimited JSON **only**, never audio or debug logs:

```json
{"protocol_version":1,"state":"recording","message":"Receiving iPhone audio","frames":1024,"rate":48000,"channels":2,"peakL":0.4,"peakR":0.4,"path":"/path/new-take.wav"}
```

States: `starting`, `recording`, `ready`, `error`. `frames` counts actual
converted PCM frames. A recording status appears only after real sample data
arrives and both WAV data and header are synced. `ready` follows finalization
and USB cleanup. Errors preserve a valid partial take; zero-frame files are
removed. SIGTERM/SIGINT stop reads, send the audio-stop command and finalize.
The interface is released and the previous USB configuration restored where
the device remains accessible. The UI should allow a short graceful stop
before enforcing a process kill; periodically updated headers also permit
recovery after an abrupt kill.

## Pairing and compatibility limits

This helper implements raw audio capture, **not lockdownd pairing**. The app can
run its separate, app-scoped pairing companion first and stop only its own
usbmux daemon before opening the raw USB interface. It must never stop a system
daemon or reuse another application's process. Existing normal iPhone trust
and unlock requirements still apply; this raw helper alone cannot promise to
display the Trust dialog. Its first-audio timeout therefore explains unlock,
data-cable and compatibility checks without claiming a successful pairing.
No phone or USB access is required by the unit tests.
