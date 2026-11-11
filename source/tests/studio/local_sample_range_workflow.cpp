#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/input_dispatcher.h"
#include "ui/layout.h"
#include "ui/std_filesystem.h"
#include "common/load_progress.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
namespace fs=std::filesystem;
using namespace pt::ui;
namespace {
void u16(std::ostream& f,uint16_t v){char b[]={char(v),char(v>>8)};f.write(b,2);}
void u32(std::ostream& f,uint32_t v){u16(f,uint16_t(v));u16(f,uint16_t(v>>16));}
void wave(const fs::path& path,uint32_t bytes,bool cues){
 std::ofstream f(path,std::ios::binary);f.write("RIFF",4);u32(f,36+bytes+(cues?60:0));f.write("WAVEfmt ",8);u32(f,16);u16(f,1);u16(f,2);u32(f,48000);u32(f,192000);u16(f,4);u16(f,16);f.write("data",4);u32(f,bytes);
 const uint32_t frames=bytes/4;for(uint32_t i=0;i<std::min(frames,8192u);++i){u16(f,uint16_t(int16_t((int(i%251)-125)*127)));u16(f,uint16_t(int16_t((int(i%241)-120)*139)));}
 if(frames>8192){f.seekp(std::streamoff(44+bytes-1));f.put(0);}
 if(cues){f.write("cue ",4);u32(f,52);u32(f,2);for(uint32_t point:{2000u,7000u}){u32(f,point);u32(f,point);f.write("data",4);u32(f,0);u32(f,0);u32(f,point);}}
 f.flush();assert(f);
}
void wave8(const fs::path& path){
 const uint32_t bytes=6u*1024*1024;std::ofstream f(path,std::ios::binary);f.write("RIFF",4);u32(f,36+bytes);f.write("WAVEfmt ",8);u32(f,16);u16(f,1);u16(f,1);u32(f,48000);u32(f,48000);u16(f,1);u16(f,8);f.write("data",4);u32(f,bytes);for(int i=0;i<4096;++i)f.put(char(64+i%128));f.seekp(44+bytes-1);f.put(char(128));f.flush();assert(f);
}
void mono16(const fs::path& path){
 std::ofstream f(path,std::ios::binary);f.write("RIFF",4);u32(f,36+512);f.write("WAVEfmt ",8);u32(f,16);u16(f,1);u16(f,1);u32(f,48000);u32(f,96000);u16(f,2);u16(f,16);f.write("data",4);u32(f,512);for(int i=0;i<256;++i)u16(f,uint16_t(i*64));f.flush();assert(f);
}
uint64_t filehash(const fs::path& path){std::ifstream f(path,std::ios::binary);std::array<char,65536>b{};uint64_t h=1469598103934665603ull;while(f){f.read(b.data(),b.size());for(std::streamsize i=0;i<f.gcount();++i){h^=uint8_t(b[size_t(i)]);h*=1099511628211ull;}}return h;}
int index(const AppState&s,const std::string&p){for(size_t i=0;i<s.fileBrowser.items.size();++i)if(s.fileBrowser.items[i].path==p)return int(i);assert(false);return -1;}
void focus(InputDispatcher&d,AppState&s,const std::string&p){const int at=index(s,p);for(int i=0;s.fileBrowser.cursor!=at&&i<1000;++i)d.on_dpad_down();assert(s.fileBrowser.cursor==at);}
void load_browser(InputDispatcher&d,AppState&s){s.currentScreen=ScreenType::INSTRUMENT;s.instrumentCursorRow=0;s.instrumentCursorColumn=2;d.on_button_a();assert(s.currentScreen==ScreenType::FILE_BROWSER);}
void ppm(const fs::path&p,const Canvas&c){std::ofstream f(p,std::ios::binary);f<<"P6\n640 480\n255\n";for(int i=0;i<640*480;++i){auto v=c.pixels()[i];char rgb[]={char(v>>16),char(v>>8),char(v)};f.write(rgb,3);}assert(f);}
uint64_t pixels(const Canvas&c){uint64_t h=1469598103934665603ull;for(int i=0;i<640*480;++i){h^=c.pixels()[i];h*=1099511628211ull;}return h;}
}
int main(int argc,char**argv){
 assert(argc==2);const auto out=fs::absolute(argv[1]);fs::create_directories(out);StdFileSystem files((out/"data").string());
 const auto folder=files.samples_directory()+"/User Jazz",big=folder+"/ZZ_Long_Source.wav",old=folder+"/Existing.wav";fs::create_directories(folder);wave(big,32u*1024*1024,true);wave(old,512,false);
 for(int i=0;i<28;++i)wave(folder+"/Hit"+std::to_string(i)+".wav",128,false);
 const auto expanded8=folder+"/Small-Raw-Large-PCM.wav";wave8(expanded8);
 const auto ordinary=folder+"/Ordinary-Mono16.wav";mono16(ordinary);
 const auto sourceHash=filehash(big);const auto sourceSize=fs::file_size(big);const auto sourceTime=fs::last_write_time(big);
 auto engine=std::make_unique<AudioEngine>();engine->setDeviceSampleRate(48000);songcore::SongcoreHost host(engine.get(),48000);assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));host.set_app_root(files.root());
 assert(host.load_sample(2,old));host.edit_project().instruments[2].name="MY EXISTING SOUND";
 AppState s;s.project=&host.edit_project();s.currentInstrument=2;s.settings.fontStyle=16;s.settings.abxyIndex=2;s.settings.rememberFolder=true;
 InputDispatcher d(s,host,files);long long now=1000;auto poll=[&]{d.set_now(++now);};
 const auto before=songcore::serialize_project(host.project());const auto beforeVersion=s.projectVersion;
 host.play_phrase(0);assert(host.is_playing());assert(host.preview_file(old));const int previewFrames=engine->getSampleLength(255);assert(previewFrames>0);
 assert(!d.open_large_sample(ordinary,"Ordinary mono16",false,true));assert(!s.localSampleRange.isOpen&&host.is_playing()&&engine->getSampleLength(255)==previewFrames);assert(songcore::serialize_project(host.project())==before);host.stop();

 load_browser(d,s);navigate_to_folder(s.fileBrowser,files,folder);focus(d,s,big);const int cursor=s.fileBrowser.cursor,scroll=s.fileBrowser.scroll;assert(scroll>0);
 d.on_button_a();assert(s.localSampleRange.isOpen&&!s.localSampleRange.source.playing&&s.localSampleRange.targetInstrument==2);
 assert(songcore::serialize_project(host.project())==before&&s.projectVersion==beforeVersion);
 TrackerLayout layout;Canvas c;std::set<uint64_t> screens;
 for(int font:{0,7,16}){s.settings.fontStyle=font;layout.draw(c,s);assert(c.font_style()==font&&screens.insert(pixels(c)).second);ppm(out/("local-range-font"+std::to_string(font)+".ppm"),c);d.on_select();assert(s.localSampleRange.help);layout.draw(c,s);assert(c.font_style()==font&&screens.insert(pixels(c)).second);ppm(out/("local-help-font"+std::to_string(font)+".ppm"),c);d.on_select();assert(!s.localSampleRange.help);}
 s.settings.fontStyle=16;d.on_button_b();assert(!s.localSampleRange.isOpen&&s.currentScreen==ScreenType::FILE_BROWSER);assert(s.fileBrowser.currentDirectory==folder&&s.fileBrowser.cursor==cursor&&s.fileBrowser.scroll==scroll&&s.fileBrowser.current()->path==big);
 d.on_start();poll();assert(s.localSampleRange.isOpen&&s.localSampleRange.source.playing);d.on_stop_preview();poll();assert(s.localSampleRange.source.playing);
 const auto rangeIn=s.localSampleRange.inFrame,rangeOut=s.localSampleRange.outFrame;
 const int rangeRow=s.localSampleRange.row;
 d.on_select();assert(s.localSampleRange.help);d.on_start();poll();assert(s.localSampleRange.source.playing);
 d.on_button_b();poll();
 assert(s.localSampleRange.isOpen&&!s.localSampleRange.help&&s.localSampleRange.source.playing);
 assert(s.localSampleRange.inFrame==rangeIn&&s.localSampleRange.outFrame==rangeOut&&s.localSampleRange.row==rangeRow);
 d.on_start();poll();assert(!s.localSampleRange.source.playing);
 setenv("PT_MEMORY_LIMIT_MB","16",1);d.on_button_b();d.on_button_a();assert(s.localSampleRange.isOpen);s.localSampleRange.row=6;d.on_button_a();assert(s.localSampleRange.isOpen&&s.localSampleRange.message.find("RAM")!=std::string::npos&&s.localSampleRange.message.find("Crop")!=std::string::npos);assert(songcore::serialize_project(host.project())==before);
 // A small raw8-bit file can need four times its byte size as float PCM.
 d.on_button_b();focus(d,s,expanded8);d.on_button_a();assert(s.localSampleRange.isOpen&&s.localSampleRange.source.bits==8&&s.localSampleRange.source.channels==1);assert(songcore::serialize_project(host.project())==before);d.on_button_b();focus(d,s,big);d.on_button_a();assert(s.localSampleRange.isOpen);
 s.localSampleRange.row=3;for(int i=0;i<30;++i)d.on_dpad_right();assert(s.localSampleRange.outFrame-s.localSampleRange.inFrame<=s.localSampleRange.maxImportFrames);
 auto& source=host.local_wav_source();assert(source.seek(1000));poll();s.localSampleRange.row=1;d.on_button_a();assert(s.localSampleRange.inFrame==1000);d.on_a_right();assert(s.localSampleRange.inFrame==1480);d.on_a_left();assert(s.localSampleRange.inFrame==1000);
 assert(source.seek(8000));poll();s.localSampleRange.row=2;d.on_button_a();assert(s.localSampleRange.outFrame==8000);d.on_a_up();assert(s.localSampleRange.outFrame==488000);d.on_a_down();assert(s.localSampleRange.outFrame==8000);
 d.on_r_right();poll();assert(s.localSampleRange.source.positionFrame==248000);d.on_r_left();poll();assert(s.localSampleRange.source.positionFrame==8000);
 // Cancellation before export and cancellation after export during instrument decode both preserve the old document.
 s.localSampleRange.row=5;pt::set_load_tick([](float){return false;});d.on_button_a();assert(s.localSampleRange.isOpen&&songcore::serialize_project(host.project())==before);pt::set_load_tick({});
 pt::set_load_tick([&](float){return source.snapshot().exporting;});d.on_button_a();assert(s.localSampleRange.isOpen&&songcore::serialize_project(host.project())==before);assert(s.localSampleRange.source.open);pt::set_load_tick({});
 // Success goes through the actual editor LOAD path and targets its own instrument rather than currentInstrument.
 d.on_button_b();d.on_button_b();load_browser(d,s);assert(s.fileBrowser.currentDirectory==folder&&s.fileBrowser.current()->path==big);d.on_button_b();
 d.open_imported_sample_editor();assert(s.currentScreen==ScreenType::SAMPLE_EDITOR);s.sampleEditor.cursorRow=19;s.sampleEditor.cursorCol=0;d.on_button_a();assert(s.browserPurpose==AppState::BrowserPurpose::LOAD_SAMPLE_EDITOR);focus(d,s,big);s.currentInstrument=1;d.on_button_a();assert(s.localSampleRange.isOpen&&s.localSampleRange.targetInstrument==2);
 assert(source.seek(1000));poll();s.localSampleRange.row=1;d.on_button_a();assert(source.seek(8000));poll();s.localSampleRange.row=2;d.on_button_a();s.localSampleRange.row=5;d.on_button_a();
 assert(!s.localSampleRange.isOpen&&s.currentScreen==ScreenType::SAMPLE_EDITOR&&s.currentInstrument==2);const auto& instrument=host.project().instruments[2];assert(instrument.name=="MY EXISTING SOUND"&&instrument.sampleFilePath&&*instrument.sampleFilePath!=big);
 assert(instrument.sampleFilePath->find("/Samples/Chops/")!=std::string::npos);assert((instrument.sliceMarkers==std::vector<int64_t>{1000,6000}));
 assert(s.settings.fontStyle==16&&s.settings.abxyIndex==2&&s.settings.rememberFolder);assert(!source.snapshot().open);assert(host.project().instruments[1].sampleFilePath==songcore::make_default_project().instruments[1].sampleFilePath);
 pt::LocalWavSource clip;assert(clip.open(*instrument.sampleFilePath));const auto meta=clip.snapshot();assert(meta.totalFrames==7000&&meta.rate==48000&&meta.channels==2&&meta.bits==16);assert(s.projectVersion>beforeVersion);
 assert(filehash(big)==sourceHash&&fs::file_size(big)==sourceSize&&fs::last_write_time(big)==sourceTime);unsetenv("PT_MEMORY_LIMIT_MB");
 std::puts("PASS: actual large-WAV browser LOAD/audition, modal controls/help/STOP_PREVIEW, folder/cursor/scroll memory, low-RAM whole-load refusal, cancelled export and instrument load preserve project, editor-slot crop with rebased cue markers, fonts0/7/16 renders, source hash/time and user font/control preferences preserved.");
}
