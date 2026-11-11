#pragma once

#include "songcore/model.h"
#include "ui/buttons.h"
#include <array>
#include <cstdint>
#include <string>

class AudioEngine;
namespace songcore { class SongcoreHost; }

namespace pt::ui {
class Canvas;
struct Theme;

inline constexpr int DRUMLAB_LANES = 8;
inline constexpr int DRUMLAB_MAX_STEPS = 64;
inline constexpr int DRUMLAB_ROWS = 20;
inline constexpr int DRUMLAB_EUCLIDEAN = 8;
inline constexpr int DRUMLAB_STEP_HISTORY = 32;

struct DrumLabStep {
    uint8_t velocity = 0;       // 0 = rest; otherwise the tracker's 1..127 V column.
    uint8_t delayTicks = 0;     // LAT, 0..11 native tics; never crosses the next step.
    bool operator==(const DrumLabStep& other) const {
        return velocity == other.velocity && delayTicks == other.delayTicks;
    }
};

struct DrumLabLane {
    int instrument = 0;
    bool locked = false;
    int period = 16;
    int hits = 4;
    int rotation = 0;
};

struct DrumLabStepEdit {
    int lane = 0, step = 0;
    DrumLabStep before, after;
};

struct DrumLabState {
    int row = 0, col = 0, footerAction = 0;
    int genre = 0, activeLanes = 8, steps = 32;
    int density = 65, variation = 35, swing = 18, humanize = 12, fill = 35;
    uint32_t seed = 1;
    std::array<DrumLabLane, DRUMLAB_LANES> lanes = {{{0,false,16,4,0}, {1,false,8,2,4},
                                                    {2,false,8,5,0}, {3,false,12,3,1},
                                                    {4,false,7,2,2}, {5,false,11,3,1},
                                                    {6,false,16,1,14}, {7,false,16,1,0}}};
    std::array<std::array<DrumLabStep, DRUMLAB_MAX_STEPS>, DRUMLAB_LANES> grid{};
    bool generated = false, dirty = true, confirmApply = false, showHelp = false, aUsed = false;
    int generatedSteps = 0, gridPage = 0;
    bool gridFocused = false, gridAHeld = false, gridPreviewHeld = false, gridAudition = false;
    bool gridBUsed = false;
    uint8_t gridHistoryDirections = 0;
    int gridStep = 0;
    std::array<DrumLabStepEdit, DRUMLAB_STEP_HISTORY> stepHistory{};
    int stepHistoryCursor = 0, stepHistorySize = 0;
    std::string status = "Assign 5-8 loaded sample / synth slots, then Generate.";
    uint32_t applySerial = 0;
    int appliedSongRow = -1;

    // Runtime only. The engine's sample-frame counter is the preview clock.
    bool previewRunning = false;
    int previewTempo = 0, previewDisplayStep = -1;
    int64_t previewStartFrame = 0, previewNextStep = 0, previewFramesPerStep = 0;
    int64_t previewNextFrame = 0;
    int previewSwingRatio = 50;
    // Latest scheduled start of each displayed pattern row. No callback allocation or uniform-grid
    // approximation: project swing can move the offbeats, including during a running preview.
    std::array<int64_t, DRUMLAB_MAX_STEPS> previewStepStartFrames{};
};

const char* drumlab_genre_name(int genre);
inline constexpr int drumlab_row_count() { return DRUMLAB_ROWS; }
const char* drumlab_lane_name(int lane);
int drumlab_hit_count(const DrumLabState&);
// Same initial project scale / transpose as the generated phrase's note path.
songcore::NoteOnPayload drumlab_note_on(const songcore::Project&, int instrument, const DrumLabStep&);
bool drumlab_generate(DrumLabState&);
// Appends after the final occupied song row, claiming only empty, unreferenced pools.
// Rechecks all resources and assignments immediately before a no-fail commit.
bool drumlab_apply(DrumLabState&, songcore::SongcoreHost&);
bool drumlab_preview(DrumLabState&, songcore::SongcoreHost&, AudioEngine&);
void drumlab_tick(DrumLabState&, songcore::SongcoreHost&, AudioEngine&);
// Dismiss pending grid chords when another overlay takes input.
void drumlab_clear_grid_input(DrumLabState&);
void drumlab_release_grid_audition(DrumLabState&, songcore::SongcoreHost&);
// Call before transport start, changing tabs, closing Studio or replacing a project.
void drumlab_stop(DrumLabState&, songcore::SongcoreHost&, AudioEngine&);
// Host before-play hook: cancel owned lookahead without clearing MIDI start/follow state.
void drumlab_before_transport(DrumLabState&, AudioEngine&);
bool drumlab_button(const ButtonEvent&, DrumLabState&, songcore::SongcoreHost&, AudioEngine&);
void drumlab_draw(Canvas&, const DrumLabState&, const Theme&, const songcore::Project&);
} // namespace pt::ui
