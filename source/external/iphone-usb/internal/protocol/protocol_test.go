package protocol

import (
	"bytes"
	"math"
	"os"
	"testing"
	"time"
)

func fixture(t *testing.T, name string) []byte {
	t.Helper()
	b, err := os.ReadFile("testdata/" + name)
	if err != nil {
		t.Fatal(err)
	}
	return b
}
func transport(b []byte) []byte {
	r := make([]byte, len(b)+4)
	le.PutUint32(r, uint32(len(r)))
	copy(r[4:], b)
	return r
}
func TestRecordedAudioAndNegotiation(t *testing.T) {
	s := NewSession()
	var replies [][]byte
	send := func(b []byte) error { replies = append(replies, append([]byte(nil), b...)); return nil }
	if err := s.Handle(fixture(t, "cwpa-request1"), send, nil); err != nil {
		t.Fatal(err)
	}
	if len(replies) != 2 || le.Uint32(replies[1][16:]) != 0x68706131 {
		t.Fatal("must request audio alone")
	}
	if le.Uint64(replies[0][20:]) != s.DeviceClock+1000 {
		t.Fatal("wrong audio clock")
	}
	replies = nil
	if err := s.Handle(fixture(t, "afmt-request"), send, nil); err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(replies[0], fixture(t, "afmt-reply")) {
		t.Fatal("AFMT reply differs from recorded protocol")
	}
	count := 0
	consume := func(a Audio) error {
		if a.Format.Rate != 48000 || a.Format.Channels != 2 || a.Format.Bits != 16 || a.Format.Float || len(a.PCM) != 4096 {
			t.Fatalf("unexpected audio %+v bytes=%d", a.Format, len(a.PCM))
		}
		count += len(a.PCM) / 4
		return nil
	}
	for _, name := range []string{"asyn-eat", "asyn-eat-nofdsc"} {
		if err := s.Handle(transport(fixture(t, name)), send, consume); err != nil {
			t.Fatal(err)
		}
	}
	if count != 2048 {
		t.Fatalf("got %d frames", count)
	}
}
func TestRecordedFixedReplies(t *testing.T) {
	for _, name := range []string{"og", "stop"} {
		s := NewSession()
		if err := s.Handle(fixture(t, name+"-request"), func(b []byte) error {
			if !bytes.Equal(b, fixture(t, name+"-reply")) {
				t.Fatal("reply mismatch", name)
			}
			return nil
		}, nil); err != nil {
			t.Fatal(err)
		}
	}
}

func TestSkewUsesDeviceTimestampScale(t *testing.T) {
	s := NewSession()
	s.haveTiming = true
	s.timestampScale = 48000
	s.firstDevice = 10
	s.lastDevice = 11
	s.firstLocal = time.Now()
	s.lastLocal = s.firstLocal.Add(time.Second)
	if err := s.Handle(fixture(t, "skew-request"), func(b []byte) error {
		if got := math.Float64frombits(le.Uint64(b[20:])); got != 48000 {
			t.Fatalf("skew=%f expected48000", got)
		}
		return nil
	}, nil); err != nil {
		t.Fatal(err)
	}
}
func TestFramerSplitAndBounds(t *testing.T) {
	frame := fixture(t, "afmt-request")
	var f Framer
	calls := 0
	for _, b := range append(append([]byte(nil), frame...), frame...) {
		if err := f.Push([]byte{b}, func(got []byte) error {
			calls++
			if !bytes.Equal(got, frame) {
				t.Fatal("wrong frame")
			}
			return nil
		}); err != nil {
			t.Fatal(err)
		}
	}
	if calls != 2 {
		t.Fatal(calls)
	}
	for _, n := range []uint32{0, 1, 4, 7, MaxPacket + 1, ^uint32(0)} {
		var bad Framer
		b := make([]byte, 4)
		le.PutUint32(b, n)
		if bad.Push(b, func([]byte) error { return nil }) == nil {
			t.Fatalf("accepted %d", n)
		}
	}
}
func TestEveryTruncatedRecordedAudioIsRejected(t *testing.T) {
	b := fixture(t, "asyn-eat")[16:]
	for n := 0; n < len(b); n++ {
		if _, err := ParseAudio(b[:n], Format{}); err == nil {
			t.Fatalf("accepted truncation %d", n)
		}
	}
	for _, n := range []uint32{0, 1, 7, MaxPacket, ^uint32(0)} {
		c := append([]byte(nil), b...)
		le.PutUint32(c[8:], n)
		if _, err := ParseAudio(c, Format{}); err == nil {
			t.Fatalf("accepted bad child size %d", n)
		}
	}
}
func TestNoFormatGuessing(t *testing.T) {
	b := fixture(t, "asyn-eat-nofdsc")[16:]
	if _, err := ParseAudio(b, Format{}); err == nil {
		t.Fatal("must not guess audio format")
	}
	f := fixture(t, "afmt-request")[28:]
	for _, offset := range []int{8, 20, 24, 28, 32} {
		c := append([]byte(nil), f...)
		le.PutUint32(c[offset:], 0xffffffff)
		if _, err := ParseFormat(c); err == nil {
			t.Fatal("accepted corrupt format", offset)
		}
	}
}
func FuzzAudioParser(f *testing.F) {
	b, _ := os.ReadFile("testdata/asyn-eat")
	f.Add(b[16:])
	f.Add([]byte{0, 1, 2})
	f.Fuzz(func(t *testing.T, b []byte) {
		if len(b) > MaxPacket {
			return
		}
		_, _ = ParseAudio(b, Format{})
	})
}
