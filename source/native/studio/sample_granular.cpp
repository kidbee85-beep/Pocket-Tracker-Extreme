// SPDX-License-Identifier: GPL-3.0-or-later
#include "sample_granular.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace studio::granular {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kWindowPoints = 513;
// A compile-time Hann table: no function-local initializer/guard on the audio thread.
constexpr double cosine(double x) {
    if (x > kPi) x -= 2. * kPi;
    double term = 1., sum = 1.;
    for (int n = 1; n <= 12; ++n) {
        term *= -x * x / double((2 * n - 1) * (2 * n));
        sum += term;
    }
    return sum;
}
constexpr std::array<float, kWindowPoints> makeWindow() {
    std::array<float, kWindowPoints> out{};
    for (int i = 1; i < kWindowPoints - 1; ++i)
        out[size_t(i)] = float(.5 - .5 * cosine(2. * kPi * i / (kWindowPoints - 1)));
    return out;
}
constexpr auto kHann = makeWindow();

// Bit classification stays valid when the enclosing application uses fast-math.
bool finite(float v) noexcept {
    uint32_t bits; std::memcpy(&bits, &v, sizeof(bits));
    // A volatile INTEGER boundary prevents fast-math from reinterpreting this as isfinite(v)
    // and then assuming it is always true. This is local state, not synchronization or a lock.
    const volatile uint32_t classified = bits;
    return (classified & 0x7f800000u) != 0x7f800000u;
}
bool finite(double v) noexcept {
    uint64_t bits; std::memcpy(&bits, &v, sizeof(bits));
    const volatile uint64_t classified = bits;
    return (classified & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
}
float bounded(float v, float low, float high, float fallback) noexcept {
    return finite(v) ? std::clamp(v, low, high) : fallback;
}
float audioValue(float v) noexcept {
    // PCM floats beyond normal audio headroom cannot overflow a summed grain cloud.
    return finite(v) ? std::clamp(v, -8.f, 8.f) : 0.f;
}
} // namespace

Parameters Voice::sanitize(Parameters p) noexcept {
    p.position = bounded(p.position, 0.f, 1.f, .5f);
    p.sizeMs = bounded(p.sizeMs, 2.f, 1000.f, 100.f);
    p.densityHz = bounded(p.densityHz, 0.f, 120.f, 20.f);
    p.sprayMs = bounded(p.sprayMs, 0.f, 1000.f, 0.f);
    p.pitchSemitones = bounded(p.pitchSemitones, -48.f, 48.f, 0.f);
    p.pitchSpraySemitones = bounded(p.pitchSpraySemitones, 0.f, 24.f, 0.f);
    p.scanSpeed = bounded(p.scanSpeed, -4.f, 4.f, 0.f);
    p.panSpread = bounded(p.panSpread, 0.f, 1.f, 0.f);
    p.reverseProbability = bounded(p.reverseProbability, 0.f, 1.f, 0.f);
    p.windowShape = bounded(p.windowShape, 0.f, 1.f, 0.f);
    p.level = bounded(p.level, 0.f, 2.f, .5f);
    p.attackMs = bounded(p.attackMs, 0.f, 5000.f, 5.f);
    p.releaseMs = bounded(p.releaseMs, 1.f, 10000.f, 100.f);
    return p;
}

bool parametersEqual(const Parameters& a, const Parameters& b) noexcept {
    return a.position==b.position && a.sizeMs==b.sizeMs && a.densityHz==b.densityHz &&
        a.sprayMs==b.sprayMs && a.pitchSemitones==b.pitchSemitones &&
        a.pitchSpraySemitones==b.pitchSpraySemitones && a.scanSpeed==b.scanSpeed &&
        a.freeze==b.freeze && a.panSpread==b.panSpread &&
        a.reverseProbability==b.reverseProbability && a.windowShape==b.windowShape &&
        a.level==b.level && a.attackMs==b.attackMs && a.releaseMs==b.releaseMs;
}
bool validParameters(const Parameters& p) noexcept {
    const float values[]={p.position,p.sizeMs,p.densityHz,p.sprayMs,p.pitchSemitones,
        p.pitchSpraySemitones,p.scanSpeed,p.panSpread,p.reverseProbability,p.windowShape,
        p.level,p.attackMs,p.releaseMs};
    for (float value:values) if (!finite(value)) return false;
    return parametersEqual(p,Voice::sanitize(p));
}

bool Voice::start(SampleView source, Parameters parameters, double noteRate,
                  double outputRate, uint32_t seed) noexcept {
    if (!source.left || source.frames < 1 || source.frames > INT32_MAX ||
        !finite(source.sourceRate) || source.sourceRate < 8000. || source.sourceRate > 768000. ||
        !finite(outputRate) || outputRate < 8000. || outputRate > 768000. ||
        !finite(noteRate) || noteRate <= 0.) return false;
    if (source.endFrame == 0) source.endFrame = source.frames;
    if (source.firstFrame < 0 || source.endFrame > source.frames ||
        source.endFrame <= source.firstFrame) return false;
    stop();
    source_ = source;
    parameters_ = sanitize(parameters);
    outputRate_ = outputRate;
    noteRate_ = std::clamp(noteRate, 1. / 256., 256.);
    initialSeed_ = seed;
    rng_ = seed ? seed : 0x6d2b79f5u;
    frames_ = spawned_ = skipped_ = 0;
    active_ = true;
    firstSpawn_ = true;
    spawnPhase_ = positionDelta_ = 0.;
    scanFrame_ = source.firstFrame + parameters_.position * double(source.endFrame - source.firstFrame - 1);
    smoothing_ = 1. - std::exp(-1. / (.01 * outputRate_));
    density_ = parameters_.densityHz;
    scanSpeed_ = parameters_.scanSpeed;
    level_ = parameters_.level;
    normalizer_ = 1.f;
    envelope_ = parameters_.attackMs > 0.f ? 0.f : 1.f;
    updateTargets();
    readRate_ = targetReadRate_;
    return true;
}

void Voice::updateTargets() noexcept {
    const double requested = source_.sourceRate / outputRate_ * noteRate_ *
                             std::exp2(double(parameters_.pitchSemitones) / 12.);
    targetReadRate_ = std::clamp(requested, 1. / 65536., 64.);
    rateClamped_ = targetReadRate_ != requested;
    attackStep_ = float(1. / std::max(1., outputRate_ * parameters_.attackMs * .001));
}

void Voice::update(Parameters parameters) noexcept {
    const auto next = sanitize(parameters);
    const bool releaseChanged = next.releaseMs != parameters_.releaseMs;
    if (active_ && next.position != parameters_.position) {
        const double desired = source_.firstFrame + next.position * double(source_.endFrame - source_.firstFrame - 1);
        positionDelta_ = desired - scanFrame_;
    }
    parameters_ = next;
    if (active_) updateTargets();
    // Hosts may submit an unchanged parameter block every audio piece. Do not restart the
    // release countdown on each update, which would leave a note ringing asymptotically.
    if (active_ && releasing_ && releaseChanged)
        releaseStep_ = envelope_ / float(std::max(1., outputRate_ * parameters_.releaseMs * .001));
}

void Voice::setNoteRate(double rate) noexcept {
    if (!finite(rate) || rate <= 0.) return;
    noteRate_ = std::clamp(rate, 1. / 256., 256.);
    if (active_) updateTargets();
}

void Voice::release() noexcept {
    if (!active_ || releasing_) return;
    releasing_ = true;
    releaseStep_ = envelope_ / float(std::max(1., outputRate_ * parameters_.releaseMs * .001));
}

void Voice::stop() noexcept {
    active_ = releasing_ = false;
    envelope_ = 0.f;
    for (auto& g : grains_) g = Grain{};
    source_ = SampleView{};
}

float Voice::randomBipolar() noexcept {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return float(rng_ >> 8) / 8388608.f - 1.f;
}

double Voice::wrapFrame(double position) const noexcept {
    if (position >= source_.firstFrame && position < source_.endFrame) return position;
    const double span = double(source_.endFrame - source_.firstFrame);
    double local = position - source_.firstFrame;
    local -= std::floor(local / span) * span;
    const double result = source_.firstFrame + std::max(0., local);
    // Rounding may turn first+local into end for large absolute offsets.
    return result < source_.endFrame ? result : double(source_.firstFrame);
}

float Voice::read(const float* data, double position) const noexcept {
    const int64_t i = std::clamp<int64_t>(static_cast<int64_t>(position),
                                        source_.firstFrame, source_.endFrame - 1);
    const int64_t j = i + 1 < source_.endFrame ? i + 1 : source_.firstFrame;
    const float a = audioValue(data[i]), b = audioValue(data[j]);
    return a + (b - a) * float(position - double(i));
}

float Voice::window(const Grain& grain) noexcept {
    const float phase = float(grain.age) / float(grain.length - 1);
    const float index = phase * float(kWindowPoints - 1);
    const int i = std::min(kWindowPoints - 2, int(index));
    const float hann = kHann[size_t(i)] + (kHann[size_t(i + 1)] - kHann[size_t(i)]) * (index - i);
    const float triangle = 1.f - std::fabs(2.f * phase - 1.f);
    return std::max(0.f, hann + (triangle - hann) * grain.shape);
}

void Voice::spawn() noexcept {
    Grain* free = nullptr;
    for (auto& grain : grains_) if (!grain.active) { free = &grain; break; }
    if (!free) { ++skipped_; return; }
    auto& g = *free;
    g = Grain{};
    g.active = true;
    g.id = ++spawned_;
    g.position = wrapFrame(scanFrame_ + randomBipolar() * parameters_.sprayMs * .001 * source_.sourceRate);
    g.ratio = std::exp2(double(randomBipolar() * parameters_.pitchSpraySemitones) / 12.);
    g.direction = (randomBipolar() + 1.f) * .5f < parameters_.reverseProbability ? -1 : 1;
    g.length = uint32_t(std::max(4., std::round(outputRate_ * parameters_.sizeMs * .001)));
    g.pan = randomBipolar() * parameters_.panSpread;
    g.gainLeft = std::sqrt(1.f - g.pan);
    g.gainRight = std::sqrt(1.f + g.pan);
    g.shape = parameters_.windowShape;
}

Frame Voice::nextStereo() noexcept {
    Frame output;
    if (!active_) return output;
    const float smooth = float(smoothing_);
    readRate_ += (targetReadRate_ - readRate_) * smoothing_;
    density_ += (parameters_.densityHz - density_) * smooth;
    scanSpeed_ += (parameters_.scanSpeed - scanSpeed_) * smooth;
    level_ += (parameters_.level - level_) * smooth;
    if (std::fabs(positionDelta_) > 1.e-8) {
        const double movement = positionDelta_ * smoothing_;
        scanFrame_ = wrapFrame(scanFrame_ + movement);
        positionDelta_ -= movement;
    }
    if (!releasing_) {
        if (density_ > 0.f) {
            if (firstSpawn_) { spawn(); firstSpawn_ = false; }
            else if (spawnPhase_ >= 1.) { spawn(); spawnPhase_ -= 1.; }
            spawnPhase_ += density_ / outputRate_;
        }
        envelope_ = std::min(1.f, envelope_ + attackStep_);
    } else {
        envelope_ = std::max(0.f, envelope_ - releaseStep_);
        if (envelope_ <= 0.f) { stop(); return output; }
    }
    int activeCount = 0;
    for (auto& g : grains_) if (g.active) {
        ++activeCount;
        const float gain = window(g);
        const float left = read(source_.left, g.position);
        const float right = source_.right ? read(source_.right, g.position) : left;
        output.left += left * gain * g.gainLeft;
        output.right += right * gain * g.gainRight;
        g.position = wrapFrame(g.position + readRate_ * g.ratio * g.direction);
        if (++g.age >= g.length) g.active = false;
    }
    // Smooth a bounded overlap gain rather than changing the gain abruptly at spawn/end.
    const float targetNormalizer = 1.f / float(std::max(1, activeCount));
    normalizer_ += (targetNormalizer - normalizer_) * smooth;
    const float gain = level_ * envelope_ * normalizer_;
    output.left *= gain;
    output.right *= gain;
    if (!parameters_.freeze)
        scanFrame_ = wrapFrame(scanFrame_ + scanSpeed_ * source_.sourceRate / outputRate_);
    ++frames_;
    return output;
}

void Voice::render(float* left, float* right, int frames) noexcept {
    if (!left || !right || frames < 1) return;
    for (int i = 0; i < frames; ++i) {
        const auto frame = nextStereo();
        left[i] = frame.left; right[i] = frame.right;
    }
}

void Voice::renderAdd(float* stereo, int frames) noexcept {
    if (!stereo || frames < 1) return;
    for (int i = 0; i < frames; ++i) {
        const auto frame = nextStereo();
        stereo[size_t(i) * 2] += frame.left; stereo[size_t(i) * 2 + 1] += frame.right;
    }
}

Snapshot Voice::snapshot() const noexcept {
    Snapshot out;
    out.active = active_; out.releasing = releasing_; out.frozen = parameters_.freeze;
    out.rateClamped = rateClamped_;
    out.sourceGeneration = source_.generation; out.seed = initialSeed_;
    out.sourceValid = source_.left != nullptr;
    out.sourceFrames = source_.frames; out.sourceRate = source_.sourceRate;
    out.firstFrame = source_.firstFrame; out.endFrame = source_.endFrame;
    out.renderedFrames = frames_; out.spawned = spawned_; out.skippedSpawns = skipped_;
    out.scanSourceFrame = scanFrame_; out.readIncrement = readRate_; out.envelope = envelope_;
    for (size_t i = 0; i < grains_.size(); ++i) {
        const auto& g = grains_[i];
        if (!g.active) continue;
        ++out.activeGrains;
        out.grains[i] = {true, g.id, g.position, readRate_ * g.ratio * g.direction,
                         g.age, g.length, window(g), g.pan};
    }
    return out;
}
} // namespace studio::granular
