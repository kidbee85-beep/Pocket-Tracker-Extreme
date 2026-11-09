#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/button_mapper.h"
#include "ui/help_text.h"
#include "ui/input_dispatcher.h"
#include "ui/instrument_row_layout.h"
#include "ui/layout.h"
#include "ui/studio.h"
#include "ui/std_filesystem.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace pt::ui;
namespace {
struct Input {
    AppState& state;
    songcore::SongcoreHost& host;
    AudioEngine& engine;
    FileSystem& fs;
    InputDispatcher dispatch;
    MapperState mapper;
    uint64_t now=1000;
    Input(AppState& s,songcore::SongcoreHost& h,AudioEngine& e,FileSystem& f)
        :state(s),host(h),engine(e),fs(f),dispatch(s,h,f){}
    void event(Button button,ButtonAction action,ButtonMods mods={}) {
        now+=20;dispatch.set_now(static_cast<long long>(now));
        ButtonEvent e{button,action,mods};
        if(!studio_button(e,state,host,engine,fs))handle_button(e,dispatch,mapper,now);
    }
    void chord(bool selectFirst,bool releaseRFirst) {
        ButtonMods r;r.r=true;ButtonMods select;select.select=true;ButtonMods both=r;both.select=true;
        event(selectFirst?Button::SELECT:Button::R_SHIFT,ButtonAction::PRESSED,selectFirst?select:r);
        event(selectFirst?Button::R_SHIFT:Button::SELECT,ButtonAction::PRESSED,both);
        assert(state.studioOpen);
        event(releaseRFirst?Button::R_SHIFT:Button::SELECT,ButtonAction::RELEASED,releaseRFirst?select:r);
        event(releaseRFirst?Button::SELECT:Button::R_SHIFT,ButtonAction::RELEASED,{});
    }
};
void snapshot(const std::filesystem::path& path,AppState& state) {
    Canvas c;TrackerLayout layout;layout.draw(c,state);
    std::ofstream f(path,std::ios::binary);f<<"P6\n640 480\n255\n";
    for(int i=0;i<640*480;++i){const auto p=c.pixels()[i];const char rgb[]={char(p>>16),char(p>>8),char(p)};f.write(rgb,3);}
}
}
int main(int argc,char** argv) {
    assert(argc==2);const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    StdFileSystem fs((out/"app-data").string());
    auto engine=std::make_unique<AudioEngine>();songcore::SongcoreHost host(engine.get(),48000);
    auto project=songcore::make_default_project();project.tracks[0].chainRefs={0};project.chains[0].phraseRefs[0]=0;
    project.phrases[0].steps[0].note=songcore::Note::C4();project.phrases[0].steps[0].instrument=11;
    project.instruments[11].sampleFilePath=studio::custom_uri(82,120,34,202);
    assert(host.push_project(songcore::serialize_project(project)));
    assert(host.load_sample(11,*project.instruments[11].sampleFilePath));
    const auto unchanged=songcore::serialize_project(host.project());

    // Both chord orders and both release orders open the selected sound while transport keeps
    // running. Releasing SELECT must never toggle Studio help or fall through to tracker help.
    for(bool selectFirst:{false,true})for(bool releaseRFirst:{false,true}){
        AppState s;s.project=&host.edit_project();s.currentScreen=ScreenType::PHRASE;s.currentInstrument=11;
        s.settings.helpMode=1;Input input(s,host,*engine,fs);host.play_song(0);assert(host.is_playing());
        input.chord(selectFirst,releaseRFirst);
        assert(s.studioOpen&&!s.studioHelp&&!s.helpOpen&&!s.helpFull&&!s.studioShortcutRelease);
        assert(!input.mapper.selectPressedAlone&&s.studioModel==82&&s.studioHarmonics==120&&s.studioTimbre==34&&s.studioMorph==202);
        assert(host.is_playing()&&s.currentScreen==ScreenType::PHRASE);
        assert(songcore::serialize_project(host.project())==unchanged);host.stop();
    }
    for(auto screen:{ScreenType::SONG,ScreenType::CHAIN,ScreenType::PHRASE,ScreenType::INSTRUMENT,
                     ScreenType::TABLE,ScreenType::INST_POOL,ScreenType::MODS,ScreenType::GROOVE,
                     ScreenType::SCALE,ScreenType::MIXER,ScreenType::EFFECTS,ScreenType::PROJECT}){
        AppState s;s.project=&host.edit_project();s.currentScreen=screen;s.currentInstrument=11;
        Input input(s,host,*engine,fs);input.dispatch.on_select_r();assert(s.studioOpen&&s.studioModel==82);
    }

    // An actual button on INSTRUMENT makes Studio discoverable for all three instrument types.
    // Its row replaces an old spacer; sample/filter/preset row positions and their keys survive.
    for(auto type:{songcore::InstrumentType::SAMPLER,songcore::InstrumentType::SOUNDFONT,songcore::InstrumentType::EXTERNAL}){
        host.edit_project().instruments[11].instrumentType=type;
        AppState s;s.project=&host.edit_project();s.currentScreen=ScreenType::INSTRUMENT;s.currentInstrument=11;
        s.instrumentCursorRow=4;s.instrumentCursorColumn=1;
        assert(instrument_cell_at(type,4,1)==InstrumentCell::STUDIO);
        assert(detail::instrument_topic(type,4,1)==HelpTopic::INST_STUDIO);
        Input input(s,host,*engine,fs);
        ButtonMods a;a.a=true;input.event(Button::A,ButtonAction::PRESSED,a);assert(!s.studioOpen);
        input.event(Button::A,ButtonAction::RELEASED,{});assert(s.studioOpen&&!s.studioShortcutRelease);
    }
    host.edit_project().instruments[11].instrumentType=songcore::InstrumentType::SAMPLER;
    {
        AppState s;s.project=&host.edit_project();s.currentScreen=ScreenType::INSTRUMENT;s.currentInstrument=11;
        s.instrumentCursorRow=4;s.instrumentCursorColumn=1;Input input(s,host,*engine,fs);
        ButtonMods a;a.a=true;input.event(Button::A,ButtonAction::PRESSED,a);
        input.event(Button::DPAD_RIGHT,ButtonAction::PRESSED,a);input.event(Button::A,ButtonAction::RELEASED,{});
        assert(!s.studioOpen&&songcore::serialize_project(host.project())==unchanged);
        snapshot(out/"instrument-studio-entry.ppm",s);
    }

    // SELECT+R remains the file browser New Folder verb and opens its text editor, never Studio.
    {
        AppState s;s.project=&host.edit_project();s.currentScreen=ScreenType::FILE_BROWSER;
        s.fileBrowser.currentDirectory=fs.make_app_folder("Samples");Input input(s,host,*engine,fs);
        ButtonMods select;select.select=true;input.event(Button::SELECT,ButtonAction::PRESSED,select);
        select.r=true;input.event(Button::R_SHIFT,ButtonAction::PRESSED,select);
        assert(!s.studioOpen&&s.qwerty.isOpen);
    }
    // The chord cannot escape a dialog or an in-place editor and lose an unfinished edit.
    for(int modal=0;modal<6;++modal){
        AppState s;s.project=&host.edit_project();s.currentScreen=ScreenType::INSTRUMENT;s.currentInstrument=11;
        if(modal==0)s.confirm.open(ConfirmDialogState::Kind::CLEAN_SEQ);
        if(modal==1)s.qwerty.isOpen=true;
        if(modal==2)s.eq.isOpen=true;
        if(modal==3)s.themeEditor.isOpen=true;
        if(modal==4)s.renderDialog.isOpen=true;
        if(modal==5)s.mapPicker.isOpen=true;
        Input input(s,host,*engine,fs);input.dispatch.on_select_r();assert(!s.studioOpen);
    }
    for(auto screen:{ScreenType::SAMPLE_EDITOR,ScreenType::SETTINGS,ScreenType::MIDI,ScreenType::MIDI_MAP}){
        AppState s;s.project=&host.edit_project();s.currentScreen=screen;Input input(s,host,*engine,fs);
        input.dispatch.on_select_r();assert(!s.studioOpen);
    }
    // Bare SELECT still opens its original contextual help; the shortcut did not steal the key.
    {
        AppState s;s.project=&host.edit_project();s.currentScreen=ScreenType::SONG;s.settings.helpMode=1;
        Input input(s,host,*engine,fs);ButtonMods select;select.select=true;
        input.event(Button::SELECT,ButtonAction::PRESSED,select);input.event(Button::SELECT,ButtonAction::RELEASED,{});
        assert(!s.studioOpen&&(s.helpOpen||s.helpFull));
    }
    std::puts("PASS: direct Studio shortcut in both press/release orders, current-sound sync, transport/document preservation, instrument entry, modal guards and legacy help/browser chords");
}
