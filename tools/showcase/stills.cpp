// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio-engine.h"
#include "songcore/host.h"
#include "ui/layout.h"
#include "ui/input_dispatcher.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"
#include <filesystem>
#include <fstream>
#include <memory>
using namespace pt::ui;
namespace fs=std::filesystem;
int main(int argc,char**argv){
 if(argc!=4)return 1;const fs::path project=fs::absolute(argv[1]),data=fs::absolute(argv[2]),out=fs::absolute(argv[3]);fs::create_directories(out);fs::current_path(data);
 std::ifstream f(project);std::string blob{std::istreambuf_iterator<char>(f),{}};
 auto e=std::make_unique<AudioEngine>();songcore::SongcoreHost host(e.get(),48000);if(!host.push_project(blob))return 2;host.set_app_root(data.string());if(host.load_media(project.parent_path().string()).failed)return 3;host.push_params();
 AppState s;s.project=&host.edit_project();s.theme=theme_by_name("ULTRAVIOLET",VisualizerType::OCTA);s.caps=PlatformCaps::sdl(false);StdFileSystem files(".");InputDispatcher dispatch(s,host,files);TrackerLayout l;Canvas c;
 auto shot=[&](const char*name){l.draw(c,s);std::ofstream p(out/(std::string(name)+".ppm"),std::ios::binary);p<<"P6\n640 480\n255\n";for(int i=0;i<640*480;++i){auto q=c.pixels()[i];char b[]={char(q>>16),char(q>>8),char(q)};p.write(b,3);}};
 s.studioOpen=true;s.studioTab=6;s.studioRow=1;shot("style");
 s.studioOpen=false;s.splashProgress=.56;shot("splash");s.splashProgress=-1;
 s.currentScreen=ScreenType::PROJECT;shot("project");
 s.currentScreen=ScreenType::GROOVE;shot("groove");
 s.currentScreen=ScreenType::INSTRUMENT;s.currentInstrument=17;s.instrumentCursorRow=0;s.instrumentCursorColumn=2;dispatch.on_button_a();if(s.currentScreen!=ScreenType::FILE_BROWSER)return 4;shot("instrument-load");
}
