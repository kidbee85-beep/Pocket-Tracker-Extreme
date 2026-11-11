// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace studio {

// A separate, disk-backed recorder for Granular performance. One control/UI
// thread, one live engine callback producer, and one owned disk worker. The
// old in-memory Punch recorder and its length limits remain independent.
class GranularDiskTake final {
public:
    enum class State { Idle, Starting, Recording, Stopping, Ready, Interrupted, Failed, Cancelled };
    enum class Issue { None, NoAudio, BufferOverrun, FormatChanged, LowSpace, OpenFailed, WriteFailed, FinalizeFailed };
    struct Part { std::string path; uint64_t frames = 0; };
    struct Snapshot {
        State state = State::Idle;
        Issue issue = Issue::None;
        int rate = 0;
        uint64_t writtenFrames = 0;
        std::vector<Part> parts;
        std::string recoverablePart;  // Incomplete/unpublished; never offer as a ready take.
        std::string message;
    };
    struct Limits {
        // Zero uses the actual RIFF/FAT/seek boundary. Nonzero permits small
        // rollover fixtures; callers use the default in normal operation.
        uint64_t segmentFrames = 0;
        uint64_t reserveBytes = 1024u * 1024u;
    };
    static constexpr uint32_t RingFrames = 65536;
    static constexpr size_t PcmBufferBytes = size_t(RingFrames) * 4;

    GranularDiskTake();
    explicit GranularDiskTake(Limits limits);
    ~GranularDiskTake();
    GranularDiskTake(const GranularDiskTake&) = delete;
    GranularDiskTake& operator=(const GranularDiskTake&) = delete;

    // Native absolute directory (not a SAF/URI), sample rate of the live mix.
    // UI never opens/writes the file or joins a running worker to start/stop.
    bool start(const std::string& directory, int rate);
    void request_stop() noexcept;
    bool active() const noexcept;
    Snapshot snapshot() const;

    // Audio thread ONLY, single producer. Mono duplicates L to R; stereo
    // interleaved. No allocation, file access, lock, wait, or logging.
    void append(const float* pcm, int frames, int channels, int rate) noexcept;
    static uint64_t compatible_segment_frames() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace studio
