#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/studio.h"
#include "ui/studio_help.h"
#include "ui/std_filesystem.h"
#include "studio/synth.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace pt::ui;
namespace {
bool contains(const StudioHelpContent& c,const std::string& text) {
    for(const auto& line:c.lines)if(line.find(text)!=std::string::npos)return true;
    return false;
}
void image(const std::filesystem::path& path,const AppState& s) {
    Canvas c;draw_studio(c,s);std::ofstream f(path,std::ios::binary);f<<"P6\n640 480\n255\n";
    for(int i=0;i<640*480;++i){const auto p=c.pixels()[i];char rgb[]={char(p>>16),char(p>>8),char(p)};f.write(rgb,3);}
}
}
int main(int argc,char**argv) {
    assert(argc==2);const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
    StdFileSystem fs((out/"app-data").string());auto engine=std::make_unique<AudioEngine>();
    engine->setDeviceSampleRate(48000);songcore::SongcoreHost host(engine.get(),48000);
    assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
    AppState s;s.project=&host.edit_project();s.currentInstrument=4;s.studioOpen=true;
    const auto before=songcore::serialize_project(host.project());
    auto event=[&](Button b,ButtonAction a,ButtonMods mods=ButtonMods{}){return studio_button({b,a,mods},s,host,*engine,fs);};
    auto tap=[&](Button b){assert(event(b,ButtonAction::PRESSED));assert(event(b,ButtonAction::RELEASED));};
    Canvas metrics;size_t maxRows=0;int checked=0;
    for(int topic=0;topic<8;++topic) {
        s.studioTab=topic;s.studioRow=0;s.studioHelp=false;s.studioHelpHeldButtons=0;
        tap(Button::SELECT);assert(s.studioHelp&&s.studioHelpTopic==topic&&s.studioHelpPage==0);
        for(int page=0;page<3;++page){
            const auto c=studio_help_content(s,topic,page);assert(!c.title.empty()&&!c.lines.empty());
            const auto lines=studio_help_wrapped_lines(metrics,c);maxRows=std::max(maxRows,lines.size());
            if(lines.size()>10)std::fprintf(stderr,"help overflow topic %d page %d: %zu rows\n",topic,page,lines.size());
            assert(lines.size()<=10);for(const auto& line:lines)assert(metrics.label_width(line,14)<=560);
            s.studioHelpPage=page;image(out/("help-"+std::to_string(topic)+"-"+std::to_string(page)+".ppm"),s);++checked;
        }
        s.studioHelpPage=0;tap(Button::DPAD_RIGHT);assert(s.studioHelpPage==1);
        tap(Button::DPAD_LEFT);assert(s.studioHelpPage==0);
        tap(Button::DPAD_LEFT);assert(s.studioHelpPage==2);
        tap(Button::DPAD_DOWN);assert(s.studioHelpTopic==(topic+1)%8&&s.studioHelpPage==0);
        tap(Button::DPAD_UP);assert(s.studioHelpTopic==topic);
        assert(s.studioTab==topic&&s.studioRow==0);
        tap(Button::START);assert(!host.is_playing());  // Modal help never starts hidden playback.
        tap(Button::B);assert(!s.studioHelp&&s.studioOpen);
        assert(songcore::serialize_project(host.project())==before);
    }
    // The model, page and labels are taken from the real engine descriptors, not a stale table.
    s.studioTab=0;
    for(int model=0;model<studio::model_count();++model){
        s.studioModel=model;
        for(int page=0;page<studio_synth_page_count(s);++page){
            s.studioSynthPage=page;s.studioRow=3;
            const auto c=studio_help_content(s,0,2);
            assert(contains(c,studio::model_name(model))&&contains(c,studio_synth_page_name(s)));
            for(int i=0;i<studio_synth_control_count(s);++i)assert(contains(c,studio_synth_control_label(s,i)+" = "+studio_synth_control_value(s,i)));
            const auto lines=studio_help_wrapped_lines(metrics,c);maxRows=std::max(maxRows,lines.size());
            assert(lines.size()<=10);for(const auto& line:lines)assert(metrics.label_width(line,14)<=560);
            ++checked;
        }
    }
    // A pressed below help then released after closing must not apply a draft or a preset.
    s.studioModel=0;s.studioSynthPage=0;s.studioTab=0;s.studioRow=studio_synth_apply_row(s);
    s.studioLiveBound=false;tap(Button::SELECT);assert(s.studioHelp);
    assert(event(Button::A,ButtonAction::PRESSED));tap(Button::SELECT);assert(!s.studioHelp);
    assert(event(Button::A,ButtonAction::RELEASED));assert(!s.studioConfirm);
    assert(songcore::serialize_project(host.project())==before);
    // A already held when SELECT is pressed is protected even if A is released first.
    ButtonMods a;a.a=true;assert(event(Button::SELECT,ButtonAction::PRESSED,a));
    assert(event(Button::A,ButtonAction::RELEASED));assert(event(Button::SELECT,ButtonAction::RELEASED));
    assert(s.studioHelp&&!s.studioConfirm);tap(Button::B);
    // Shoulder releases used while reading cannot switch a Drumlab bar after help closes.
    s.studioTab=5;const auto bar=s.drumLab.gridPage;tap(Button::SELECT);
    assert(event(Button::R_SHIFT,ButtonAction::PRESSED));tap(Button::B);
    assert(event(Button::R_SHIFT,ButtonAction::RELEASED));assert(s.drumLab.gridPage==bar);
    // Punch stays momentary, and opening help releases a previously held gesture.
    s.studioTab=3;s.studioPunch=9;assert(event(Button::A,ButtonAction::PRESSED,a));
    assert(engine->studioRack.punch.load()==9);assert(event(Button::SELECT,ButtonAction::PRESSED,a));
    assert(engine->studioRack.punch.load()==0);assert(event(Button::SELECT,ButtonAction::RELEASED,a));
    assert(s.studioHelp);assert(event(Button::A,ButtonAction::RELEASED));tap(Button::B);
    assert(event(Button::A,ButtonAction::PRESSED,a));assert(engine->studioRack.punch.load()==9);
    assert(event(Button::A,ButtonAction::RELEASED));assert(engine->studioRack.punch.load()==0);
    // A help-owned release is also consumed if an external route closed Studio meanwhile.
    tap(Button::SELECT);assert(event(Button::A,ButtonAction::PRESSED));s.studioHelp=false;s.studioOpen=false;
    assert(event(Button::A,ButtonAction::RELEASED));assert(songcore::serialize_project(host.project())==before);
    const auto punch=studio_help_content(s,3,1);assert(contains(punch,"L + R + Start")&&contains(punch,"B + Down")&&contains(punch,"B + Up"));
    const auto digging=studio_help_content(s,7,1);assert(contains(digging,"10 ms")&&contains(digging,"5 s"));
    std::printf("PASS: %d contextual pages / engine-page views, max %zu wrapped rows, all eight topics, pager/modal controls, descriptor accuracy, release swallowing, Punch restoration and unchanged project\n",checked,maxRows);
}
