// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "studio/granular_disk_take.h"
#include <cstddef>

namespace songcore { class SongcoreHost; struct Project; }
class AudioEngine;

namespace pt::ui {
struct AppState;
class FileSystem;

// The actions follow the five existing Granular controls so their indices
// and input paths remain stable.
inline constexpr int kGranularRecordAction = 5;
inline constexpr int kGranularUseTakeAction = 6;
inline constexpr int kGranularActionCount = 7;

struct SampleGranularRecordingView {
    studio::GranularDiskTake::State state = studio::GranularDiskTake::State::Idle;
    uint64_t nextPollMs = 0;
    uint64_t frames = 0;
    int rate = 0;
    std::string message;
    std::vector<studio::GranularDiskTake::Part> readyParts;
    std::size_t part = 0;

    // Only the recorder's own audition may be stopped by its Stop button.
    const songcore::Project* originProject = nullptr;
    int originSlot = -1;
    uint32_t originSourceGeneration = 0;
    bool rootPending = false;
    bool ownsRoot = false;
    bool solo = false;

    std::string requestedPath;
};

bool granular_recording_busy(const SampleGranularRecordingView&);
bool granular_recording_has_take(const SampleGranularRecordingView&);
std::string granular_recording_status(const SampleGranularRecordingView&);
void granular_recording_select_part(AppState&, int direction);
bool sample_granular_recording_toggle(AppState&, songcore::SongcoreHost&, AudioEngine&, FileSystem&);
bool sample_granular_request_take(AppState&);
bool sample_granular_recording_poll(AppState&, songcore::SongcoreHost&, AudioEngine&, uint64_t nowMs);
void sample_granular_recording_interrupt(AppState&, songcore::SongcoreHost&, AudioEngine&);
} // namespace pt::ui
