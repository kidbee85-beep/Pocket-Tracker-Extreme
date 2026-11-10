pte-iphone-pair — TrackWerk 0.1.0.8-Brick

Purpose
-------
Normal, user-approved iPhone USB host pairing for the experimental direct
audio input. This helper does not capture audio. The separately bundled
pte-iphone-usb performs audio-only QuickTime USB transport after pairing.

Command
-------
pte-iphone-pair pair --udid SERIAL --socket /absolute/private/mux.sock --timeout 90

The owning application starts its own foreground usbmuxd with the mandatory
scoped flags -f -p -C PRIVATE_PAIRDIR -S PRIVATE_SOCKET -P PRIVATE_PIDFILE.
The caller stops only its own child process before opening the audio endpoint.
This helper never launches, signals or configures a daemon, never accesses the
default system socket, and never enables a service at device startup.

The UNIX socket must already exist in a directory owned by the current user,
with no group/other access. The selected UDID is required and is matched only
to USB entries. Wi-Fi entries are ignored. USB descriptors with or without the
modern UDID hyphen identify the same physical phone.

Output is newline-delimited JSON, protocol_version 1. States are connecting,
pairing, trust, unlock, paired, error and cancelled. Exit 0 means a stored
normal pair record was used to establish a lockdown session; it does not prove
that the audio endpoint or any audio sample will work. Exit 1 is failure or
cancel; exit 2 is invalid arguments/private socket. --version is read-only.

Trust and cancellation
----------------------
The normal Pair request may cause the iPhone to ask for Trust and its passcode.
The user must approve on the phone. Denial is terminal. A locked existing host
session is retried without generating a replacement identity. Pair success is
followed by a fresh verified StartSession. The wait is bounded to 1–90 seconds.
SIGTERM/SIGINT cancel. A blocked upstream socket operation is bounded by helper
process exit; the parent owns cleanup of its daemon and temporary directory.

No supervision, jailbreak, passcode bypass or DRM bypass is implemented.
Pair certificates/private keys remain in the app-owned pairing directory via
usbmuxd. They must never be included in logs, fixtures, release ZIPs or reports.
Only effective private storage permissions are acceptable. On exFAT the app
may use private temporary RAM storage and request Trust again in a later session.

Dependency and reproducible build
---------------------------------
go-ios: github.com/danielpaulus/go-ios
Version: v1.3.3-0.20261109092416-63c0d5705a01
Commit: 63c0d5705a01 (resolved 2026-11-09)
License: MIT; original license and all imported dependency notices in vendor/.
The exact dependency graph and module hashes are in go.mod/go.sum. The source
needed by this command is vendored; building does not require a network.

GOOS=linux GOARCH=arm64 CGO_ENABLED=0 go build -mod=vendor -trimpath -ldflags='-s -w' -o pte-iphone-pair .
go test -mod=vendor -race -timeout 30s ./...
GOOS=linux GOARCH=arm64 CGO_ENABLED=0 go test -mod=vendor -c -o iphone-pair-tests .

Use Go 1.27.3 as supplied in the build manifest, or a compatible newer version.
The Linux ARM64 executable is static and does not need a TLS shared library.

Checks
------
Eight tests cover normal Trust retry, preserving a locked identity, respecting
denial, validating after SavePairRecord, exact USB serial selection, argument
and private-path rejection, the real go-ios USBMux/lockdown message exchange
against a local simulated phone, and a blocked-read process timeout.

The simulated phone deliberately disables session TLS. These tests establish
control flow and output privacy, not real iPhone/iOS/KNULLI compatibility.
The physical USB host port, iOS Trust dialog, audio endpoint, reliable long takes
and cleanup after cable removal still require an actual Brick+iPhone test.

Primary sources
---------------
https://github.com/danielpaulus/go-ios/tree/63c0d5705a01/ios
https://github.com/danielpaulus/go-ios/blob/63c0d5705a01/ios/pair.go
https://github.com/danielpaulus/go-ios/blob/63c0d5705a01/ios/usbmuxconnection.go
https://github.com/libimobiledevice/usbmuxd
https://support.apple.com/en-us/109054
