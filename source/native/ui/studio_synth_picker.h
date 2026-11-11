#pragma once

#include "ui/buttons.h"

#include <array>
#include <string>

class AudioEngine;
namespace songcore { class SongcoreHost; }

namespace pt::ui {

struct AppState;

enum class StudioSynthPickerMode { Closed, Model, Preset };

struct StudioSynthPickerState {
    StudioSynthPickerMode mode = StudioSynthPickerMode::Closed;
    int instrument = -1;
    int group = 0;
    int item = 0;
    bool chooseArmed = false;
    bool previewHeld = false;
    bool previewSounding = false;
};

struct StudioSynthModelGroup {
    const char* name;
    int firstModel;
    int count;
};

// Sample Granular is a UI choice outside the stable oscillator ID range.
inline constexpr int kSampleGranularModel = -1;
const std::array<StudioSynthModelGroup, 8>& studio_synth_model_groups();
int studio_synth_model_group_count();
int studio_synth_model_group(int model);
std::string studio_synth_preset_label(int model, int variation);

bool studio_synth_picker_open(const AppState&);
bool studio_synth_open_picker_for_row(AppState&, songcore::SongcoreHost&);
void studio_synth_close_picker(AppState&);
bool studio_synth_picker_button(const ButtonEvent&, AppState&, songcore::SongcoreHost&, AudioEngine&);

} // namespace pt::ui
