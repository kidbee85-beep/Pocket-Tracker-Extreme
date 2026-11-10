package audio

import (
	"bytes"
	"encoding/binary"
	"math"
	"os"
	"path/filepath"
	"testing"

	"pockettracker-extreme/iphone-usb/internal/protocol"
)

var pcm16 = protocol.Format{Rate: 48000, Channels: 2, Bits: 16, BytesPerFrame: 4}

func TestPCM16AtNativeRateIsBitExact(t *testing.T) {
	input := make([]byte, 65536*4)
	for i := 0; i < 65536; i++ {
		binary.LittleEndian.PutUint16(input[i*4:], uint16(i))
		binary.LittleEndian.PutUint16(input[i*4+2:], uint16(i))
	}
	p := filepath.Join(t.TempDir(), "identity.wav")
	w, err := New(p, 48000, 2, 2)
	if err != nil {
		t.Fatal(err)
	}
	if err = w.Consume(protocol.Audio{Format: pcm16, PCM: input}); err != nil {
		t.Fatal(err)
	}
	if err = w.Close(); err != nil {
		t.Fatal(err)
	}
	b := verify(t, p, 65536, 48000, 2)
	if !bytes.Equal(input, b[44:]) {
		t.Fatal("native PCM16 was altered")
	}
}

func sine(frames int) []byte {
	b := make([]byte, frames*4)
	for i := 0; i < frames; i++ {
		v := int16(math.Round(math.Sin(float64(i)*2*math.Pi*440/48000) * 16000))
		binary.LittleEndian.PutUint16(b[i*4:], uint16(v))
		binary.LittleEndian.PutUint16(b[i*4+2:], uint16(-v))
	}
	return b
}
func verify(t *testing.T, path string, frames uint64, rate, channels int) []byte {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(b) != 44+int(frames)*channels*2 || string(b[:4]) != "RIFF" || string(b[8:12]) != "WAVE" || binary.LittleEndian.Uint32(b[4:]) != uint32(len(b)-8) || binary.LittleEndian.Uint32(b[40:]) != uint32(len(b)-44) || binary.LittleEndian.Uint32(b[24:]) != uint32(rate) || binary.LittleEndian.Uint16(b[22:]) != uint16(channels) {
		t.Fatal("invalid finalized WAV", len(b), frames)
	}
	return b
}
func TestResampleDurationAndChunkIndependence(t *testing.T) {
	input := sine(48000)
	for _, rate := range []int{44100, 48000} {
		var reference []byte
		for _, chunk := range []int{48000, 1, 137} {
			path := filepath.Join(t.TempDir(), "take.wav")
			w, err := New(path, rate, 2, 2)
			if err != nil {
				t.Fatal(err)
			}
			for offset := 0; offset < len(input); offset += chunk * 4 {
				end := offset + chunk*4
				if end > len(input) {
					end = len(input)
				}
				if err := w.Consume(protocol.Audio{Format: pcm16, PCM: input[offset:end]}); err != nil {
					t.Fatal(err)
				}
			}
			if err := w.Sync(); err != nil {
				t.Fatal(err)
			}
			verify(t, path, w.Frames, rate, 2)
			if err := w.Close(); err != nil {
				t.Fatal(err)
			}
			if err := w.Close(); err != nil {
				t.Fatal(err)
			}
			if w.Frames != uint64(rate) {
				t.Fatalf("rate %d frames %d", rate, w.Frames)
			}
			b := verify(t, path, w.Frames, rate, 2)
			if reference == nil {
				reference = b
			} else if !bytes.Equal(reference, b) {
				t.Fatalf("chunk-dependent resampler at %d", chunk)
			}
		}
	}
}
func TestFloatClampMonoAndBounds(t *testing.T) {
	path := filepath.Join(t.TempDir(), "float.wav")
	w, err := New(path, 48000, 2, 1)
	if err != nil {
		t.Fatal(err)
	}
	input := make([]byte, 5*4)
	for i, v := range []float32{-2, 2, float32(math.NaN()), float32(math.Inf(1)), 0.5} {
		binary.LittleEndian.PutUint32(input[i*4:], math.Float32bits(v))
	}
	f := protocol.Format{Rate: 48000, Channels: 1, Bits: 32, Float: true, BytesPerFrame: 4}
	if err := w.Consume(protocol.Audio{Format: f, PCM: input}); err != nil {
		t.Fatal(err)
	}
	if err := w.Close(); err != nil {
		t.Fatal(err)
	}
	b := verify(t, path, 5, 48000, 2)
	expected := []int16{-32768, 32767, 0, 0, 16384}
	for i, v := range expected {
		for c := 0; c < 2; c++ {
			if got := int16(binary.LittleEndian.Uint16(b[44+i*4+c*2:])); got != v {
				t.Fatalf("frame %d channel %d =%d expected%d", i, c, got, v)
			}
		}
	}
	path = filepath.Join(t.TempDir(), "bounded.wav")
	w, _ = New(path, 48000, 1, 1)
	if err := w.Consume(protocol.Audio{Format: pcm16, PCM: sine(96000)}); err != nil {
		t.Fatal(err)
	}
	if w.Frames != 48000 {
		t.Fatal(w.Frames)
	}
	if err := w.Close(); err != nil {
		t.Fatal(err)
	}
	b = verify(t, path, 48000, 48000, 1)
	for _, v := range b[44:] {
		if v != 0 {
			t.Fatal("opposite stereo should mix to silence")
		}
	}
}

func TestUpsampleDurationAndBigEndian(t *testing.T) {
	f := pcm16
	f.Rate = 44100
	f.BigEndian = true
	input := make([]byte, 44100*4)
	for i := 0; i < 44100; i++ {
		binary.BigEndian.PutUint16(input[i*4:], 12345)
		binary.BigEndian.PutUint16(input[i*4+2:], uint16(32768))
	}
	p := filepath.Join(t.TempDir(), "upsampled.wav")
	w, err := New(p, 48000, 2, 2)
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < len(input); i += 113 * 4 {
		end := i + 113*4
		if end > len(input) {
			end = len(input)
		}
		if err := w.Consume(protocol.Audio{Format: f, PCM: input[i:end]}); err != nil {
			t.Fatal(err)
		}
	}
	if err = w.Close(); err != nil {
		t.Fatal(err)
	}
	b := verify(t, p, 48000, 48000, 2)
	for i := 44; i < len(b); i += 4 {
		if int16(binary.LittleEndian.Uint16(b[i:])) != 12345 || int16(binary.LittleEndian.Uint16(b[i+2:])) != -32768 {
			t.Fatal("big-endian conversion or held resampler tail changed sample")
		}
	}
}
func TestOutputSafetyAndFormatChange(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "reserved.wav")
	if err := os.WriteFile(p, nil, 0600); err != nil {
		t.Fatal(err)
	}
	w, err := New(p, 48000, 2, 1)
	if err != nil {
		t.Fatal(err)
	}
	if err := w.Consume(protocol.Audio{Format: pcm16, PCM: sine(50)}); err != nil {
		t.Fatal(err)
	}
	other := pcm16
	other.Rate = 44100
	if w.Consume(protocol.Audio{Format: other, PCM: sine(5)}) == nil {
		t.Fatal("accepted mid-take format change")
	}
	if err := w.Close(); err != nil {
		t.Fatal(err)
	}
	before := verify(t, p, 50, 48000, 2)
	if _, err := New(p, 48000, 2, 1); err == nil {
		t.Fatal("overwrote take")
	}
	after, _ := os.ReadFile(p)
	if !bytes.Equal(before, after) {
		t.Fatal("modified existing take")
	}
	empty := filepath.Join(dir, "empty")
	os.WriteFile(empty, nil, 0600)
	link := filepath.Join(dir, "link")
	os.Symlink(empty, link)
	if _, err := New(link, 48000, 2, 1); err == nil {
		t.Fatal("followed symlink")
	}
}
