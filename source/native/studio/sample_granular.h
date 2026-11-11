// SPDX-License-Identifier: GPL-3.0-or-later
// Original sample-based granular DSP. This is independent of the legacy synthetic G models.
#pragma once
#include <array>
#include <cstdint>

namespace studio::granular {

inline constexpr int kMaxGrains = 16;

// Non-owning immutable PCM. The owner must keep both arrays alive and unchanged until stop().
// endFrame==0 selects frames; the playable interval is [firstFrame,endFrame).
// generation is an identity tag, not a lifetime guard: the host must invalidate before freeing PCM.
struct SampleView {
    const float* left = nullptr;
    const float* right = nullptr; // null means mono
    int64_t frames = 0;
    double sourceRate = 48000.;
    uint32_t generation = 0;
    int64_t firstFrame = 0;
    int64_t endFrame = 0;
};

// Physical units; sanitized on start/update. No hidden oscillators or source-free fallback.
struct Parameters {
    float position = .5f;              // normalized selected region, 0..1
    float sizeMs = 100.f;              // 2..1000 ms; latched for each new grain
    float densityHz = 20.f;            // 0..120; zero stops new grains
    float sprayMs = 0.f;               // +/- source milliseconds, 0..1000
    float pitchSemitones = 0.f;        // -48..48, applied smoothly to existing grains
    float pitchSpraySemitones = 0.f;   // +/- 0..24; latched per grain
    float scanSpeed = 0.f;             // source seconds per output second, -4..4
    bool freeze = false;              // stops automatic scan, permits manual position edits
    float panSpread = 0.f;             // 0..1; random per-grain stereo balance
    float reverseProbability = 0.f;    // 0..1
    float windowShape = 0.f;           // 0=Hann, 1=triangle
    float level = .5f;                 // 0..2, smoothed
    float attackMs = 5.f;              // 0..5000
    float releaseMs = 100.f;           // 1..10000
};

struct Frame { float left = 0.f, right = 0.f; };

struct GrainSnapshot {
    bool active = false;
    uint64_t id = 0;
    double sourceFrame = 0.;
    double signedReadIncrement = 0.;
    uint32_t ageFrames = 0, lengthFrames = 0;
    float window = 0.f, pan = 0.f;
};

struct Snapshot {
    bool active = false, releasing = false, frozen = false, rateClamped = false;
    bool sourceValid = false;
    int instrumentId = -1, trackId = -1, voiceSlot = -1; // filled by host integration
    int64_t sourceFrames = 0, firstFrame = 0, endFrame = 0;
    double sourceRate = 0.;
    uint32_t sourceGeneration = 0, seed = 0;
    uint64_t renderedFrames = 0, spawned = 0, skippedSpawns = 0;
    int activeGrains = 0;
    double scanSourceFrame = 0., readIncrement = 0.;
    float envelope = 0.f;
    std::array<GrainSnapshot, kMaxGrains> grains{};
};

bool validParameters(const Parameters&) noexcept;
bool parametersEqual(const Parameters&, const Parameters&) noexcept;

// One voice, owned exclusively by the audio thread, including update() and snapshot().
// The control thread must publish Parameters through a host-owned mailbox before calling update.
// snapshot() returns a value for a host-owned publisher; never read this Voice concurrently.
// Construction/start/update/render/release/stop/snapshot do not allocate, lock or perform I/O.
class Voice {
public:
    // Invalid arguments leave the previous voice untouched. noteRate is the note/root ratio;
    // sourceRate/outputRate conversion is applied here exactly once.
    bool start(SampleView source, Parameters parameters, double noteRate,
               double outputRate, uint32_t seed) noexcept;
    void update(Parameters parameters) noexcept;
    void setNoteRate(double noteRate) noexcept;
    void release() noexcept; // stop spawning, fade existing grains with the note envelope
    void stop() noexcept;    // immediate silence; releases the borrowed view
    Frame nextStereo() noexcept;
    void render(float* left, float* right, int frames) noexcept; // overwrite planar outputs
    void renderAdd(float* stereo, int frames) noexcept;         // add interleaved stereo
    Snapshot snapshot() const noexcept;
    bool active() const noexcept { return active_; }
    Parameters parameters() const noexcept { return parameters_; }
    static Parameters sanitize(Parameters) noexcept;

private:
    struct Grain {
        bool active = false;
        uint64_t id = 0;
        double position = 0., ratio = 1.;
        uint32_t age = 0, length = 4;
        float pan = 0.f, gainLeft = 1.f, gainRight = 1.f, shape = 0.f;
        int direction = 1;
    };
    float randomBipolar() noexcept;
    double wrapFrame(double) const noexcept;
    float read(const float*, double) const noexcept;
    static float window(const Grain&) noexcept;
    void updateTargets() noexcept;
    void spawn() noexcept;

    SampleView source_{};
    Parameters parameters_{};
    std::array<Grain, kMaxGrains> grains_{};
    bool active_ = false, releasing_ = false, firstSpawn_ = true, rateClamped_ = false;
    uint32_t rng_ = 1, initialSeed_ = 1;
    uint64_t frames_ = 0, spawned_ = 0, skipped_ = 0;
    double outputRate_ = 48000., noteRate_ = 1., scanFrame_ = 0.;
    double positionDelta_ = 0., spawnPhase_ = 0., smoothing_ = .001;
    double readRate_ = 1., targetReadRate_ = 1.;
    float density_ = 20.f, scanSpeed_ = 0.f, level_ = .5f, normalizer_ = 1.f;
    float envelope_ = 0.f, attackStep_ = 1.f, releaseStep_ = 1.f;
};

} // namespace studio::granular
