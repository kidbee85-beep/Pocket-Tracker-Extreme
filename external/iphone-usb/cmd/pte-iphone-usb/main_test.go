package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"testing"
	"time"

	"pockettracker-extreme/iphone-usb/internal/audio"
)

type fakeUSB struct {
	frames  [][]byte
	index   int
	cancel  context.CancelFunc
	fail    bool
	stopped bool
}

func (f *fakeUSB) Read(ctx context.Context, b []byte) (int, error) {
	if f.index < len(f.frames) {
		n := copy(b, f.frames[f.index])
		f.index++
		return n, nil
	}
	if f.fail {
		return 0, errors.New("unplugged fixture")
	}
	if f.cancel != nil {
		f.cancel()
	}
	<-ctx.Done()
	return 0, ctx.Err()
}
func (f *fakeUSB) Write(_ context.Context, b []byte) error {
	if len(b) >= 20 && binary.LittleEndian.Uint32(b[16:]) == 0x68706130 {
		f.stopped = true
	}
	return nil
}
func (f *fakeUSB) Close() error { return nil }
func load(t *testing.T, name string) []byte {
	t.Helper()
	b, err := os.ReadFile("../../internal/protocol/testdata/" + name)
	if err != nil {
		t.Fatal(err)
	}
	if name == "asyn-eat" {
		f := make([]byte, len(b)+4)
		binary.LittleEndian.PutUint32(f, uint32(len(f)))
		copy(f[4:], b)
		return f
	}
	return b
}
func TestCaptureCancellationAndDisconnectSaveRealPartialAudio(t *testing.T) {
	for _, disconnect := range []bool{false, true} {
		ctx, cancel := context.WithCancel(context.Background())
		o := options{path: filepath.Join(t.TempDir(), "take.wav"), rate: 48000, channels: 2, seconds: 1}
		w, err := audio.New(o.path, o.rate, o.channels, o.seconds)
		if err != nil {
			t.Fatal(err)
		}
		transport := &fakeUSB{frames: [][]byte{load(t, "cwpa-request1"), load(t, "afmt-request"), load(t, "asyn-eat")}, cancel: cancel, fail: disconnect}
		var out bytes.Buffer
		start := time.Now()
		err = capture(ctx, transport, w, o, json.NewEncoder(&out))
		cancel()
		if disconnect != (err != nil) {
			t.Fatalf("disconnect=%v err=%v", disconnect, err)
		}
		if time.Since(start) > time.Second {
			t.Fatal("stop did not interrupt read")
		}
		if !transport.stopped {
			t.Fatal("audio stop command not sent")
		}
		if err := w.Close(); err != nil {
			t.Fatal(err)
		}
		var event status
		if err := json.Unmarshal(bytes.TrimSpace(out.Bytes()), &event); err != nil {
			t.Fatal(err)
		}
		if event.State != "recording" || event.Frames != 1024 || event.ProtocolVersion != 1 {
			t.Fatalf("wrong status %+v", event)
		}
		b, err := os.ReadFile(o.path)
		if err != nil {
			t.Fatal(err)
		}
		if len(b) != 44+1024*4 || binary.LittleEndian.Uint32(b[40:]) != 4096 {
			t.Fatal("invalid partial WAV")
		}
	}
}
func TestNoRecordingStatusWithoutPCM(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	o := options{path: filepath.Join(t.TempDir(), "take.wav"), rate: 48000, channels: 2, seconds: 1}
	w, _ := audio.New(o.path, o.rate, o.channels, o.seconds)
	transport := &fakeUSB{frames: [][]byte{load(t, "cwpa-request1"), load(t, "afmt-request")}, cancel: cancel}
	var out bytes.Buffer
	if err := capture(ctx, transport, w, o, json.NewEncoder(&out)); err != nil {
		t.Fatal(err)
	}
	w.Close()
	cancel()
	if out.Len() != 0 || w.Frames != 0 {
		t.Fatal("announced recording without decoded PCM")
	}
}
