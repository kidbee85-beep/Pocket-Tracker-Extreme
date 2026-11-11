// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/buttons.h"
#include <cstdint>
#include <string>
class AudioEngine;
namespace songcore { class SongcoreHost; struct Instrument; }
namespace pt::ui {
struct AppState;class Canvas;class FileSystem;
bool sample_granular_studio(const AppState&);
void sample_granular_open(AppState&,const songcore::Instrument&);
void sample_granular_select(AppState&,songcore::SongcoreHost&,bool activate);
void sample_granular_sync(AppState&);
bool sample_granular_poll(AppState&,songcore::SongcoreHost&,uint64_t nowMs);
bool sample_granular_audition_chord(const ButtonEvent&,AppState&,songcore::SongcoreHost&);
void sample_granular_stop_audition(AppState&,songcore::SongcoreHost&);
int sample_granular_page_count();
const char* sample_granular_page_name(const AppState&);
int sample_granular_control_count(const AppState&);
std::string sample_granular_control_label(const AppState&,int control);
std::string sample_granular_control_value(const AppState&,int control);
int sample_granular_control_level(const AppState&,int control);
void sample_granular_edit(AppState&,songcore::SongcoreHost&,int row,int direction,bool coarse);
void sample_granular_navigate(AppState&,Button direction);
bool sample_granular_wave_action(AppState&);
bool sample_granular_preset_picker_open(const AppState&);
bool sample_granular_open_preset_picker(AppState&);
void sample_granular_close_preset_picker(AppState&);
bool sample_granular_preset_picker_button(const ButtonEvent&,AppState&,songcore::SongcoreHost&);
int sample_granular_picker_count(const AppState&);
int sample_granular_picker_index(const AppState&,int item);
bool sample_granular_preset_is_favorite(const AppState&,int index);
bool sample_granular_action(AppState&,songcore::SongcoreHost&,AudioEngine&,FileSystem&);
bool sample_granular_load_source(AppState&,songcore::SongcoreHost&,const std::string& path);
bool sample_granular_load_preset(AppState&,songcore::SongcoreHost&,FileSystem&,const std::string& path,bool* sampleReplaced=nullptr);
void draw_sample_granular(Canvas&,const AppState&);
} // namespace pt::ui
