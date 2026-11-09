#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/input_dispatcher.h"
#include "ui/source_sampler.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace pt::ui;
namespace {
struct FakeSource final : SourceBackend {
    std::string snapshot="{}";
    std::vector<std::string> actions;
    bool command(const std::string& action,const std::string&) override {actions.push_back(action);return true;}
    std::string state_json() override {return snapshot;}
};
struct CheckedFs final : StdFileSystem {
    using StdFileSystem::StdFileSystem;
    int audioCopies=0,sidecarCopies=0;
    bool failAudio=false,failSidecar=false;
    bool copy_file(const std::string& from,const std::string& to) override {
        if(from.size()>=12&&from.substr(from.size()-12)==".source.json"){
            ++sidecarCopies;if(failSidecar){std::ofstream(to)<<"{\"partial\":";return false;}
        }else {++audioCopies;if(failAudio)return false;}
        return StdFileSystem::copy_file(from,to);
    }
};
void word(std::ofstream& f,uint16_t v){char b[]={char(v),char(v>>8)};f.write(b,2);}
void dword(std::ofstream& f,uint32_t v){char b[]={char(v),char(v>>8),char(v>>16),char(v>>24)};f.write(b,4);}
void wav(const std::filesystem::path& p){
    std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);
    f.write("RIFF",4);dword(f,36+4096*4);f.write("WAVEfmt ",8);dword(f,16);
    word(f,1);word(f,2);dword(f,48000);dword(f,48000*4);word(f,4);word(f,16);f.write("data",4);dword(f,4096*4);
    for(int i=0;i<4096;++i){const int16_t v=static_cast<int16_t>(std::sin(i*.0576)*12000);word(f,v);word(f,v/2);}
}
std::string bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
AppState ready(songcore::SongcoreHost& host,FakeSource& backend,const std::filesystem::path& file,int target){
    AppState s;s.project=&host.edit_project();s.studioOpen=true;s.studioTab=7;s.currentInstrument=17;
    auto& v=s.sourceSampler;v.backend=&backend;v.row=12;v.clipReady=true;
    v.clipPath=file.string();v.clipFrames=4096;v.clipRate=48000;v.clipChannels=2;
    v.targetInstrument=target;v.title="Owned fixture / Électrique";
    return s;
}
void action(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs){
    assert(studio_button({Button::A,ButtonAction::RELEASED,{}},s,host,engine,fs));
}
}
int main(int argc,char** argv){
    assert(argc==2);const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    CheckedFs fs((out/"app-data").string());FakeSource backend;
    auto engine=std::make_unique<AudioEngine>();songcore::SongcoreHost host(engine.get(),48000);
    assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
    const auto file=out/"private/Clip_fixture.wav";wav(file);
    const auto credits=file.string()+".source.json";
    std::ofstream(credits)<<"{\"version\":1,\"title\":\"Owned fixture\",\"source_url\":\"https://example.invalid/fixture\"}\n";

    // Import targets only the requested slot, resets stale SoundFont/synth edits, saves provenance,
    // and requests the same sample editor that the shell opens after handling the button event.
    auto& old=host.edit_project().instruments[73];old.name="Previous source";old.instrumentType=songcore::InstrumentType::EXTERNAL;
    old.root={0,2};old.filterType="lp";old.filterCut=0;old.drive=255;old.reverse=true;old.tableId=3;
    old.modSlots[0].type=songcore::ModType::AHD;old.modSlots[0].dest=songcore::ModDest::VOLUME;old.modSlots[0].decay=1;
    host.edit_project().instruments[17].name="Unrelated instrument";
    AppState s=ready(host,backend,file,73);action(s,host,*engine,fs);
    assert(!s.studioOpen&&s.pendingSampleEditor&&s.currentInstrument==73&&s.projectVersion==1);
    const auto copied=s.sourceImportedPath;assert(!copied.empty());
    assert(std::filesystem::path(copied).parent_path()==out/"app-data/Samples/Clips");
    assert(bytes(file)==bytes(copied)&&bytes(credits)==bytes(copied+".source.json"));
    assert(fs.audioCopies==1&&fs.sidecarCopies==1);
    const auto& fresh=host.project().instruments[73];
    assert(fresh.id==73&&fresh.sampleId==73&&fresh.sampleFilePath==copied&&!fresh.soundfontPath);
    assert(fresh.instrumentType==songcore::InstrumentType::SAMPLER&&fresh.volume==170&&!fresh.reverse&&fresh.tableId==-1);
    assert(fresh.filterType=="off"&&fresh.drive==0&&fresh.modSlots[0].type==songcore::ModType::NONE);
    assert(host.project().instruments[17].name=="Unrelated instrument");
    InputDispatcher dispatch(s,host,fs);
    s.pendingSampleEditor=false;dispatch.open_imported_sample_editor();
    assert(s.currentScreen==ScreenType::SAMPLE_EDITOR&&s.sampleEditor.instrumentId==73&&s.sampleEditor.totalFrames==4096);
    assert(s.sampleEditor.selectionStart==0&&s.sampleEditor.selectionEnd==4096);
    dispatch.on_button_b();
    host.preview_instrument(73);float audio[1024]{};double energy=0;
    for(int block=0;block<8;++block){engine->processLiveBlock(audio,512,2,48000);for(float v:audio){assert(std::isfinite(v));energy+=v*v;}}
    assert(energy>1e-5);host.stop_preview();

    // A successful private-to-library copy survives a later load failure. Retrying uses that exact
    // cached WAV and provenance, without creating duplicates or mutating the previous instrument.
    songcore::SongcoreHost missingEngine(nullptr,48000);
    assert(missingEngine.push_project(songcore::serialize_project(songcore::make_default_project())));
    missingEngine.edit_project().instruments[12].name="Keep on load failure";
    const auto before=songcore::serialize_project(missingEngine.project());
    auto failed=ready(missingEngine,backend,file,12);const int beforeCopies=fs.audioCopies;
    action(failed,missingEngine,*engine,fs);assert(failed.studioOpen&&!failed.pendingSampleEditor);
    assert(!failed.sourceImportedPath.empty()&&fs.audioCopies==beforeCopies+1);
    assert(songcore::serialize_project(missingEngine.project())==before);
    action(failed,missingEngine,*engine,fs);assert(fs.audioCopies==beforeCopies+1);
    failed.project=&host.edit_project();action(failed,host,*engine,fs);
    assert(failed.pendingSampleEditor&&!failed.studioOpen&&fs.audioCopies==beforeCopies+1);

    // Storage refusal must leave the song unchanged and the private clip available.
    auto refused=ready(host,backend,file,9);const auto unmodified=songcore::serialize_project(host.project());
    fs.failAudio=true;action(refused,host,*engine,fs);fs.failAudio=false;
    assert(refused.studioOpen&&!refused.pendingSampleEditor&&refused.sourceImportedPath.empty());
    assert(songcore::serialize_project(host.project())==unmodified&&std::filesystem::exists(file));

    // Metadata cannot silently disappear when a sidecar copy fails. The copied WAV is retained,
    // the instrument is kept, and a retry copies only the missing sidecar before importing.
    auto provenance=ready(host,backend,file,10);const auto beforeMetadata=songcore::serialize_project(host.project());
    fs.failSidecar=true;action(provenance,host,*engine,fs);
    assert(provenance.studioOpen&&!provenance.pendingSampleEditor&&!provenance.sourceImportedPath.empty());
    assert(!fs.file_exists(provenance.sourceImportedPath+".source.json"));
    assert(songcore::serialize_project(host.project())==beforeMetadata);
    const int audioBeforeRetry=fs.audioCopies,metadataBeforeRetry=fs.sidecarCopies;
    fs.failSidecar=false;action(provenance,host,*engine,fs);
    assert(!provenance.studioOpen&&provenance.pendingSampleEditor&&fs.audioCopies==audioBeforeRetry);
    assert(fs.sidecarCopies==metadataBeforeRetry+1&&fs.file_exists(provenance.sourceImportedPath+".source.json"));

    // Evicting a cached destination is recoverable and never causes an existing WAV overwrite.
    auto evicted=ready(host,backend,file,11);evicted.sourceImportedFor=file.string();
    evicted.sourceImportedPath=(out/"no-longer-present.wav").string();action(evicted,host,*engine,fs);
    assert(evicted.pendingSampleEditor&&fs.file_exists(evicted.sourceImportedPath)&&bytes(evicted.sourceImportedPath)==bytes(file));

    // The eighth page participates in wrap navigation. A held edit key must not cause the release
    // to execute a source command or a synth action after changing pages.
    AppState navigation;navigation.project=&host.edit_project();navigation.studioOpen=true;navigation.studioTab=6;
    navigation.sourceSampler.backend=&backend;backend.snapshot="{\"providers\":[{\"id\":\"archive\",\"available\":true}]}";
    ButtonMods page;page.b=true;
    studio_button({Button::DPAD_RIGHT,ButtonAction::PRESSED,page},navigation,host,*engine,fs);assert(navigation.studioTab==7);
    studio_button({Button::B,ButtonAction::RELEASED,{}},navigation,host,*engine,fs);assert(navigation.studioOpen);
    studio_button({Button::SELECT,ButtonAction::RELEASED,{}},navigation,host,*engine,fs);assert(navigation.sourceSampler.showHelp&&!navigation.studioHelp);
    studio_button({Button::B,ButtonAction::RELEASED,{}},navigation,host,*engine,fs);assert(!navigation.sourceSampler.showHelp&&navigation.studioOpen);
    page.a=true;studio_button({Button::DPAD_RIGHT,ButtonAction::PRESSED,page},navigation,host,*engine,fs);assert(navigation.studioTab==0);
    const auto version=navigation.projectVersion;
    studio_button({Button::A,ButtonAction::RELEASED,{}},navigation,host,*engine,fs);assert(navigation.projectVersion==version);
    studio_button({Button::B,ButtonAction::RELEASED,{}},navigation,host,*engine,fs);assert(navigation.studioOpen);
    page.a=false;studio_button({Button::DPAD_LEFT,ButtonAction::PRESSED,page},navigation,host,*engine,fs);assert(navigation.studioTab==7);
    studio_button({Button::B,ButtonAction::RELEASED,{}},navigation,host,*engine,fs);
    studio_button({Button::B,ButtonAction::RELEASED,{}},navigation,host,*engine,fs);assert(!navigation.studioOpen);
    assert(!backend.actions.empty());
    std::puts("PASS: source page import/editor handoff, fresh instrument, provenance failure/retry, cached/stale destinations, eight-page navigation and help");
}
