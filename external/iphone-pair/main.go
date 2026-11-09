// pte-iphone-pair performs only normal, user-approved USB host pairing.
// It connects to an app-owned usbmuxd socket; it never launches or stops daemons.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"io"
	"log/slog"
	"os"
	"os/signal"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/danielpaulus/go-ios/ios"
)

type status struct {
	ProtocolVersion int    `json:"protocol_version"`
	State           string `json:"state"`
	Message         string `json:"message"`
	Error           string `json:"error,omitempty"`
}

type pairBackend interface {
	find(string) (ios.DeviceEntry, error)
	validate(ios.DeviceEntry) error
	pair(ios.DeviceEntry) error
}

type iosBackend struct{}

func (iosBackend) find(udid string) (ios.DeviceEntry, error) {
	devices, err := ios.ListDevices()
	if err != nil {
		return ios.DeviceEntry{}, err
	}
	for _, device := range devices.DeviceList {
		// A paired Wi-Fi device must never replace the selected physical phone.
		if sameSerial(device.Properties.SerialNumber, udid) && device.Properties.ConnectionType == "USB" {
			return device, nil
		}
	}
	return ios.DeviceEntry{}, errors.New("selected USB device not found")
}

func (iosBackend) validate(device ios.DeviceEntry) error {
	mux, err := ios.NewUsbMuxConnectionSimple()
	if err != nil {
		return err
	}
	defer mux.Close()
	record, err := mux.ReadPair(device.Properties.SerialNumber)
	if err != nil {
		return err
	}
	if record.HostID == "" || record.SystemBUID == "" || len(record.HostCertificate) == 0 || len(record.HostPrivateKey) == 0 {
		return errors.New("pair record is incomplete")
	}
	connection, err := mux.ConnectLockdown(device.DeviceID)
	if err != nil {
		return err
	}
	response, err := connection.StartSession(record)
	if err != nil {
		return err
	}
	if response.SessionID == "" {
		return errors.New("lockdown session was not established")
	}
	connection.Close()
	return nil
}

func (iosBackend) pair(device ios.DeviceEntry) error { return ios.Pair(device) }

func sameSerial(a, b string) bool {
	// USB serial descriptors may omit the hyphen in a 24-character iPhone UDID.
	return strings.EqualFold(strings.ReplaceAll(a, "-", ""), strings.ReplaceAll(b, "-", ""))
}

func validSerial(serial string) bool {
	return regexp.MustCompile(`^[A-Fa-f0-9]{8}-?[A-Fa-f0-9]{16}$|^[A-Fa-f0-9]{40}$`).MatchString(serial)
}

func validateSocket(path string) error {
	if path == "" || !filepath.IsAbs(path) || filepath.Clean(path) != path || len(path) > 100 || strings.ContainsAny(path, "\x00\r\n:") {
		return errors.New("a private absolute UNIX socket path is required")
	}
	if path == "/var/run/usbmuxd" || path == "/run/usbmuxd" {
		return errors.New("the system usbmuxd socket is not accepted")
	}
	info, err := os.Lstat(filepath.Dir(path))
	if err != nil || !info.IsDir() || info.Mode()&os.ModeSymlink != 0 || info.Mode().Perm()&0077 != 0 {
		return errors.New("the socket parent must be a private directory (mode 0700)")
	}
	if stat, ok := info.Sys().(*syscall.Stat_t); !ok || stat.Uid != uint32(os.Geteuid()) {
		return errors.New("the socket directory must belong to this user")
	}
	info, err = os.Lstat(path)
	if err != nil || info.Mode()&os.ModeSocket == 0 || info.Mode()&os.ModeSymlink != 0 {
		return errors.New("the app-owned usbmuxd socket is unavailable")
	}
	return nil
}

// Only stable categories are published. Library errors can contain device values,
// pair records, certificates or other host secrets and must never be logged.
func category(err error) string {
	if err == nil {
		return ""
	}
	text := strings.ToLower(err.Error())
	switch {
	case strings.Contains(text, "userdeniedpairing") || strings.Contains(text, "user denied"):
		return "denied"
	case strings.Contains(text, "pairingdialog") || strings.Contains(text, "dialogresponsepending"):
		return "trust"
	case strings.Contains(text, "passwordprotected") || strings.Contains(text, "passcoderequired") || strings.Contains(text, "password protected") || strings.Contains(text, "device is locked"):
		return "unlock"
	case strings.Contains(text, "pairingprohibited") || strings.Contains(text, "getprohibited") || strings.Contains(text, "mcprotected"):
		return "prohibited"
	case strings.Contains(text, "not found") || strings.Contains(text, "no such file") || strings.Contains(text, "connection refused") || strings.Contains(text, "no ios devices"):
		return "unavailable"
	default:
		return "pairing"
	}
}

func pairLoop(ctx context.Context, udid string, backend pairBackend, emit func(string, string, string), retry time.Duration) error {
	emit("connecting", "Connecting to the selected iPhone over USB.", "")
	for {
		if ctx.Err() != nil {
			return ctx.Err()
		}
		device, err := backend.find(udid)
		if err != nil {
			emit("connecting", "Waiting for the selected iPhone. Keep it unlocked and connected.", "unavailable")
		} else {
			err = backend.validate(device)
			if err == nil {
				emit("paired", "Normal iPhone host pairing is ready.", "")
				return nil
			}
			if category(err) != "unlock" {
				emit("pairing", "Requesting normal iPhone host pairing. Watch the iPhone screen.", "")
				err = backend.pair(device)
				if err == nil {
					// Saving a record is insufficient: prove it opens a lockdown session.
					err = backend.validate(device)
					if err == nil {
						emit("paired", "Normal iPhone host pairing is ready.", "")
						return nil
					}
				}
			}
			switch category(err) {
			case "denied":
				emit("error", "Trust was declined on the iPhone. Reconnect and start a new recording to try again.", "trust_denied")
				return errors.New("trust_denied")
			case "prohibited":
				emit("error", "This iPhone does not allow pairing over this connection.", "pairing_prohibited")
				return errors.New("pairing_prohibited")
			case "unlock":
				emit("unlock", "Unlock the iPhone with its passcode and keep the screen awake.", "")
			case "trust":
				emit("trust", "Tap Trust on the iPhone and confirm its passcode when asked.", "")
			default:
				emit("pairing", "Waiting for iPhone pairing. Unlock it and accept Trust if shown.", "")
			}
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(retry):
		}
	}
}

func run(args []string, output io.Writer) int {
	encoder := json.NewEncoder(output)
	var outputMu sync.Mutex
	last := status{}
	closed := false
	defer func() {
		outputMu.Lock()
		closed = true
		outputMu.Unlock()
	}()
	emit := func(state, message, code string) {
		outputMu.Lock()
		defer outputMu.Unlock()
		if closed {
			return
		}
		value := status{1, state, message, code}
		if value != last {
			_ = encoder.Encode(value)
			last = value
		}
	}
	lastState := func() string {
		outputMu.Lock()
		defer outputMu.Unlock()
		return last.State
	}
	if len(args) == 1 && args[0] == "--version" {
		_ = encoder.Encode(map[string]any{"protocol_version": 1, "name": "pte-iphone-pair", "version": "0.1.0.8", "go_ios_commit": "63c0d5705a01"})
		return 0
	}
	flags := flag.NewFlagSet("pair", flag.ContinueOnError)
	flags.SetOutput(io.Discard)
	udid := flags.String("udid", "", "selected USB device serial")
	socket := flags.String("socket", "", "app-owned UNIX usbmuxd socket")
	timeout := flags.Int("timeout", 90, "maximum pairing wait in seconds (1–90)")
	if len(args) == 0 || args[0] != "pair" || flags.Parse(args[1:]) != nil || flags.NArg() != 0 || !validSerial(*udid) || *timeout < 1 || *timeout > 90 {
		emit("error", "Usage: pte-iphone-pair pair --udid SERIAL --socket PRIVATE_PATH [--timeout 90]", "invalid_arguments")
		return 2
	}
	if err := validateSocket(*socket); err != nil {
		emit("error", "A private app-owned usbmuxd socket is required. System services are left unchanged.", "private_socket_unavailable")
		return 2
	}
	_ = os.Setenv("USBMUXD_SOCKET_ADDRESS", "unix://"+*socket)
	// Silence even warning logs: upstream parsing errors can embed pair data.
	ios.SetLogger(slog.New(slog.NewTextHandler(io.Discard, nil)))
	signals, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	ctx, cancel := context.WithTimeout(signals, time.Duration(*timeout)*time.Second)
	defer cancel()
	finished := make(chan error, 1)
	go func() {
		defer func() {
			if recover() != nil {
				finished <- errors.New("protocol_error")
			}
		}()
		finished <- pairLoop(ctx, *udid, iosBackend{}, emit, 2*time.Second)
	}()
	select {
	case err := <-finished:
		if err == nil {
			return 0
		}
		if lastState() != "error" {
			emit("error", "The iPhone pairing exchange failed. Keep the iPhone unlocked and retry.", "pairing_failed")
		}
		return 1
	case <-ctx.Done():
		if errors.Is(ctx.Err(), context.DeadlineExceeded) {
			emit("error", "iPhone pairing timed out. Unlock it, accept Trust if shown, and start again.", "pairing_timeout")
		} else {
			emit("cancelled", "iPhone pairing stopped.", "cancelled")
		}
		// A blocked upstream USBMux read is intentionally bounded by process exit.
		return 1
	}
}

func main() {
	os.Exit(run(os.Args[1:], os.Stdout))
}
