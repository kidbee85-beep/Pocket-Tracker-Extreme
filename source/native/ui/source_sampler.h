#pragma once
#include "ui/buttons.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class AudioEngine;
namespace songcore { class SongcoreHost; struct Project; }

namespace pt::ui {
class Canvas;
class FileSystem;
struct AppState;
struct Theme;

class SourceBackend {
public:
    virtual ~SourceBackend() = default;
    virtual bool command(const std::string& action, const std::string& argsJson) = 0;
    virtual std::string state_json() = 0;
    // Native platforms use the tracker keyboard; Android retains its own dialog.
    virtual bool take_query_request() { return false; }
};

inline constexpr int SOURCE_QUICK_CLIP_ROW = 17;
inline constexpr int SOURCE_COLLECTION_ROW = 18;
inline constexpr int SOURCE_SAMPLER_ROWS = SOURCE_COLLECTION_ROW + 1;
inline constexpr int SOURCE_SAMPLER_PROVIDERS = 5;
#ifdef PT_MIYOO_PLUS_ARCHIVE
inline constexpr int64_t SOURCE_CLIP_MAX_MS = 60000;
#else
inline constexpr int64_t SOURCE_CLIP_MAX_MS = 180000;
#endif

struct SourceSamplerItem {
    std::string id, title, creator, kind, url, license;
    int64_t durationMs = 0;
    std::string description,tags,instruments;
    bool live = false;
};

struct SourceSamplerState {
    struct ClipRequest {
        int64_t inMs, outMs;
        uint64_t startedMs;
    };

    SourceBackend* backend = nullptr; // Owned by the platform shell, never by AppState copies.
    int row = SOURCE_COLLECTION_ROW, col = 0, provider = 0, genre = 0;
    int selectedResult = -1, selectedTrack = -1, targetInstrument = -1;
    int openedResult = -1, playingTrack = -1;
    std::array<bool, SOURCE_SAMPLER_PROVIDERS> providerAvailable = {{true}};
    std::vector<SourceSamplerItem> results, tracks;
    std::string query, title, creator, sourceUrl, licenseUrl;
    std::string description,tags,instruments;
    std::string backendStatus = "idle", status = "Search for music, then choose a source and track.";
    std::string error, clipPath, importPath, invalidatedClipPath;
    int64_t positionMs = 0, durationMs = 0, inMs = 0, outMs = 0, clipFrames = 0;
    int64_t liveWindowStartMs = 0, liveWindowEndMs = 0, livePlayheadMs = 0;
    int clipRate = 0, clipChannels = 0;
    bool playing = false, recording = false, rendering = false, busy = false, clipReady = false;
    bool importRequested = false, showHelp = false, aUsed = false;
    bool listFocused = false, pendingTrackList = false, sortTitle = false;
    bool showingTracks = false, live = false, liveAvailable = false, previewGainAvailable = false, favoritesAvailable = false;
    bool liveHistoryAvailable = false, liveRewound = false, liveInSet = false, liveOutSet = false;
    bool openWhenReady = false;
    bool importContextBound = false, importIsAutomatic = false;
    bool takeRequestIdsAvailable = false;
    const songcore::Project* importProject = nullptr; // Compared only, never dereferenced.
    int importProjectVersion = 0;
    int importInstrument = -1;
    uint32_t nextTakeRequest = 0, expectedTakeRequest = 0;
    int previewGainPercent = 100, liveCaptureSeconds = 30;
    int quickClipLengthIndex = 2, historyTakeLengthIndex = 2, liveHistorySeconds = 0;
    int collectionProvider = 0, shelfGroup = 0, shelfCursor = 0, stationGenre = 0;
    bool collectionOpen = false, collectionArmed = false;
    bool quickClipArmed = false;
    std::optional<ClipRequest> quickClipRequest;
    std::string catalogProvider, catalogGenre, catalogCollection, shelfTitle;
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
void source_sampler_bind_import_context(AppState&);
bool source_sampler_apply_import(AppState&, songcore::SongcoreHost&, AudioEngine&, FileSystem&);
void source_sampler_draw(Canvas&, const SourceSamplerState&, const Theme&, const songcore::Project&);
} // namespace pt::ui
