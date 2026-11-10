// Package audio converts validated LPCM into a bounded recoverable PCM16 WAV.
package audio

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"os"
	"syscall"

	"pockettracker-extreme/iphone-usb/internal/protocol"
)

type Writer struct {
	file               *os.File
	Rate, Channels     int
	Frames, MaxFrames  uint64
	PeakL, PeakR       float64
	format             protocol.Format
	haveFormat         bool
	sourceFrames, next uint64
	previous           [2]float64
	buf                []byte
	closed             bool
	failed             bool
}

func New(path string, rate, channels, seconds int) (*Writer, error) {
	if (rate != 44100 && rate != 48000) || (channels != 1 && channels != 2) || seconds < 1 || seconds > 600 {
		return nil, errors.New("invalid output audio settings")
	}
	f, err := os.OpenFile(path, os.O_CREATE|os.O_EXCL|os.O_RDWR, 0600)
	if errors.Is(err, os.ErrExist) {
		// The UI reserves its unique destination before spawning us. Refuse
		// symlinks, directories and nonempty files; never truncate an old take.
		f, err = os.OpenFile(path, os.O_RDWR|syscall.O_NOFOLLOW, 0600)
		if err == nil {
			info, statErr := f.Stat()
			if statErr != nil || !info.Mode().IsRegular() || info.Size() != 0 {
				f.Close()
				return nil, errors.New("output already exists and is not an empty regular file")
			}
		}
	}
	if err != nil {
		return nil, err
	}
	w := &Writer{file: f, Rate: rate, Channels: channels, MaxFrames: uint64(rate * seconds)}
	if err = w.header(); err != nil {
		f.Close()
		os.Remove(path)
		return nil, err
	}
	return w, nil
}
func (w *Writer) header() error {
	b := make([]byte, 44)
	copy(b, "RIFF")
	binary.LittleEndian.PutUint32(b[4:], 36+uint32(w.Frames)*uint32(w.Channels)*2)
	copy(b[8:], "WAVEfmt ")
	binary.LittleEndian.PutUint32(b[16:], 16)
	binary.LittleEndian.PutUint16(b[20:], 1)
	binary.LittleEndian.PutUint16(b[22:], uint16(w.Channels))
	binary.LittleEndian.PutUint32(b[24:], uint32(w.Rate))
	binary.LittleEndian.PutUint32(b[28:], uint32(w.Rate*w.Channels*2))
	binary.LittleEndian.PutUint16(b[32:], uint16(w.Channels*2))
	binary.LittleEndian.PutUint16(b[34:], 16)
	copy(b[36:], "data")
	binary.LittleEndian.PutUint32(b[40:], uint32(w.Frames)*uint32(w.Channels)*2)
	n, err := w.file.WriteAt(b, 0)
	if err == nil && n != 44 {
		err = errors.New("short WAV header write")
	}
	return err
}
func clamp(v float64) float64 {
	if math.IsNaN(v) || math.IsInf(v, 0) {
		return 0
	}
	if v > 1 {
		return 1
	}
	if v < -1 {
		return -1
	}
	return v
}
func decode(b []byte, f protocol.Format) float64 {
	var order binary.ByteOrder = binary.LittleEndian
	if f.BigEndian {
		order = binary.BigEndian
	}
	if f.Float {
		return clamp(float64(math.Float32frombits(order.Uint32(b))))
	}
	return float64(int16(order.Uint16(b))) / 32768
}
func (w *Writer) output(v [2]float64) {
	if w.Frames >= w.MaxFrames {
		return
	}
	if w.Channels == 1 {
		v[0] = (v[0] + v[1]) * 0.5
		v[1] = v[0]
	}
	for c := 0; c < w.Channels; c++ {
		x := clamp(v[c])
		quantized := math.Round(x * 32768)
		if quantized > 32767 {
			quantized = 32767
		}
		sample := int16(quantized)
		w.buf = append(w.buf, byte(sample), byte(uint16(sample)>>8))
	}
	w.PeakL = math.Max(w.PeakL, math.Abs(v[0]))
	w.PeakR = math.Max(w.PeakR, math.Abs(v[1]))
	w.Frames++
}
func (w *Writer) write() error {
	if len(w.buf) == 0 {
		return nil
	}
	offset := int64(44) + int64(w.Frames)*int64(w.Channels)*2 - int64(len(w.buf))
	want := len(w.buf)
	n, err := w.file.WriteAt(w.buf, offset)
	w.buf = w.buf[:0]
	if err != nil || n != want {
		w.failed = true
		w.Frames = uint64((offset - 44 + int64(n)) / int64(w.Channels*2))
		_ = w.file.Truncate(44 + int64(w.Frames)*int64(w.Channels*2))
		if err != nil {
			return err
		}
		return errors.New("short PCM write")
	}
	return w.header()
}
func (w *Writer) Consume(a protocol.Audio) error {
	if w.closed {
		return errors.New("audio writer is closed")
	}
	f := a.Format
	if f.Rate < 8000 || f.Rate > 192000 || f.Channels < 1 || f.Channels > 2 || (f.Float && f.Bits != 32) || (!f.Float && f.Bits != 16) || f.BytesPerFrame != f.Channels*f.Bits/8 || len(a.PCM)%f.BytesPerFrame != 0 {
		return errors.New("invalid PCM input")
	}
	if w.haveFormat && a.Format != w.format {
		return errors.New("iPhone changed audio format during the take; start a new recording")
	}
	if !w.haveFormat {
		w.format = a.Format
		w.haveFormat = true
	}
	w.PeakL = 0
	w.PeakR = 0
	for i := 0; i < len(a.PCM) && w.Frames < w.MaxFrames; i += a.Format.BytesPerFrame {
		v := [2]float64{decode(a.PCM[i:], a.Format), 0}
		v[1] = v[0]
		if a.Format.Channels == 2 {
			v[1] = decode(a.PCM[i+a.Format.Bits/8:], a.Format)
		}
		index := w.sourceFrames
		for w.next <= index*uint64(w.Rate) && w.Frames < w.MaxFrames {
			out := v
			if index > 0 {
				fraction := float64(w.next-(index-1)*uint64(w.Rate)) / float64(w.Rate)
				for c := range out {
					out[c] = w.previous[c] + (v[c]-w.previous[c])*fraction
				}
			}
			w.output(out)
			w.next += uint64(a.Format.Rate)
		}
		w.previous = v
		w.sourceFrames++
		if len(w.buf) >= 32768 {
			if err := w.write(); err != nil {
				return err
			}
		}
	}
	return w.write()
}

// Sync commits both data and the current RIFF lengths before a status snapshot.
func (w *Writer) Sync() error {
	if err := w.write(); err != nil {
		return err
	}
	if err := w.header(); err != nil {
		return err
	}
	return w.file.Sync()
}
func (w *Writer) Close() error {
	if w.closed {
		return nil
	}
	w.closed = true
	if w.haveFormat && !w.failed {
		for w.next < w.sourceFrames*uint64(w.Rate) && w.Frames < w.MaxFrames {
			w.output(w.previous)
			w.next += uint64(w.format.Rate)
		}
	}
	err := w.Sync()
	closeErr := w.file.Close()
	if err != nil {
		return fmt.Errorf("finalizing WAV: %w", err)
	}
	return closeErr
}
