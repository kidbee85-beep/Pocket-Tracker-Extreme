#pragma once
#include "ui/buttons.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class AudioEngine;
namespace songcore { class SongcoreHost; struct Project; }

namespace pt::ui {
class Canvas;
struct Theme;

class SourceBackend {
public:
    virtual ~SourceBackend() = default;
    virtual bool command(const std::string& action, const std::string& argsJson) = 0;
    virtual std::string state_json() = 0;
    // Native platforms use the tracker keyboard; Android retains its own dialog.
    virtual bool take_query_request() { return false; }
};

inline constexpr int SOURCE_SAMPLER_ROWS = 14;
#ifdef PT_MIYOO_PLUS_ARCHIVE
inline constexpr int SOURCE_SAMPLER_PROVIDERS = 2;
inline constexpr int64_t SOURCE_CLIP_MAX_MS = 60000;
#else
inline constexpr int SOURCE_SAMPLER_PROVIDERS = 3;
inline constexpr int64_t SOURCE_CLIP_MAX_MS = 180000;
#endif

struct SourceSamplerItem {
    std::string id, title, creator, kind, url, license;
    int64_t durationMs = 0;
    std::string description,tags,instruments;
};

struct SourceSamplerState {
    SourceBackend* backend = nullptr; // Owned by the platform shell, never by AppState copies.
    int row = 2, col = 0, provider = 0, genre = 0;
    int selectedResult = -1, selectedTrack = -1, targetInstrument = -1;
    int openedResult = -1, playingTrack = -1;
    std::array<bool, SOURCE_SAMPLER_PROVIDERS> providerAvailable = {{true}};
    std::vector<SourceSamplerItem> results, tracks;
    std::string query, title, creator, sourceUrl, licenseUrl;
    std::string description,tags,instruments;
    std::string backendStatus = "idle", status = "Search for music, then choose a source and track.";
    std::string error, clipPath, importPath, invalidatedClipPath;
    int64_t positionMs = 0, durationMs = 0, inMs = 0, outMs = 0, clipFrames = 0;
    int clipRate = 0, clipChannels = 0;
    bool playing = false, recording = false, rendering = false, busy = false, clipReady = false;
    bool importRequested = false, showHelp = false, aUsed = false;
    bool listFocused = false, pendingTrackList = false, sortTitle = false;
    double progress = 0;
    uint64_t lastPollMs = 0;
    bool hasPolled = false;
    std::string lastSnapshot;
};

inline constexpr int source_sampler_row_count() { return SOURCE_SAMPLER_ROWS; }
const char* source_sampler_genre_name(int genre);
const char* source_sampler_provider_name(int provider);
// At most one backend snapshot per100ms. Draws consume only this parsed cache.
bool source_sampler_poll(SourceSamplerState&, uint64_t nowMs);
void source_sampler_stop(SourceSamplerState&);
bool source_sampler_submit_query(SourceSamplerState&, const std::string& query);
bool source_sampler_button(const ButtonEvent&, SourceSamplerState&, songcore::SongcoreHost&, AudioEngine&);
void source_sampler_draw(Canvas&, const SourceSamplerState&, const Theme&, const songcore::Project&);
} // namespace pt::ui
