// Package protocol implements the audio-only part of the reverse-engineered
// QuickTime USB protocol. See LICENSE-QVH and README.md for its provenance.
// All lengths are checked before slicing; peer dictionaries are never decoded.
package protocol

import (
	_ "embed"
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"time"
)

const MaxPacket = 4 * 1024 * 1024
const (
	ping       = 0x70696e67
	syncMagic  = 0x73796e63
	asyn       = 0x6173796e
	replyMagic = 0x72706c79
	cwpa       = 0x63777061
	cvrp       = 0x63767270
	clok       = 0x636c6f6b
	timeType   = 0x74696d65
	afmt       = 0x61666d74
	skew       = 0x736b6577
	stop       = 0x73746f70
	og         = 0x676f2120
	eat        = 0x65617421
	feed       = 0x66656564
	rels       = 0x72656c73
	hpa0       = 0x68706130
	sprp       = 0x73707270
	srat       = 0x73726174
	tbas       = 0x74626173
	tjmp       = 0x746a6d70
	sbuf       = 0x73627566
	fdsc       = 0x66647363
	mdia       = 0x6d646961
	soun       = 0x736f756e
	asbd       = 0x61736264
	sdat       = 0x73646174
	nsmp       = 0x6e736d70
	opts       = 0x6f707473
)

// Embedded outbound templates come from the MIT-licensed upstream recordings.
// Their untrusted-input parsers are deliberately not included in this helper.
//
//go:embed testdata/asyn-hpa1
var hpaTemplate []byte

//go:embed testdata/afmt-reply
var afmtTemplate []byte

var le = binary.LittleEndian

// Framer accepts arbitrarily split USB transfers, never an unbounded allocation.
type Framer struct{ pending []byte }

func (f *Framer) Push(data []byte, consume func([]byte) error) error {
	if len(data) > 65536 || len(f.pending)+len(data) > MaxPacket+65536 {
		return errors.New("USB transfer exceeds framing limit")
	}
	f.pending = append(f.pending, data...)
	for len(f.pending) >= 4 {
		n := int(le.Uint32(f.pending))
		if n < 8 || n > MaxPacket {
			return fmt.Errorf("invalid USB packet length %d", n)
		}
		if len(f.pending) < n {
			return nil
		}
		if err := consume(f.pending[:n]); err != nil {
			return err
		}
		f.pending = f.pending[n:]
	}
	return nil
}

// Format describes supported interleaved LPCM. Other codecs/layouts fail closed.
type Format struct {
	Rate          int
	Channels      int
	Bits          int
	Float         bool
	BigEndian     bool
	BytesPerFrame int
}

func ParseFormat(b []byte) (Format, error) {
	var f Format
	if len(b) < 40 {
		return f, errors.New("truncated audio format")
	}
	rate := math.Float64frombits(le.Uint64(b))
	flags := le.Uint32(b[12:])
	if math.IsNaN(rate) || math.IsInf(rate, 0) || rate < 8000 || rate > 192000 || rate != math.Trunc(rate) {
		return f, errors.New("unsupported source sample rate")
	}
	f = Format{Rate: int(rate), Channels: int(le.Uint32(b[28:])), Bits: int(le.Uint32(b[32:])), Float: flags&1 != 0, BigEndian: flags&2 != 0, BytesPerFrame: int(le.Uint32(b[24:]))}
	if le.Uint32(b[8:]) != 0x6c70636d || f.Channels < 1 || f.Channels > 2 || flags&0x20 != 0 || flags&0x08 == 0 {
		return Format{}, errors.New("source is not supported packed interleaved mono/stereo LPCM")
	}
	if (f.Float && f.Bits != 32) || (!f.Float && (f.Bits != 16 || flags&4 == 0)) || f.BytesPerFrame != f.Channels*f.Bits/8 || le.Uint32(b[20:]) != 1 || int(le.Uint32(b[16:])) != f.BytesPerFrame {
		return Format{}, errors.New("unsupported PCM representation")
	}
	return f, nil
}

type Audio struct {
	Format         Format
	PCM            []byte
	Timestamp      float64
	TimestampScale uint32
	HasTimestamp   bool
}

func box(b []byte) (uint32, []byte, []byte, error) {
	if len(b) < 8 {
		return 0, nil, nil, errors.New("truncated CoreMedia box header")
	}
	n := int(le.Uint32(b))
	if n < 8 || n > len(b) {
		return 0, nil, nil, errors.New("invalid CoreMedia box length")
	}
	return le.Uint32(b[4:]), b[8:n], b[n:], nil
}
func parseDescriptor(b []byte) (Format, error) {
	var f Format
	mediaOK := false
	haveFormat := false
	for len(b) > 0 {
		t, p, rest, err := box(b)
		if err != nil {
			return f, err
		}
		b = rest
		switch t {
		case mdia:
			if len(p) != 4 || le.Uint32(p) != soun {
				return f, errors.New("non-audio format descriptor")
			}
			mediaOK = true
		case asbd:
			f, err = ParseFormat(p)
			if err != nil {
				return f, err
			}
			haveFormat = true
		}
	}
	if !mediaOK || !haveFormat {
		return f, errors.New("audio format descriptor is incomplete")
	}
	return f, nil
}
func ParseAudio(b []byte, previous Format) (Audio, error) {
	a := Audio{Format: previous}
	typ, p, rest, err := box(b)
	if err != nil {
		return a, err
	}
	if typ != sbuf || len(rest) != 0 {
		return a, errors.New("invalid audio sample buffer")
	}
	samples := -1
	haveData := false
	for len(p) > 0 {
		t, body, next, err := box(p)
		if err != nil {
			return a, err
		}
		p = next
		switch t {
		case fdsc:
			a.Format, err = parseDescriptor(body)
			if err != nil {
				return a, err
			}
		case sdat:
			if haveData {
				return a, errors.New("duplicate sample data")
			}
			a.PCM = body
			haveData = true
		case nsmp:
			if len(body) != 4 {
				return a, errors.New("invalid sample count")
			}
			samples = int(le.Uint32(body))
		case opts:
			if len(body) != 24 {
				return a, errors.New("invalid audio timestamp")
			}
			scale := le.Uint32(body[8:])
			if scale > 0 {
				a.Timestamp = float64(le.Uint64(body)) / float64(scale)
				a.TimestampScale = scale
				a.HasTimestamp = true
			}
			// Other timing/attachment fields are opaque, bounded by their outer box.
		}
	}
	if len(a.PCM) > 0 {
		if a.Format.BytesPerFrame == 0 {
			return a, errors.New("audio received before a supported format")
		}
		if len(a.PCM)%a.Format.BytesPerFrame != 0 {
			return a, errors.New("partial PCM frame")
		}
		if samples >= 0 && samples != len(a.PCM)/a.Format.BytesPerFrame {
			return a, errors.New("PCM sample count mismatch")
		}
	}
	return a, nil
}

// Session never asks for video (HPD1/NEED). Audio parameters are established by
// AFMT or the first audio format descriptor, never assumed from raw byte count.
type Session struct {
	Format                  Format
	DeviceClock             uint64
	start                   time.Time
	clockStart              time.Time
	firstLocal, lastLocal   time.Time
	firstDevice, lastDevice float64
	haveTiming              bool
	timestampScale          uint32
}

func NewSession() *Session { now := time.Now(); return &Session{start: now, clockStart: now} }
func reply(correlation uint64, size int) []byte {
	b := make([]byte, size)
	le.PutUint32(b, uint32(size))
	le.PutUint32(b[4:], replyMagic)
	le.PutUint64(b[8:], correlation)
	return b
}
func clockReply(correlation, clock uint64) []byte {
	b := reply(correlation, 28)
	le.PutUint64(b[20:], clock)
	return b
}
func (s *Session) Stop() []byte {
	b := make([]byte, 20)
	le.PutUint32(b, 20)
	le.PutUint32(b[4:], asyn)
	le.PutUint64(b[8:], s.DeviceClock)
	le.PutUint32(b[16:], hpa0)
	return b
}
func (s *Session) Handle(frame []byte, send func([]byte) error, consume func(Audio) error) error {
	if len(frame) < 8 || len(frame) > MaxPacket || int(le.Uint32(frame)) != len(frame) {
		return errors.New("invalid protocol frame")
	}
	b := frame[4:]
	magic := le.Uint32(b)
	if magic == ping {
		if len(b) != 12 {
			return errors.New("invalid ping length")
		}
		r := make([]byte, 16)
		le.PutUint32(r, 16)
		le.PutUint32(r[4:], ping)
		le.PutUint64(r[8:], 0x100000000)
		return send(r)
	}
	if len(b) < 16 {
		return errors.New("truncated protocol header")
	}
	typ := le.Uint32(b[12:])
	switch magic {
	case syncMagic:
		if len(b) < 24 {
			return errors.New("truncated sync header")
		}
		correlation := le.Uint64(b[16:])
		payload := b[24:]
		switch typ {
		case cwpa:
			if len(payload) != 8 || le.Uint64(b[4:]) != 1 {
				return errors.New("invalid CWPA")
			}
			s.DeviceClock = le.Uint64(payload)
			if err := send(clockReply(correlation, s.DeviceClock+1000)); err != nil {
				return err
			}
			r := append([]byte(nil), hpaTemplate...)
			le.PutUint64(r[8:], s.DeviceClock)
			return send(r)
		case cvrp:
			if len(payload) < 8 {
				return errors.New("truncated CVRP")
			}
			return send(clockReply(correlation, le.Uint64(payload)+0x1000af))
		case clok:
			s.clockStart = time.Now()
			return send(clockReply(correlation, le.Uint64(b[4:])+0x10000))
		case timeType:
			r := reply(correlation, 44)
			le.PutUint64(r[20:], uint64(time.Since(s.clockStart).Nanoseconds()))
			le.PutUint32(r[28:], 1000000000)
			le.PutUint32(r[32:], 1)
			return send(r)
		case afmt:
			var err error
			s.Format, err = ParseFormat(payload)
			if err != nil {
				return err
			}
			r := append([]byte(nil), afmtTemplate...)
			le.PutUint64(r[8:], correlation)
			return send(r)
		case skew:
			// The protocol expresses skew in ticks of the device audio PTS
			// clock, normally 48000 Hz, not our nanosecond host clock.
			scale := float64(s.timestampScale)
			if scale == 0 {
				scale = float64(s.Format.Rate)
			}
			if scale == 0 {
				scale = 48000
			}
			value := scale
			if s.haveTiming && s.lastDevice > s.firstDevice {
				value = scale * s.lastLocal.Sub(s.firstLocal).Seconds() / (s.lastDevice - s.firstDevice)
			}
			if math.IsNaN(value) || math.IsInf(value, 0) || value < 0.5*scale || value > 2*scale {
				value = scale
			}
			r := reply(correlation, 28)
			le.PutUint64(r[20:], math.Float64bits(value))
			return send(r)
		case stop, og:
			return send(reply(correlation, 24))
		default:
			return fmt.Errorf("unsupported sync message 0x%x", typ)
		}
	case asyn:
		switch typ {
		case eat:
			a, err := ParseAudio(b[16:], s.Format)
			if err != nil {
				return err
			}
			s.Format = a.Format
			if a.HasTimestamp {
				now := time.Now()
				if !s.haveTiming || s.timestampScale != a.TimestampScale {
					s.firstLocal = now
					s.firstDevice = a.Timestamp
					s.haveTiming = true
					s.timestampScale = a.TimestampScale
				}
				s.lastLocal = now
				s.lastDevice = a.Timestamp
			}
			if len(a.PCM) > 0 {
				return consume(a)
			}
			return nil
		case rels:
			return errors.New("iPhone ended the audio session")
		case feed, sprp, srat, tbas, tjmp:
			return nil
		default:
			return fmt.Errorf("unsupported async message 0x%x", typ)
		}
	default:
		return fmt.Errorf("unsupported protocol magic 0x%x", magic)
	}
}
