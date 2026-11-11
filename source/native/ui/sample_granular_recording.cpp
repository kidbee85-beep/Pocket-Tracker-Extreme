// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/sample_granular_recording.h"

#include "audio-engine.h"
#include "songcore/host.h"
#include "ui/app_state.h"
#include "ui/sample_granular_ui.h"
#include "ui/std_filesystem.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace pt::ui {
namespace {
using State = studio::GranularDiskTake::State;

uint64_t elapsed_seconds(uint64_t frames, int rate) {
    return rate > 0 ? frames / static_cast<uint64_t>(rate) : 0;
}

std::string duration(uint64_t seconds) {
    char text[40];
    if (seconds >= 3600) {
        std::snprintf(text, sizeof(text), "%lluh %02llum %02llus",
                      static_cast<unsigned long long>(seconds / 3600),
                      static_cast<unsigned long long>((seconds / 60) % 60),
                      static_cast<unsigned long long>(seconds % 60));
    } else {
        std::snprintf(text, sizeof(text), "%02llu:%02llu",
                      static_cast<unsigned long long>(seconds / 60),
                      static_cast<unsigned long long>(seconds % 60));
    }
    return text;
}

void stop_owned_root(SampleGranularRecordingView& view, songcore::SongcoreHost& host) {
    view.rootPending = false;
    if (view.ownsRoot) {
        view.ownsRoot = false;
        host.stop_preview(true);
    }
    view.solo = false;
}

bool root_context_matches(const AppState& s, const SampleGranularRecordingView& view,
                          const songcore::SongcoreHost& host) {
    return s.studioOpen && s.project && s.project == view.originProject &&
           s.currentInstrument == view.originSlot && view.originSlot >= 0 &&
           view.originSlot < int(host.project().instruments.size()) &&
           host.project().instruments[view.originSlot].granularEnabled;
}

std::string saved_part(const SampleGranularRecordingView& view) {
    if (view.readyParts.empty()) return {};
    const auto part = std::min(view.part, view.readyParts.size() - 1);
    std::string label = path_name(view.readyParts[part].path);
    if (view.readyParts.size() > 1) {
        label = std::to_string(part + 1) + "/" + std::to_string(view.readyParts.size()) + "  " + label;
    }
    return label;
}
} // namespace

bool granular_recording_busy(const SampleGranularRecordingView& view) {
    return view.state == State::Starting || view.state == State::Recording || view.state == State::Stopping;
}

bool granular_recording_has_take(const SampleGranularRecordingView& view) {
    return !granular_recording_busy(view) && !view.readyParts.empty();
}

std::string granular_recording_status(const SampleGranularRecordingView& view) {
    if (view.state == State::Starting) return view.solo ? "Opening WAV / granular root" : "Opening WAV / master mix";
    if (view.state == State::Recording) {
        return duration(elapsed_seconds(view.frames, view.rate)) +
               (view.solo ? "  /  GRANULAR ROOT" : "  /  MASTER MIX");
    }
    if (view.state == State::Stopping) return "Finishing WAV...";
    if (view.state == State::Ready) return "SAVED  " + saved_part(view);
    if (!view.message.empty()) return view.message;
    if (granular_recording_has_take(view)) return "SAVED  " + saved_part(view);
    return "Stereo WAV / manual stop";
}

void granular_recording_select_part(AppState& s, int direction) {
    auto& view = s.sampleGranular.recording;
    if (!granular_recording_has_take(view) || view.readyParts.size() < 2) return;
    const size_t count = view.readyParts.size();
    view.part = direction < 0 ? (view.part + count - 1) % count : (view.part + 1) % count;
    s.studioMessage = saved_part(view);
}

bool sample_granular_recording_toggle(AppState& s, songcore::SongcoreHost& host,
                                      AudioEngine& engine, FileSystem& fs) {
    auto& view = s.sampleGranular.recording;
    auto& recorder = engine.granularDiskTake;
    if (granular_recording_busy(view) || recorder.active()) {
        // A delayed update of the STOP button must never start another take.
        if (view.state != State::Stopping && recorder.active()) {
            recorder.request_stop();
            view.state = State::Stopping;
            stop_owned_root(view, host);
        }
        view.nextPollMs = 0;
        return true;
    }

    const bool solo = !host.is_playing();
    uint32_t generation = 0;
    if (solo) {
        const int slot = s.currentInstrument;
        if (slot < 0 || slot >= int(host.project().instruments.size()) ||
            !host.project().instruments[slot].granularEnabled) {
            s.studioMessage = "Load a source and enable Granular to record its root note";
            return false;
        }
        const auto source = host.sample_granular_snapshot(slot, AudioEngine::PREVIEW_LANE);
        s.sampleGranular.publisherFocused = true;
        if (!source.sourceValid || s.sampleGranular.waitingForSource) {
            s.studioMessage = "Load an actual sample before recording its root note";
            return false;
        }
        generation = source.sourceGeneration;
    }

    const std::string folder = fs.make_app_folder("Samples/Resampled/Granular");
    if (folder.empty() || folder.find("://") != std::string::npos ||
        !std::filesystem::path(folder).is_absolute()) {
        s.studioMessage = "Granular recording needs a writable local sample folder";
        return false;
    }
    if (!recorder.start(folder, int(engine.getSampleRate()))) {
        const auto rejected = recorder.snapshot();
        s.studioMessage = rejected.message.empty() ? "Could not start Granular recording" : rejected.message;
        view.nextPollMs = 0;
        return false;
    }

    view.state = State::Starting;
    view.rate = int(engine.getSampleRate());
    view.frames = 0;
    view.message.clear();
    view.originProject = s.project;
    view.originSlot = s.currentInstrument;
    view.originSourceGeneration = generation;
    view.solo = solo;
    view.rootPending = solo;
    view.ownsRoot = false;
    view.nextPollMs = 0;
    s.studioMessage.clear();
    return true;
}

bool sample_granular_request_take(AppState& s) {
    auto& view = s.sampleGranular.recording;
    if (!granular_recording_has_take(view)) {
        s.studioMessage = granular_recording_busy(view) ? "Stop and save the recording first" : "Record a take first";
        return false;
    }
    view.requestedPath = view.readyParts[std::min(view.part, view.readyParts.size() - 1)].path;
    s.sampleGranular.request = SampleGranularRequest::USE_RECORDED_TAKE;
    return true;
}

bool sample_granular_recording_poll(AppState& s, songcore::SongcoreHost& host,
                                    AudioEngine& engine, uint64_t nowMs) {
    auto& view = s.sampleGranular.recording;
    if (nowMs < view.nextPollMs) return false;
    view.nextPollMs = nowMs + (view.state == State::Starting ? 16 : 125);

    const auto next = engine.granularDiskTake.snapshot();
    const bool stateChanged = next.state != view.state;
    bool changed = stateChanged || next.message != view.message ||
                   elapsed_seconds(next.writtenFrames, next.rate) != elapsed_seconds(view.frames, view.rate);
    view.state = next.state;
    view.rate = next.rate;
    view.frames = next.writtenFrames;
    view.message = next.message;

    if (view.rootPending || view.ownsRoot) {
        bool pinned = root_context_matches(s, view, host) && !host.is_playing();
        if (pinned && s.studioTab == 0 && !sample_granular_studio(s)) pinned = false;
        if (pinned && view.state == State::Recording) {
            const auto source = host.sample_granular_snapshot(view.originSlot, AudioEngine::PREVIEW_LANE);
            s.sampleGranular.publisherFocused = true;
            pinned = source.sourceValid && source.sourceGeneration == view.originSourceGeneration;
        }
        if (!pinned || (view.state != State::Starting && view.state != State::Recording)) {
            stop_owned_root(view, host);
            changed = true;
        } else if (view.rootPending && view.state == State::Recording) {
            // Wait until the recorder can accept PCM before starting the root
            // note. If a held note already exists, transfer its ownership.
            if (s.studioAuditionHeld) s.studioAuditionHeld = false;
            else host.preview_instrument(view.originSlot);
            view.rootPending = false;
            view.ownsRoot = true;
            changed = true;
        }
    }

    if ((view.state == State::Ready || view.state == State::Interrupted) && !next.parts.empty() &&
        (stateChanged || view.readyParts.empty() || next.parts.back().path != view.readyParts.back().path)) {
        view.readyParts = next.parts;
        view.part = 0;
        changed = true;
    }

    return changed && s.studioOpen && sample_granular_studio(s);
}

void sample_granular_recording_interrupt(AppState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    auto& view = s.sampleGranular.recording;
    if (engine.granularDiskTake.active()) engine.granularDiskTake.request_stop();
    stop_owned_root(view, host);
    view.nextPollMs = 0;
}
} // namespace pt::ui
