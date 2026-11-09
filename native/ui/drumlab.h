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
};

struct DrumLabState {
    int row = 0, col = 0, footerAction = 0;
    int genre = 0, activeLanes = 8, steps = 32;
    int density = 65, variation = 35, swing = 18, humanize = 12, fill = 35;
    uint32_t seed = 1;
    std::array<DrumLabLane, DRUMLAB_LANES> lanes = {{{0,false},{1,false},{2,false},{3,false},
                                                    {4,false},{5,false},{6,false},{7,false}}};
    std::array<std::array<DrumLabStep, DRUMLAB_MAX_STEPS>, DRUMLAB_LANES> grid{};
    bool generated = false, dirty = true, confirmApply = false, showHelp = false, aUsed = false;
    int generatedSteps = 0, gridPage = 0;
    std::string status = "Assign 5-8 loaded sample / synth slots, then Generate.";
    uint32_t applySerial = 0;
    int appliedSongRow = -1;

    // Runtime only. The engine's sample-frame counter is the preview clock.
    bool previewRunning = false;
    int previewTempo = 0, previewDisplayStep = -1;
    int64_t previewStartFrame = 0, previewNextStep = 0, previewFramesPerStep = 0;
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
// Call before transport start, changing tabs, closing Studio or replacing a project.
void drumlab_stop(DrumLabState&, songcore::SongcoreHost&, AudioEngine&);
// Host before-play hook: cancel owned lookahead without clearing MIDI start/follow state.
void drumlab_before_transport(DrumLabState&, AudioEngine&);
bool drumlab_button(const ButtonEvent&, DrumLabState&, songcore::SongcoreHost&, AudioEngine&);
void drumlab_draw(Canvas&, const DrumLabState&, const Theme&, const songcore::Project&);
} // namespace pt::ui
