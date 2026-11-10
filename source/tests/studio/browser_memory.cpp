#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/input_dispatcher.h"
#include "ui/settings_store.h"
#include "ui/std_filesystem.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>

using namespace pt::ui;
namespace {
void u16(std::ofstream& f, unsigned v) { char b[]={char(v),char(v>>8)};f.write(b,2); }
void u32(std::ofstream& f, unsigned v) { u16(f,v);u16(f,v>>16); }
void wave(const std::filesystem::path& path) {
    std::ofstream f(path,std::ios::binary);f.write("RIFF",4);u32(f,36+1024);f.write("WAVEfmt ",8);
    u32(f,16);u16(f,1);u16(f,1);u32(f,48000);u32(f,96000);u16(f,2);u16(f,16);
    f.write("data",4);u32(f,1024);for(int i=0;i<512;++i)u16(f,unsigned(int(std::sin(i*.08)*10000)));
}
int index_of(const AppState& s,const std::string& path) {
    for(size_t i=0;i<s.fileBrowser.items.size();++i) if(s.fileBrowser.items[i].path==path)return int(i);
    assert(!"expected browser entry");return -1;
}
void focus(InputDispatcher& d,AppState& s,const std::string& path) {
    const int target=index_of(s,path);
    for(int i=0;s.fileBrowser.cursor!=target&&i<1000;++i)d.on_dpad_down();
    assert(s.fileBrowser.cursor==target);
}
void choose(InputDispatcher& d,AppState& s,const std::string& path) { focus(d,s,path);d.on_button_a(); }
void instrument_load(InputDispatcher& d,AppState& s) {
    s.currentScreen=ScreenType::INSTRUMENT;s.instrumentCursorRow=0;s.instrumentCursorColumn=2;
    d.on_button_a();assert(s.currentScreen==ScreenType::FILE_BROWSER);
}
void bookmark(InputDispatcher& d,AppState& s,const char* name) {
    choose(d,s,std::string(SOURCE_BOOKMARK_PREFIX)+name);
}

// A SAF-like opaque-path filesystem with deliberately stale parent listings. This exercises the
// shared opener's invalidation and permission fallback without treating pt:// as a native path.
class OpaqueSaf : public StdFileSystem {
  public:
    explicit OpaqueSaf(const std::string& root):StdFileSystem(root) {
        folders={"pt://","pt://grant-A","pt://grant-A/Samples","pt://grant-A/Samples/Jazz %20 音"};
        files["pt://grant-A/Samples/Jazz %20 音"]={"Quiet.wav","Swing.wav"};
    }
    bool granted=true;
    std::set<std::string> folders;
    std::map<std::string,std::vector<std::string>> files;
    std::map<std::string,std::vector<FileInfo>> cache;
    int invalidations=0;
    std::string samples_directory() override {return granted?"pt://grant-A/Samples":"pt://";}
    std::string parent_path(const std::string& path) override {
        if(path.rfind("pt://",0)!=0)return StdFileSystem::parent_path(path);
        if(path=="pt://")return {};
        const auto slash=path.rfind('/');return slash<=4?"pt://":path.substr(0,slash);
    }
    void forget_listing(const std::string& path) override {cache.erase(path);++invalidations;}
    std::vector<FileInfo> list_files(const std::string& dir) override {
        if(dir.rfind("pt://",0)!=0)return StdFileSystem::list_files(dir);
        if(cache.count(dir))return cache[dir];
        std::vector<FileInfo> out;
        if(folders.count(dir)&&(granted||dir=="pt://")) {
            for(const auto& p:folders)if(p!=dir&&parent_path(p)==dir&&granted) {
                FileInfo f;f.path=p;f.name=p.substr(p.rfind('/')+1);f.isDirectory=true;out.push_back(f);
            }
            for(const auto& name:files[dir]) {
                FileInfo f;f.path=dir+"/"+name;f.name=name;f.extension="wav";out.push_back(f);
            }
        }
        return cache[dir]=out;
    }
    bool is_directory(const std::string& path) override {
        if(path.rfind("pt://",0)!=0)return StdFileSystem::is_directory(path);
        if(path=="pt://")return true;
        for(const auto& f:list_files(parent_path(path)))if(f.path==path)return f.isDirectory;
        return false;
    }
};
}

int main(int argc,char**argv) {
    assert(argc==2);const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
    StdFileSystem fs((out/"data").string());Theme theme=theme_default();
    // The old default is migrated once, while unrelated user settings remain intact.
    assert(fs.write_file(fs.settings_path(),R"({"rememberFolder":false,"abxy":2,"help":2,"buttonSound":false})"));
    SettingsValues upgraded;assert(load_settings(fs,upgraded,theme));
    assert(upgraded.rememberFolder&&upgraded.abxyIndex==2&&upgraded.helpMode==2&&!upgraded.buttonSoundEnabled);
    upgraded.rememberFolder=false;assert(save_settings(fs,upgraded,theme));
    SettingsValues optedOut;assert(load_settings(fs,optedOut,theme));assert(!optedOut.rememberFolder);

    auto engine=std::make_unique<AudioEngine>();engine->setDeviceSampleRate(48000);
    songcore::SongcoreHost host(engine.get(),48000);
    assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
    host.set_app_root(fs.parent_path(fs.samples_directory()));
    AppState s;s.project=&host.edit_project();s.currentInstrument=2;
    InputDispatcher d(s,host,fs);
    const auto folder=fs.samples_directory()+"/Drums & Jazz";
    std::filesystem::create_directories(folder);
    std::filesystem::remove(folder+"/Earlier.wav");std::filesystem::remove(folder+"/Another.wav");
    for(int i=0;i<36;++i){char name[32];std::snprintf(name,sizeof(name),"/Hit%02d.wav",i);wave(folder+name);}
    const auto hit=folder+"/Hit28.wav";
    instrument_load(d,s);assert(s.fileBrowser.sampleDomain);choose(d,s,folder);focus(d,s,hit);
    const int scroll=s.fileBrowser.scroll, originalCursor=s.fileBrowser.cursor;assert(scroll>0);d.on_button_a();
    assert(s.currentScreen==ScreenType::INSTRUMENT&&host.project().instruments[2].sampleFilePath==hit);
    instrument_load(d,s);assert(s.fileBrowser.currentDirectory==folder);
    assert(s.fileBrowser.current()->path==hit&&s.fileBrowser.scroll==scroll);
    d.on_button_b();

    // Directory changes outside the app cannot turn a remembered row number into a different sound.
    wave(folder+"/Earlier.wav");wave(folder+"/Another.wav");std::filesystem::remove(folder+"/Hit01.wav");
    instrument_load(d,s);assert(s.fileBrowser.current()->path==hit);
    assert(s.fileBrowser.cursor==originalCursor+1);
    assert(s.fileBrowser.cursor>=s.fileBrowser.scroll&&s.fileBrowser.cursor<s.fileBrowser.scroll+BROWSER_VISIBLE_ROWS);
    d.on_button_b();

    // Actual Sample Editor LOAD shares sample memory and replaces the editor's own slot.
    d.open_imported_sample_editor();assert(s.currentScreen==ScreenType::SAMPLE_EDITOR);
    s.sampleEditor.cursorRow=19;s.sampleEditor.cursorCol=0;d.on_button_a();
    assert(s.browserPurpose==AppState::BrowserPurpose::LOAD_SAMPLE_EDITOR&&s.fileBrowser.currentDirectory==folder);
    assert(s.fileBrowser.current()->path==hit);choose(d,s,folder+"/Hit29.wav");
    assert(s.currentScreen==ScreenType::SAMPLE_EDITOR&&host.project().instruments[2].sampleFilePath==folder+"/Hit29.wav");

    // Leaving without loading still remembers the last visited sample folder.
    const auto child=folder+"/Breaks";std::filesystem::create_directories(child);wave(child+"/Loop.wav");
    instrument_load(d,s);choose(d,s,child);focus(d,s,child+"/Loop.wav");d.on_button_b();
    instrument_load(d,s);assert(s.fileBrowser.currentDirectory==child&&s.fileBrowser.current()->path==child+"/Loop.wav");
    bookmark(d,s,"presets");assert(!s.fileBrowser.sampleDomain&&s.fileBrowser.currentDirectory==fs.instruments_directory());
    d.on_button_b();assert(s.settings.lastSampleFolder==child);
    instrument_load(d,s);bookmark(d,s,"soundfonts");assert(!s.fileBrowser.sampleDomain);
    bookmark(d,s,"samples");assert(s.fileBrowser.sampleDomain&&s.fileBrowser.currentDirectory==child);d.on_button_b();

    // Even identical configured root strings do not make a project/preset/SF browser a sample domain.
    for(auto purpose:{AppState::BrowserPurpose::LOAD_PROJECT,AppState::BrowserPurpose::LOAD_PRESET}) {
        s.currentScreen=ScreenType::INSTRUMENT;d.open_file_browser(purpose,fs.samples_directory(),{});
        assert(!s.fileBrowser.sampleDomain&&s.fileBrowser.currentDirectory==fs.samples_directory());
        d.on_button_b();assert(s.settings.lastSampleFolder==child);
    }
    s.folderConfig.soundfonts=fs.samples_directory();
    host.set_instrument_type(2,songcore::InstrumentType::SOUNDFONT);instrument_load(d,s);
    assert(!s.fileBrowser.sampleDomain&&s.fileBrowser.currentDirectory==fs.samples_directory());
    d.on_button_b();assert(s.settings.lastSampleFolder==child);
    host.set_instrument_type(2,songcore::InstrumentType::SAMPLER);s.folderConfig.soundfonts.reset();

    // Settings round-trip restores folder, selection, viewport and sort in a fresh app state.
    instrument_load(d,s);navigate_to_folder(s.fileBrowser,fs,folder);s.fileBrowser.sortMode=FileSortMode::NAME_DESC;
    rebuild_items(s.fileBrowser,fs);focus(d,s,folder+"/Hit05.wav");
    const int persistedScroll=s.fileBrowser.scroll;assert(persistedScroll>0);d.on_button_b();
    assert(save_settings(fs,s.settings,s.theme));AppState reboot;reboot.project=&host.edit_project();
    assert(load_settings(fs,reboot.settings,reboot.theme));InputDispatcher next(reboot,host,fs);
    instrument_load(next,reboot);assert(reboot.fileBrowser.currentDirectory==folder);
    assert(reboot.fileBrowser.current()->path==folder+"/Hit05.wav"&&reboot.fileBrowser.sortMode==FileSortMode::NAME_DESC);
    assert(reboot.fileBrowser.scroll==persistedScroll);
    next.on_button_b();reboot.settings.rememberFolder=false;
    instrument_load(next,reboot);assert(reboot.fileBrowser.currentDirectory==fs.samples_directory());next.on_button_b();

    // Deleted folder and deleted selected file have bounded, valid fallbacks, never a stale row.
    s.settings.lastSampleFolder=child;std::filesystem::remove_all(child);
    instrument_load(d,s);assert(s.fileBrowser.currentDirectory==fs.samples_directory());d.on_button_b();
    s.settings.lastSampleFolder=folder;s.settings.lastSampleSelection=hit;
    s.settings.lastSampleCursor=1000000;s.settings.lastSampleScroll=1000000;
    std::filesystem::remove(hit);instrument_load(d,s);
    assert(s.fileBrowser.currentDirectory==folder&&s.fileBrowser.current());
    assert(s.fileBrowser.cursor<int(s.fileBrowser.items.size())&&s.fileBrowser.scroll<=s.fileBrowser.cursor);d.on_button_b();

    // Opaque SAF path identity, stale-cache invalidation, deleted folder and revoked grant fallback.
    OpaqueSaf saf((out/"private").string());AppState a;a.project=&host.edit_project();InputDispatcher ad(a,host,saf);
    const std::string opaque="pt://grant-A/Samples/Jazz %20 音";
    a.settings.lastSampleFolder=fs.samples_directory();  // Exists natively, but is outside SAF storage.
    instrument_load(ad,a);assert(a.fileBrowser.currentDirectory==saf.samples_directory());ad.on_button_b();
    instrument_load(ad,a);choose(ad,a,opaque);focus(ad,a,opaque+"/Swing.wav");ad.on_button_b();
    instrument_load(ad,a);assert(a.fileBrowser.currentDirectory==opaque&&a.fileBrowser.current()->path==opaque+"/Swing.wav");
    ad.on_button_b();saf.folders.erase(opaque);instrument_load(ad,a);
    assert(a.fileBrowser.currentDirectory==saf.samples_directory()&&saf.invalidations>0);ad.on_button_b();
    a.settings.lastSampleFolder=opaque;saf.granted=false;instrument_load(ad,a);
    assert(a.fileBrowser.currentDirectory=="pt://"&&a.settings.lastSampleFolder.empty());ad.on_button_b();

    std::puts("PASS: sample-folder memory: native Instrument/Editor load, stable selection/viewport, cancel, source-domain isolation, persistence/migration/opt-out, removed media and opaque SAF cache/grant fallback");
}
