#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <memory>
#include <string>

namespace pt {
struct LocalWavSnapshot {
    bool open=false;
    bool playing=false;
    bool atEnd=false;
    bool exporting=false;
    int rate=0;
    int channels=0;
    int bits=0;
    bool isFloat=false;
    int64_t totalFrames=0;
    int64_t dataBytes=0;
    int64_t positionFrame=0;
    double exportProgress=0.;
    std::string path;
    std::string error;
};

// Control methods/snapshot/export run outside the audio callback. The worker
// owns playback file reads/seeks. Metadata and memory are independent of file
// duration. The source file is never rewritten.
class LocalWavSource {
public:
    LocalWavSource();
    ~LocalWavSource();
    LocalWavSource(const LocalWavSource&)=delete;
    LocalWavSource& operator=(const LocalWavSource&)=delete;

    // Synchronous metadata open; playback is initially paused at frame zero.
    // A failed open closes the previous source, so stale audio cannot resume.
    bool open(const std::string& path);
    void close() noexcept;
    bool play() noexcept;
    void pause() noexcept;
    void stop() noexcept;  // pause and rewind, keeping the source open
    bool seek(int64_t sourceFrame) noexcept; // clamp; preserve playing/paused state
    LocalWavSnapshot snapshot() const;

    // ADDS preview audio to the supplied interleaved stereo buffer. Paused or
    // unavailable audio leaves it unchanged. Single audio-consumer thread only.
    // No allocations, file I/O, mutexes, waits or notifications in this method.
    void render_audio(float* stereo,int frames,int sampleRate) noexcept;

    // Synchronous bounded raw-PCM copy; invoke on a UI/background job, never
    // the audio callback. Interval is [firstFrame,endFrame). Destination must
    // be newly created exclusively; existing files are always retained.
    // Preserves source rate/depth/format, rebases included WAV cue points.
    // Failure/cancel removes only an output created by this operation.
    bool export_region(const std::string& destinationPath,int64_t firstFrame,int64_t endFrame);
    void cancel_export() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace pt
