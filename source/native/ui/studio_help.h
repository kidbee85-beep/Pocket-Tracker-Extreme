#pragma once
#include "ui/app_state.h"
#include "ui/buttons.h"
#include "ui/canvas.h"
#include <string>
#include <vector>

class AudioEngine;
namespace songcore { class SongcoreHost; }
namespace pt::ui {
struct StudioHelpContent { std::string title; std::vector<std::string> lines; };
inline constexpr int STUDIO_HELP_TOPICS=8, STUDIO_HELP_PAGES=3;
inline constexpr int STUDIO_HELP_QUICK=-1;
StudioHelpContent studio_help_quick_content(const AppState&);
StudioHelpContent studio_help_content(const AppState&,int topic,int page);
std::vector<std::string> studio_help_wrapped_lines(Canvas&,const StudioHelpContent&);
bool studio_help_captures(const ButtonEvent&,const AppState&);
bool studio_help_button(const ButtonEvent&,AppState&,songcore::SongcoreHost&,AudioEngine&);
void draw_studio_help(Canvas&,const AppState&);
}
