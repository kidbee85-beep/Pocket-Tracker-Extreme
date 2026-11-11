#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/button_mapper.h"
#include "ui/input_dispatcher.h"
#include "ui/source_sampler.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using namespace pt::ui;
struct QueryBackend final : SourceBackend {
    int searches=0;
    songcore::json args;
    bool command(const std::string& action,const std::string& raw) override {
        if(action=="search"){++searches;args=songcore::json::parse(raw);}return true;
    }
    std::string state_json() override {return "{}";}
};
int main(int argc,char** argv) {
    assert(argc==2);std::filesystem::create_directories(argv[1]);
    auto engine=std::make_unique<AudioEngine>();songcore::SongcoreHost host(engine.get(),48000);
    StdFileSystem fs(argv[1]);AppState s;s.project=&host.edit_project();
    s.studioOpen=true;s.studioTab=7;s.splashProgress=-1;
    QueryBackend backend;s.sourceSampler.backend=&backend;s.sourceSampler.provider=1;
    s.sourceSampler.genre=0;s.sourceSampler.query="old query";
    s.sourceSampler.inMs=1234;s.sourceSampler.outMs=9876;
    InputDispatcher dispatcher(s,host,fs);MapperState mapper;
    dispatcher.open_source_search();
    assert(s.qwerty.isOpen&&s.qwerty.text=="old query");
    s.qwerty.text="jazz guitar";
    const int beforeTab=s.studioTab;
    const ButtonEvent press{Button::START,ButtonAction::PRESSED,{}};
    assert(!studio_button(press,s,host,*engine,fs));
    handle_button(press,dispatcher,mapper,1000);
    handle_button({Button::START,ButtonAction::RELEASED,{}},dispatcher,mapper,1010);
    assert(!s.qwerty.isOpen&&backend.searches==1);
    assert(backend.args.at("query")=="jazz guitar");
    assert(backend.args.at("provider")=="commons");
    assert(backend.args.at("genre")=="jazz");
    assert(!host.is_playing()&&s.studioOpen&&s.studioTab==beforeTab);
    assert(s.sourceSampler.inMs==1234&&s.sourceSampler.outMs==9876);
    s.sourceSampler.busy=false;dispatcher.open_source_search();
    s.qwerty.text="cancel this";
    ButtonMods select;select.select=true;
    handle_button({Button::SELECT,ButtonAction::PRESSED,select},dispatcher,mapper,1020);
    handle_button({Button::SELECT,ButtonAction::RELEASED,{}},dispatcher,mapper,1030);
    assert(s.helpOpen&&s.qwerty.isOpen&&s.qwerty.text=="cancel this"&&backend.searches==1);
    ButtonMods back;back.b=true;
    handle_button({Button::B,ButtonAction::PRESSED,back},dispatcher,mapper,1040);
    handle_button({Button::B,ButtonAction::RELEASED,{}},dispatcher,mapper,1050);
    assert(!s.helpOpen&&s.qwerty.isOpen);
    handle_button({Button::SELECT,ButtonAction::PRESSED,select},dispatcher,mapper,1060);
    back.select=true;
    handle_button({Button::B,ButtonAction::PRESSED,back},dispatcher,mapper,1070);
    handle_button({Button::B,ButtonAction::RELEASED,select},dispatcher,mapper,1080);
    handle_button({Button::SELECT,ButtonAction::RELEASED,{}},dispatcher,mapper,1090);
    assert(!s.qwerty.isOpen&&backend.searches==1);
    assert(s.sourceSampler.query=="jazz guitar");
    std::puts("PASS native Digging keyboard: search submits provider/genre, Start stays modal, cancel and IN/OUT preserved.");
}
