#include "ui/source_sampler.h"
#include "ui/canvas.h"
#include "ui/theme.h"
#include "ui/studio.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "audio-engine.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace pt::ui;
using songcore::json;
namespace {
struct FakeBackend : SourceBackend {
    json state;
    std::string raw;
    int reads=0;
    bool accepted=true,throws=false;
    bool asciiArguments=true;
    std::vector<std::pair<std::string,json>> commands;
    bool command(const std::string& action,const std::string& args) override {
        if(throws)throw std::runtime_error("fixture failure");
        for(unsigned char c:args)if(c>=128)asciiArguments=false;
        commands.emplace_back(action,json::parse(args));return accepted;
    }
    std::string state_json() override {++reads;return raw.empty()?state.dump():raw;}
};

json snapshot() {
    return {{"status","ready"},{"provider","archive"},{"genre","jazz"},{"query","Jazz test"},
        {"providers",json::array({{{"id","archive"},{"available",true}},{{"id","commons"},{"available",true}},{{"id","youtube"},{"available",false}}})},
        {"results",json::array({{{"id","album-a"},{"title","Archive test album A"},{"creator","Test creator"},{"kind","album"},{"url","https://archive.org/details/test-a"},{"license","https://creativecommons.org/licenses/by/4.0/"},
                                    {"description","Historical jazz quartet, as described by the archive."},{"tags",json::array({"jazz","quartet"})},{"instruments",json::array({"piano","double bass"})}},
                                 {{"id","album-b"},{"title","Archive test album B"},{"creator","Another creator"},{"kind","album"},{"url","https://archive.org/details/test-b"}}})},
        {"tracks",json::array({{{"id","track-a"},{"title","Test track one"},{"creator","Test creator"},{"url","https://archive.org/details/test-a"},{"duration_ms",120000}}})},
        {"selected_index",0},{"track_index",0},{"title","Test track one"},{"creator","Test creator"},
        {"source_url","https://archive.org/details/test-a"},{"license_url","https://creativecommons.org/licenses/by/4.0/"},
        {"playing",false},{"recording",false},{"rendering",false},{"busy",false},
        {"position_ms",2500},{"duration_ms",120000},{"in_ms",1000},{"out_ms",4000},
        {"clip_ready",false},{"clip_path",""},{"clip_frames",0},{"clip_rate",0},{"clip_channels",0}};
}
void release(SourceSamplerState& state,songcore::SongcoreHost& host,AudioEngine& engine) {
    source_sampler_button({Button::A,ButtonAction::PRESSED,{}},state,host,engine);
    source_sampler_button({Button::A,ButtonAction::RELEASED,{}},state,host,engine);
}
void edit(SourceSamplerState& state,songcore::SongcoreHost& host,AudioEngine& engine,Button direction) {
    source_sampler_button({Button::A,ButtonAction::PRESSED,{}},state,host,engine);
    source_sampler_button({direction,ButtonAction::PRESSED,{true}},state,host,engine);
    source_sampler_button({Button::A,ButtonAction::RELEASED,{}},state,host,engine);
}
void shot(const std::filesystem::path& path,const SourceSamplerState& state,const songcore::Project& p) {
    Canvas canvas;auto project=p;AppState app;app.project=&project;app.theme=theme_studio();
    app.studioOpen=true;app.studioTab=7;app.sourceSampler=state;draw_studio(canvas,app);
    std::ofstream output(path,std::ios::binary);output<<"P6\n640 480\n255\n";
    for(int i=0;i<640*480;++i){auto pixel=canvas.pixels()[i];char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};output.write(rgb,3);}
}
}

int main(int argc,char**argv) {
    assert(argc==2);const std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);
    auto engine=std::make_unique<AudioEngine>();songcore::SongcoreHost host(nullptr,48000);
    auto project=songcore::make_default_project();project.tracks[0].chainRefs={0};project.chains[0].phraseRefs[0]=0;
    project.phrases[0].steps[0].note=songcore::Note::C4();assert(host.push_project(songcore::serialize_project(project)));
    const auto original=songcore::serialize_project(host.project());
    SourceSamplerState disconnected;disconnected.row=5;host.play_song(0);release(disconnected,host,*engine);
    assert(host.is_playing());host.stop(); // unavailable playback cannot stop an unrelated song
    FakeBackend backend;backend.state=snapshot();SourceSamplerState state;state.backend=&backend;
    assert(source_sampler_poll(state,0)&&backend.reads==1);assert(!source_sampler_poll(state,50)&&backend.reads==1);
    assert(!source_sampler_poll(state,100)&&backend.reads==2);
    assert(state.results.size()==2&&state.tracks.size()==1&&state.selectedResult==0&&state.durationMs==120000);
    assert(state.providerAvailable[0]&&state.providerAvailable[1]&&!state.providerAvailable[2]);
    assert(state.results[0].description.find("Historical jazz")!=std::string::npos&&state.results[0].instruments=="piano / double bass");
    assert(state.results[1].instruments.empty()); // Never invent missing instrument metadata.
    state.row=0;edit(state,host,*engine,Button::DPAD_RIGHT);assert(state.provider==1);
    edit(state,host,*engine,Button::DPAD_RIGHT);assert(state.provider==0); // skips unavailable YouTube
    state.row=3;const auto beforeBrowse=backend.commands.size();const auto markerIn=state.inMs,markerOut=state.outMs;
    source_sampler_button({Button::DPAD_RIGHT,ButtonAction::PRESSED,{}},state,host,*engine);
    assert(state.listFocused&&state.selectedResult==0);
    source_sampler_button({Button::DPAD_DOWN,ButtonAction::PRESSED,{}},state,host,*engine);
    assert(state.row==3&&state.selectedResult==1&&backend.commands.size()==beforeBrowse&&state.inMs==markerIn&&state.outMs==markerOut);
    source_sampler_button({Button::DPAD_UP,ButtonAction::PRESSED,{true}},state,host,*engine);
    source_sampler_button({Button::A,ButtonAction::RELEASED,{}},state,host,*engine);
    assert(state.selectedResult==0&&backend.commands.size()==beforeBrowse); // Holding A cannot turn vertical browsing into edit-axis movement or launch playback.
    source_sampler_button({Button::DPAD_DOWN,ButtonAction::PRESSED,{}},state,host,*engine);
    source_sampler_button({Button::DPAD_LEFT,ButtonAction::PRESSED,{}},state,host,*engine);
    assert(!state.listFocused&&state.selectedResult==1); // Horizontal keys change panes, not vertical song choice.
    backend.state["position_ms"]=2600;assert(source_sampler_poll(state,200)&&state.selectedResult==1); // live clock cannot steal list selection
    backend.state["selected_index"]=1;state.selectedResult=0;assert(source_sampler_poll(state,300)&&state.selectedResult==0&&state.openedResult==1);
    state.selectedResult=1;state.row=2;state.query="Funk \"cuts\" / ÄÖÜ 🎺";state.genre=4;host.play_song(0);release(state,host,*engine);
    assert(backend.commands.back().first=="query_dialog"&&backend.commands.back().second["query"]==state.query&&host.is_playing());
    assert(backend.commands.back().second["provider"]=="archive"&&backend.commands.back().second["genre"]==""&&backend.asciiArguments);
    state.row=3;engine->studioRack.punch.store(6);release(state,host,*engine);
    assert(!host.is_playing()&&engine->studioRack.punch.load()==0&&backend.commands.back().first=="open");
    assert(backend.commands.back().second["index"]==1&&state.busy);
    const auto commandsWhileBusy=backend.commands.size();release(state,host,*engine);assert(backend.commands.size()==commandsWhileBusy);
    state.row=5;state.col=1;release(state,host,*engine);assert(backend.commands.back().first=="stop");
    assert(source_sampler_poll(state,201));assert(!state.pendingTrackList); // Stop cancels the pending focus transfer.
    state.row=3;release(state,host,*engine);assert(state.pendingTrackList);
    assert(source_sampler_poll(state,202));assert(state.row==4&&state.listFocused);
    state.row=4;release(state,host,*engine);assert(backend.commands.back().first=="listen");
    source_sampler_poll(state,202);state.row=5;state.col=0;host.play_song(0);release(state,host,*engine);
    assert(!host.is_playing()&&backend.commands.back().first=="play");
    state.playing=true;release(state,host,*engine);assert(backend.commands.back().first=="pause");state.playing=false;
    state.row=7;const auto commandsBeforeMarker=backend.commands.size();edit(state,host,*engine,Button::DPAD_RIGHT);
    assert(backend.commands.size()==commandsBeforeMarker+1&&backend.commands.back().first=="set_region");
    assert(backend.commands.back().second["in_ms"]==1010&&backend.commands.back().second["out_ms"]==4000);
    release(state,host,*engine);assert(backend.commands.back().first=="mark_in"&&backend.commands.back().second.empty());
    state.row=8;release(state,host,*engine);assert(backend.commands.back().first=="mark_out");
    state.row=6;state.positionMs=119500;edit(state,host,*engine,Button::DPAD_UP);
    assert(backend.commands.back().first=="seek"&&backend.commands.back().second["position_ms"]==120000);
    state.row=9;state.recording=true;state.busy=true;release(state,host,*engine);
    assert(backend.commands.back().first=="record"&&backend.commands.back().second["enabled"]==false);
    state.recording=false;state.busy=false;state.rendering=false;release(state,host,*engine);assert(backend.commands.back().second["enabled"]==true);
    state.recording=false;
    state.row=10;state.inMs=4000;state.outMs=1000;const auto beforeBadRender=backend.commands.size();release(state,host,*engine);
    assert(backend.commands.size()==beforeBadRender);
    state.durationMs=600000;state.inMs=0;state.outMs=180001;release(state,host,*engine);assert(backend.commands.size()==beforeBadRender);
    state.inMs=1000;state.outMs=4000;release(state,host,*engine);
    assert(backend.commands.back().first=="render"&&state.busy&&state.rendering&&!state.clipReady);
    state.row=12;release(state,host,*engine);assert(!state.importRequested);
    backend.state["status"]="clip_ready";backend.state["clip_ready"]=true;backend.state["clip_path"]="/private/app/clip.wav";
    backend.state["clip_frames"]=144000;backend.state["clip_rate"]=48000;backend.state["clip_channels"]=2;
    assert(source_sampler_poll(state,400)&&state.clipReady);host.play_song(0);engine->studioRack.punch.store(5);release(state,host,*engine);
    assert(state.importRequested&&state.importPath=="/private/app/clip.wav"&&!host.is_playing()&&engine->studioRack.punch.load()==0&&songcore::serialize_project(host.project())==original);
    state.importRequested=false;state.row=11;state.targetInstrument=-1;edit(state,host,*engine,Button::DPAD_RIGHT);assert(state.targetInstrument==0);
    state.row=3;release(state,host,*engine);assert(!state.clipReady);
    assert(source_sampler_poll(state,401)&&!state.clipReady); // an unchanged, stale backend snapshot cannot revive the invalidated clip
    state.row=12;release(state,host,*engine);assert(!state.importRequested);
    backend.state["clip_path"]="/private/app/clip-next.wav";assert(source_sampler_poll(state,501)&&state.clipReady);
    state.row=13;state.col=1;release(state,host,*engine);
    assert(backend.commands.back().first=="info"&&backend.commands.back().second["url"]==state.licenseUrl);
    state.sourceUrl="javascript:alert(1)";state.col=0;const auto beforeBadLink=backend.commands.size();release(state,host,*engine);assert(backend.commands.size()==beforeBadLink);
    backend.accepted=false;state.row=2;release(state,host,*engine);assert(state.status.find("refused")!=std::string::npos);backend.accepted=true;
    backend.throws=true;release(state,host,*engine);assert(state.status.find("respond")!=std::string::npos);backend.throws=false;
    backend.raw="{broken";assert(source_sampler_poll(state,601)&&state.status.find("unreadable")!=std::string::npos);
    assert(state.results.size()==2);backend.raw.clear();
    backend.state["clip_frames"]=48000*181;assert(source_sampler_poll(state,701)&&!state.clipReady);
    state.row=12;release(state,host,*engine);assert(!state.importRequested);
    backend.state["clip_frames"]=144000;backend.state["position_ms"]=-20;backend.state["out_ms"]=999999999999LL;
    assert(source_sampler_poll(state,801)&&state.positionMs==0&&state.outMs==state.durationMs);
    state.row=5;const auto beforeStart=backend.commands.size();
    assert(!source_sampler_button({Button::START,ButtonAction::PRESSED,{}},state,host,*engine));assert(backend.commands.size()==beforeStart+1&&backend.commands.back().first=="stop");
    source_sampler_button({Button::SELECT,ButtonAction::RELEASED,{}},state,host,*engine);assert(state.showHelp);
    assert(source_sampler_button({Button::B,ButtonAction::RELEASED,{}},state,host,*engine)&&!state.showHelp);
    assert(!source_sampler_button({Button::B,ButtonAction::RELEASED,{}},state,host,*engine));assert(backend.commands.back().first=="stop");
    const int readsBeforeDraw=backend.reads;
    Canvas canvas;canvas.clear(0xFF123456);source_sampler_draw(canvas,state,theme_studio(),host.project());
    assert(backend.reads==readsBeforeDraw);
    for(int y=0;y<104;++y)for(int x=0;x<640;++x)assert(canvas.pixels()[y*640+x]==0xFF123456);
    for(int y=476;y<480;++y)for(int x=0;x<640;++x)assert(canvas.pixels()[y*640+x]==0xFF123456);
    backend.state=snapshot();backend.state["message"]="Fixture preview: source, markers and license remain visible.";state.hasPolled=false;source_sampler_poll(state,700);
    state.row=3;shot(out/"source-browser.ppm",state,host.project());state.row=8;shot(out/"source-region.ppm",state,host.project());
    state.showHelp=true;shot(out/"source-help.ppm",state,host.project());
    assert(songcore::serialize_project(host.project())==original);
    // Browser additions leave playback positions, IN/OUT and the project untouched until a
    // source is explicitly chosen; the direct seek keeps the existing marker pair.
    FakeBackend browser;browser.state=snapshot();SourceSamplerState browse;browse.backend=&browser;
    source_sampler_poll(browse,0);browse.row=2;browse.col=1;
    release(browse,host,*engine);assert(browser.commands.back().first=="discover"&&browser.commands.back().second["genre"]=="jazz");
    assert(browse.busy);browser.state["message"]="Real metadata discoveries";source_sampler_poll(browse,1);
    browse.row=3;browse.listFocused=true;const auto beforeCancel=browser.commands.size();
    assert(source_sampler_button({Button::B,ButtonAction::PRESSED,{}},browse,host,*engine));
    assert(source_sampler_button({Button::B,ButtonAction::RELEASED,{}},browse,host,*engine));
    assert(!browse.listFocused&&browser.commands.size()==beforeCancel);
    browse.row=4;const auto beforeSortIn=browse.inMs,beforeSortOut=browse.outMs;
    source_sampler_button({Button::L_SHIFT,ButtonAction::RELEASED,{}},browse,host,*engine);
    assert(browser.commands.back().first=="sort"&&browser.commands.back().second["order"]=="title");
    assert(browse.inMs==beforeSortIn&&browse.outMs==beforeSortOut);
    ButtonMods scrub;scrub.r=true;
    source_sampler_button({Button::DPAD_RIGHT,ButtonAction::PRESSED,scrub},browse,host,*engine);
    assert(browser.commands.back().first=="seek"&&browser.commands.back().second["position_ms"]==7500);
    assert(browse.inMs==beforeSortIn&&browse.outMs==beforeSortOut);
    browse.row=3;browse.listFocused=true;
    shot(out/"source-browser-focus.ppm",browse,host.project());
    browse.row=2;browse.listFocused=false;browse.col=1;shot(out/"source-random.ppm",browse,host.project());
    std::ofstream(out/"source-sampler-validation.json")<<json({{"backend_reads",backend.reads},{"commands_checked",backend.commands.size()},
        {"cached_draw_only",true},{"project_unchanged",true},{"selection_marker_and_import_guards",true},{"native_text_dialog_contract",true},{"network_used",false}}).dump(2)<<'\n';
    std::cout<<"PASS: cached backend contract, provider availability, list focus, gamepad release, markers/render/import guards, source links, lifecycle stop, project preservation\n";
}
