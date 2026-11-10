#pragma once
#include "ui/app_state.h"
#include "ui/canvas.h"
#include "ui/filesystem.h"
#include "songcore/host.h"
#include "audio-engine.h"
namespace pt::ui {
void live_resample_toggle(AppState&,songcore::SongcoreHost&,AudioEngine&,FileSystem&);
void live_resample_stop(AppState&,AudioEngine&,FileSystem&);
bool live_resample_poll(AppState&,AudioEngine&,FileSystem&);
void live_resample_use(AppState&,songcore::SongcoreHost&,AudioEngine&,FileSystem&);
void live_resample_cycle_length(AppState&);
void draw_live_resample_strip(Canvas&,const AppState&,int x,int y,int w,int h);
void draw_live_resample_indicator(Canvas&,const AppState&);
}
