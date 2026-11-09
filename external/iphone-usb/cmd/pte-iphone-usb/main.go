// pte-iphone-usb is a deliberately small, audio-only companion process.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"pockettracker-extreme/iphone-usb/internal/audio"
	"pockettracker-extreme/iphone-usb/internal/protocol"
	"pockettracker-extreme/iphone-usb/internal/usb"
)

type status struct {
	ProtocolVersion int     `json:"protocol_version"`
	State           string  `json:"state"`
	Message         string  `json:"message"`
	Frames          uint64  `json:"frames"`
	Rate            int     `json:"rate"`
	Channels        int     `json:"channels"`
	PeakL           float64 `json:"peakL"`
	PeakR           float64 `json:"peakR"`
	Path            string  `json:"path"`
}
type options struct {
	id, path                string
	rate, channels, seconds int
}

func emit(enc *json.Encoder, w *audio.Writer, o options, state, message string) error {
	s := status{ProtocolVersion: 1, State: state, Message: message, Rate: o.rate, Channels: o.channels, Path: o.path}
	if w != nil {
		s.Frames = w.Frames
		s.PeakL = w.PeakL
		s.PeakR = w.PeakR
	}
	return enc.Encode(s)
}

var full = errors.New("maximum capture length reached")

func capture(ctx context.Context, t usb.Transport, w *audio.Writer, o options, enc *json.Encoder) error {
	s := protocol.NewSession()
	var f protocol.Framer
	started := time.Now()
	lastPCM := started
	lastStatus := time.Time{}
	send := func(b []byte) error {
		writeCtx, cancel := context.WithTimeout(ctx, time.Second)
		defer cancel()
		return t.Write(writeCtx, b)
	}
	consume := func(a protocol.Audio) error {
		if err := w.Consume(a); err != nil {
			return err
		}
		lastPCM = time.Now()
		if w.Frames > 0 && (lastStatus.IsZero() || time.Since(lastStatus) >= 200*time.Millisecond) {
			if err := w.Sync(); err != nil {
				return err
			}
			if err := emit(enc, w, o, "recording", "Receiving iPhone audio"); err != nil {
				return err
			}
			lastStatus = time.Now()
		}
		if w.Frames >= w.MaxFrames {
			return full
		}
		return nil
	}
	defer func() {
		stopCtx, cancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
		defer cancel()
		_ = t.Write(stopCtx, s.Stop())
	}()
	b := make([]byte, 65536)
	for ctx.Err() == nil {
		readCtx, cancel := context.WithTimeout(ctx, 500*time.Millisecond)
		n, err := t.Read(readCtx, b)
		cancel()
		if n > 0 {
			if processErr := f.Push(b[:n], func(frame []byte) error { return s.Handle(frame, send, consume) }); processErr != nil {
				if errors.Is(processErr, full) {
					return nil
				}
				return processErr
			}
		}
		if ctx.Err() != nil {
			return nil
		}
		if err != nil && !errors.Is(err, context.DeadlineExceeded) && !errors.Is(err, context.Canceled) {
			return fmt.Errorf("iPhone USB audio disconnected: %w", err)
		}
		if w.Frames == 0 && time.Since(started) > 20*time.Second {
			return errors.New("no audio arrived from the iPhone; unlock it, use a data cable, accept Trust if shown, and start audio playback. This experimental mode may be unsupported on this iOS/host combination")
		}
		if w.Frames > 0 && time.Since(lastPCM) > 10*time.Second {
			return errors.New("iPhone stopped sending audio; the valid partial take has been saved")
		}
		if time.Since(started) > time.Duration(o.seconds+30)*time.Second {
			return errors.New("capture time limit reached before the requested audio duration")
		}
	}
	return nil
}
func record(args []string, enc *json.Encoder) int {
	o := options{}
	fs := flag.NewFlagSet("record", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	fs.StringVar(&o.id, "udid", "", "USB serial from list")
	fs.StringVar(&o.path, "output", "", "new WAV path")
	fs.IntVar(&o.rate, "rate", 48000, "44100 or 48000")
	fs.IntVar(&o.channels, "channels", 2, "1 or 2")
	fs.IntVar(&o.seconds, "max-seconds", 600, "1 to 600")
	if err := fs.Parse(args); err != nil {
		_ = emit(enc, nil, o, "error", err.Error())
		return 2
	}
	if fs.NArg() != 0 || strings.TrimSpace(o.id) == "" || len(o.id) > 256 || o.path == "" || (o.rate != 44100 && o.rate != 48000) || (o.channels != 1 && o.channels != 2) || o.seconds < 1 || o.seconds > 600 {
		_ = emit(enc, nil, o, "error", "record requires --udid, --output, rate 44100/48000, channels 1/2 and max-seconds 1..600")
		return 2
	}
	var err error
	o.path, err = filepath.Abs(o.path)
	if err != nil {
		_ = emit(enc, nil, o, "error", err.Error())
		return 2
	}
	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()
	if err = emit(enc, nil, o, "starting", "Opening experimental iPhone USB audio"); err != nil {
		return 1
	}
	w, err := audio.New(o.path, o.rate, o.channels, o.seconds)
	if err != nil {
		_ = emit(enc, nil, o, "error", err.Error())
		return 1
	}
	t, err := usb.Open(ctx, o.id)
	if err == nil {
		err = capture(ctx, t, w, o, enc)
	}
	closeErr := w.Close()
	if err == nil {
		err = closeErr
	}
	// Finalize the take before potentially waiting for USB re-enumeration.
	// Ready still follows cleanup, but an enforced kill cannot lose the tail.
	if t != nil {
		closeErr = t.Close()
		if err == nil && closeErr != nil {
			err = fmt.Errorf("audio saved, but restoring USB configuration failed: %w", closeErr)
		}
	}
	if w.Frames == 0 {
		_ = os.Remove(o.path)
		if err == nil {
			err = errors.New("recording stopped before iPhone audio was received")
		}
	}
	if err != nil {
		_ = emit(enc, w, o, "error", err.Error())
		return 1
	}
	if err = emit(enc, w, o, "ready", "iPhone take saved"); err != nil {
		return 1
	}
	return 0
}
func run(args []string) int {
	enc := json.NewEncoder(os.Stdout)
	if len(args) == 1 && args[0] == "--version" {
		fmt.Println("pte-iphone-usb 1.0 protocol=1 experimental-audio-only")
		return 0
	}
	if len(args) == 1 && args[0] == "list" {
		devices, err := usb.List()
		result := struct {
			ProtocolVersion int          `json:"protocol_version"`
			Devices         []usb.Device `json:"devices"`
			Error           string       `json:"error,omitempty"`
			Message         string       `json:"message,omitempty"`
		}{ProtocolVersion: 1, Devices: devices}
		if result.Devices == nil {
			result.Devices = []usb.Device{}
		}
		if err != nil {
			result.Error = err.Error()
		}
		if len(devices) == 0 {
			result.Message = "No accessible iPhone found. Connect it using a data cable and unlock it."
		}
		if enc.Encode(result) != nil {
			return 1
		}
		return 0
	}
	if len(args) > 0 && args[0] == "record" {
		return record(args[1:], enc)
	}
	_ = emit(enc, nil, options{}, "error", "usage: pte-iphone-usb list | record --udid SERIAL --output FILE [--rate 48000 --channels 2 --max-seconds 600] | --version")
	return 2
}
func main() { os.Exit(run(os.Args[1:])) }
