// SPDX-License-Identifier: GPL-3.0-or-later
// Fixed-size phrase-step automation for the sample-based Granular instrument.
#pragma once

#include "sample_granular.h"
#include <array>
#include <cstdint>
#include <type_traits>

namespace studio::granular::step {

// Stable values saved in .ptp FX cells. Do not reuse these IDs for other effects.
inline constexpr int kFirstFx = 0x60;
enum Field : uint8_t {
    Position, Size, Density, Spray, Pitch, PitchSpray, Scan, Freeze,
    PanSpread, Reverse, Window, Level, Attack, Release, Clear,
};
inline constexpr int kFieldCount = Clear;
inline constexpr int kCommandCount = Clear + 1;
inline constexpr int kLastFx = kFirstFx + Clear;
inline constexpr int kRowSlots = 3;
using Mask = uint16_t;
static_assert(kFieldCount <= 16);

struct Command {
    Field field;
    const char* name;
    const char* title;
    const char* range;
    const char* response;
    uint8_t maxByte;
};

inline constexpr Command kCommands[] = {
    {Position, "GPO", "Grain position", "00=start FF=end of region", "moves the grain scan head", 255},
    {Size, "GSZ", "Grain length", "00=2 ms FF=1000 ms", "new grains; finer at short sizes", 255},
    {Density, "GDE", "Grain density", "00=off FF=120 grains/s", "existing grains fade naturally", 255},
    {Spray, "GSP", "Position spray", "00=none FF=+/-1000 ms", "random start; finer near zero", 255},
    {Pitch, "GPT", "Grain pitch", "00=-48 80=0 FF=+48 semitones", "glides on sounding grains", 255},
    {PitchSpray, "GPS", "Pitch spray", "00=none FF=+/-24 semitones", "random pitch of new grains", 255},
    {Scan, "GSC", "Scan speed", "00=-4x 80=still FF=+4x", "moves through source seconds", 255},
    {Freeze, "GFZ", "Freeze scan", "00=off 01=on", "GPO still moves the position", 1},
    {PanSpread, "GPN", "Grain stereo spread", "00=none FF=full", "random stereo balance of new grains", 255},
    {Reverse, "GRP", "Reverse grain chance", "00=never FF=always", "chosen per new grain", 255},
    {Window, "GWN", "Grain window", "00=Hann FF=triangle", "shape of new grains", 255},
    {Level, "GLV", "Granular level", "00=silent FF=2x", "before instrument volume and FX", 255},
    {Attack, "GAT", "Granular attack", "00=instant FF=5000 ms", "controls an attack, no retrigger", 255},
    {Release, "GRL", "Granular release", "00=1 ms FF=10000 ms", "fade after note off", 255},
    {Clear, "GCL", "Clear Granular step values", "00=restore instrument values", "this track only; no retrigger", 0},
};
static_assert(sizeof(kCommands) / sizeof(*kCommands) == kCommandCount);
inline constexpr bool registryOrder() noexcept {
    for (int i = 0; i < kCommandCount; ++i)
        if (int(kCommands[i].field) != i) return false;
    return true;
}
static_assert(registryOrder());

inline constexpr bool isFx(int type) noexcept { return type >= kFirstFx && type <= kLastFx; }
inline constexpr int code(Field field) noexcept { return kFirstFx + int(field); }
inline constexpr const Command& command(int type) noexcept { return kCommands[type - kFirstFx]; }
inline constexpr uint8_t clampByte(int value, uint8_t max) noexcept {
    return static_cast<uint8_t>(value < 0 ? 0 : value > max ? max : value);
}

struct Operation { uint8_t field, value; };
// Plain old data: safe as an Event union member and as a value in the audio queues.
struct Delta { uint8_t count; Operation ops[kRowSlots]; };
static_assert(std::is_trivial_v<Delta> && sizeof(Delta) == 7);

inline constexpr bool append(Delta& delta, int type, int value) noexcept {
    if (!isFx(type) || delta.count >= kRowSlots) return false;
    const auto& def = command(type);
    delta.ops[delta.count++] = {static_cast<uint8_t>(def.field), clampByte(value, def.maxByte)};
    return true;
}

struct Overlay {
    Mask mask = 0;
    std::array<uint8_t, kFieldCount> bytes{};

    constexpr void clear() noexcept { mask = 0; }
    constexpr bool has(Field field) const noexcept {
        return field < Clear && (mask & (Mask(1) << field));
    }
    constexpr void apply(const Delta& delta) noexcept {
        for (uint8_t i = 0; i < delta.count && i < kRowSlots; ++i) {
            const auto op = delta.ops[i];
            if (op.field == Clear) clear();
            else if (op.field < Clear) {
                bytes[op.field] = clampByte(op.value, kCommands[op.field].maxByte);
                mask |= Mask(1) << op.field;
            }
        }
    }
};

inline constexpr float unit(uint8_t byte) noexcept { return float(byte) / 255.0f; }
inline constexpr float bipolar(uint8_t byte) noexcept {
    return byte < 128 ? float(int(byte) - 128) / 128.0f : float(int(byte) - 128) / 127.0f;
}
inline constexpr float fineLow(uint8_t byte, float min, float max) noexcept {
    const float t = unit(byte);
    return min + (max - min) * (t * t);
}

// Overlay *only* the authored FX fields on current instrument settings. Studio
// edits to every other field can still reach this voice, and the preset never changes.
inline constexpr Parameters effective(Parameters p, const Overlay& overlay) noexcept {
    if (overlay.has(Position)) p.position = unit(overlay.bytes[Position]);
    if (overlay.has(Size)) p.sizeMs = fineLow(overlay.bytes[Size], 2.f, 1000.f);
    if (overlay.has(Density)) p.densityHz = 120.f * unit(overlay.bytes[Density]);
    if (overlay.has(Spray)) p.sprayMs = fineLow(overlay.bytes[Spray], 0.f, 1000.f);
    if (overlay.has(Pitch)) p.pitchSemitones = 48.f * bipolar(overlay.bytes[Pitch]);
    if (overlay.has(PitchSpray)) p.pitchSpraySemitones = 24.f * unit(overlay.bytes[PitchSpray]);
    if (overlay.has(Scan)) p.scanSpeed = 4.f * bipolar(overlay.bytes[Scan]);
    if (overlay.has(Freeze)) p.freeze = overlay.bytes[Freeze] != 0;
    if (overlay.has(PanSpread)) p.panSpread = unit(overlay.bytes[PanSpread]);
    if (overlay.has(Reverse)) p.reverseProbability = unit(overlay.bytes[Reverse]);
    if (overlay.has(Window)) p.windowShape = unit(overlay.bytes[Window]);
    if (overlay.has(Level)) p.level = 2.f * unit(overlay.bytes[Level]);
    if (overlay.has(Attack)) p.attackMs = fineLow(overlay.bytes[Attack], 0.f, 5000.f);
    if (overlay.has(Release)) p.releaseMs = fineLow(overlay.bytes[Release], 1.f, 10000.f);
    return p;
}

struct SourceKey {
    int instrument = -1, sample = -1;
    uint32_t generation = 0;
    uint32_t modeEpoch = 0;
    constexpr bool valid() const noexcept { return instrument >= 0 && sample >= 0; }
    constexpr bool operator==(const SourceKey& other) const noexcept {
        return instrument == other.instrument && sample == other.sample && generation == other.generation &&
               modeEpoch == other.modeEpoch;
    }
};

// An audio-thread latch, one per sequencer lane. The engine checks that the
// backing source still matches before accepting an FX-only row.
struct TrackOverlay {
    SourceKey source{};
    Overlay overlay{};
    constexpr void reset() noexcept { source = {}; overlay.clear(); }
    constexpr void bind(SourceKey next) noexcept {
        if (!(source == next)) overlay.clear();
        source = next;
    }
};

// The dispatch pass runs before rendering, so capture the track overlay at
// each actual update frame. A later update must not affect the earlier PCM.
struct TimedOverlay { int frame; Overlay overlay; };
struct VoiceOverlay {
    static constexpr uint8_t kPending = 32;
    SourceKey source{};
    Overlay overlay{};
    std::array<TimedOverlay, kPending> pending{};
    uint8_t count = 0, cursor = 0;

    void beginBlock() noexcept { count = cursor = 0; }
    void bind(const TrackOverlay& track) noexcept {
        source = track.source;
        overlay = track.overlay;
        beginBlock();
    }
    void reset() noexcept { bind(TrackOverlay{}); }
    bool schedule(int frame, const Overlay& state) noexcept {
        if (count && pending[count - 1].frame == frame) {
            pending[count - 1].overlay = state;
            return true;
        }
        if (count == kPending) {
            // Bound the work but preserve the eventual value. The caller
            // publishes an overrun counter; no hidden change to another lane.
            pending[kPending - 1] = {frame, state};
            return false;
        }
        pending[count++] = {frame, state};
        return true;
    }
    bool applyThrough(int frame) noexcept {
        const auto before = cursor;
        while (cursor < count && pending[cursor].frame <= frame) overlay = pending[cursor++].overlay;
        return cursor != before;
    }
};

} // namespace studio::granular::step
