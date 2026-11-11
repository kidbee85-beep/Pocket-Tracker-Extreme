#pragma once
#include "ui/app_state.h"
#include "ui/canvas.h"
#include "ui/buttons.h"
#include "songcore/host.h"
#include "audio-engine.h"
#include "ui/sample_granular_ui.h"
namespace pt::ui {
void draw_studio(Canvas&,const AppState&);
void open_studio_appearance(AppState&);
bool studio_poll_capture(AppState&,uint64_t now);
void studio_capture_refresh(AppState&,bool scanExternal=true);
void open_studio_capture(AppState&);
void studio_capture_edit(AppState&,int direction);
void studio_capture_select_internal(AppState&);
std::string studio_capture_status(const CaptureView&);
void studio_capture_action(AppState&,songcore::SongcoreHost&,AudioEngine&,FileSystem&);
bool studio_import_take(AppState&,songcore::SongcoreHost&,AudioEngine&,FileSystem&,
                        const std::string& sourcePath,std::string& importedPath,
                        const std::string& name,const std::string& folder);
std::string studio_value(const AppState&,int row);
std::string studio_label(const AppState&,int row);
int studio_rows(int tab);
void open_studio_synth(AppState&,const songcore::Instrument&);
bool studio_synth_live(const AppState&);
int studio_synth_page_count(const AppState&);
std::string studio_synth_page_name(const AppState&);
int studio_synth_control_count(const AppState&);
int studio_synth_apply_row(const AppState&);
std::string studio_synth_control_label(const AppState&,int index);
std::string studio_synth_control_value(const AppState&,int index);
int studio_synth_control_level(const AppState&,int index);
std::string studio_synth_note_name(const AppState&);
void studio_synth_sync_target(AppState&);
void studio_synth_edit(AppState&,songcore::SongcoreHost&,AudioEngine&,int row,int direction,bool coarse);
bool studio_synth_apply(AppState&,songcore::SongcoreHost&,AudioEngine&);
bool studio_synth_compare(AppState&,songcore::SongcoreHost&,AudioEngine&);
void studio_synth_audition(AppState&,songcore::SongcoreHost&,bool on);
void studio_synth_stop_audition(AppState&,songcore::SongcoreHost&);
void studio_synth_navigate(AppState&,Button direction);
bool studio_button(const ButtonEvent&,AppState&,songcore::SongcoreHost&,AudioEngine&,FileSystem&);
}
