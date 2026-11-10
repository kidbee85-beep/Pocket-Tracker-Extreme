#include "studio/punch.h"
#include "studio/rack.h"
#include "studio/synth.h"
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/studio.h"
#include "ui/button_mapper.h"
#include "ui/input_dispatcher.h"
#include "ui/settings_store.h"
#include "ui/input_config.h"
#include "ui/theme_io.h"
#include "ui/std_filesystem.h"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <set>
#include <vector>

namespace {std::atomic<bool> audioGuard{false};std::atomic<int> audioAllocations{0};}
void* operator new(std::size_t n){if(audioGuard.load())++audioAllocations;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

namespace {
using namespace pt::ui;
constexpr int block=256;
void source(float* out,int64_t offset,float rate){
 for(int i=0;i<block;++i){const double time=(offset+i)/rate;
  const uint32_t hash=static_cast<uint32_t>(offset+i)*1664525u+1013904223u;
  const float noise=(static_cast<int>(hash>>24)-128)/128.f;
  out[2*i]=.17f*std::sin(6.2831853*(170*time+19*time*time))+.06f*noise+.03f;
  out[2*i+1]=.13f*std::sin(6.2831853*323*time)+.04f*std::sin(6.2831853*2900*time)-.02f;
 }
}
void process(studio::Rack& rack,float* data,float rate){audioGuard=true;rack.process(data,block,2,rate);audioGuard=false;}
void snapshot(const std::filesystem::path& path,const AppState& state){
 Canvas canvas;draw_studio(canvas,state);std::ofstream out(path,std::ios::binary);out<<"P6\n640 480\n255\n";
 for(int i=0;i<640*480;++i){const auto p=canvas.pixels()[i];const char rgb[]={char(p>>16),char(p>>8),char(p)};out.write(rgb,3);}assert(out.good());
}
struct Input {
 AppState& state;songcore::SongcoreHost& host;AudioEngine& engine;FileSystem& fs;
 InputDispatcher dispatch;MapperState mapper;uint64_t now=1000;
 Input(AppState& s,songcore::SongcoreHost& h,AudioEngine& e,FileSystem& f):state(s),host(h),engine(e),fs(f),dispatch(s,h,f){}
 void event(Button button,ButtonAction action,ButtonMods mods={}){
  now+=20;dispatch.set_now(static_cast<long long>(now));ButtonEvent e{button,action,mods};
  if(!studio_button(e,state,host,engine,fs))handle_button(e,dispatch,mapper,now);
 }
};
void dsp(){
 std::set<int> ids;std::set<std::string> names;
 for(int bank=0;bank<8;++bank)for(int pad=0;pad<8;++pad){const int id=studio::punch_id(bank,pad);
  assert(ids.insert(id).second&&id>=1&&id<=64);assert(names.insert(studio::punch_name(id)).second);
  assert(studio::punch_bank(id)==bank&&studio::punch_pad(id)==pad&&*studio::punch_detail(id));}
 assert(ids.size()==64);
 const char* legacy[]={"BEAT REPEAT","ROLL","REVERSE","LOW PASS","BIT CRUSH","DELAY THROW","GATE","TAPE STOP","HALF SPEED","DOUBLE SPEED","REVERSE ROLL","HIGH PASS","BAND PASS","DISTORT","WAVE FOLD","RING MOD","TREMOLO","AUTO PAN","STEREO SWAP","MONO","WIDE","DUCK","MICRO STUTTER","DROP OUT"};
 for(int i=0;i<24;++i)assert(std::string(studio::punch_name(i+1))==legacy[i]);
 std::vector<std::vector<float>> signatures;
 double worstUs=0;int worstId=0;
 for(int id=1;id<=64;++id){
  studio::Rack rack;std::array<float,block*2> data{},dry{};int64_t frame=0;
  for(int n=0;n<400;++n){source(data.data(),frame,48000);dry=data;process(rack,data.data(),48000);assert(data==dry);frame+=block;}
  rack.punch=id;double diff=0,energy=0;std::vector<float> signature;signature.reserve(block*2*64);
  const auto start=std::chrono::steady_clock::now();
  for(int n=0;n<256;++n){source(data.data(),frame,48000);dry=data;process(rack,data.data(),48000);frame+=block;
   for(int k=0;k<block*2;++k){assert(std::isfinite(data[k])&&std::fabs(data[k])<=2.001f);if(n>4){diff+=std::pow(data[k]-dry[k],2);energy+=data[k]*data[k];if(k%8==0)signature.push_back(data[k]);}}
  }
  const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/256;
  if(us>worstUs){worstUs=us;worstId=id;}if(diff<=1e-5)std::cerr<<"inaudible id "<<id<<" "<<studio::punch_name(id)<<" diff "<<diff<<"\n";assert(diff>1e-5);if(id!=24)assert(energy>1e-5);
  for(const auto& previous:signatures){double delta=0;for(size_t k=0;k<signature.size();++k)delta+=std::fabs(signature[k]-previous[k]);assert(delta>1e-3);}
  signatures.push_back(std::move(signature));
  rack.punch=0;for(int n=0;n<8;++n){source(data.data(),frame,48000);process(rack,data.data(),48000);frame+=block;}
  source(data.data(),frame,48000);dry=data;process(rack,data.data(),48000);assert(data==dry);
 }
 // Different block boundaries and sample rates must not expose invalid capture/delay indices.
 for(float sr:{8000.f,44100.f,96000.f}){studio::Rack rack;std::array<float,block*2> data{};
  for(int n=0;n<100;++n){source(data.data(),n*block,sr);process(rack,data.data(),sr);}
  for(int id=1;id<=64;++id){rack.punch=id;rack.tempo=id%2?20:999;for(int n=0;n<8;++n){source(data.data(),n*block,sr);process(rack,data.data(),sr);for(float v:data)assert(std::isfinite(v)&&std::fabs(v)<=2.001f);}}
  rack.punch=0;for(int n=0;n<12;++n){source(data.data(),n*block,sr);process(rack,data.data(),sr);}
 }
 assert(audioAllocations==0);
 // Releasing a long capture and striking again must capture the current input,
 // never the audio that preceded the previous hold. Snapshot copying stays bounded.
 {
  studio::Rack rack;std::array<float,block*2> data{};
  for(int n=0;n<40;++n){data.fill(.12f);process(rack,data.data(),48000);}
  rack.punch=1;for(int n=0;n<220;++n){data.fill(-.31f);process(rack,data.data(),48000);}
  rack.punch=0;for(int n=0;n<6;++n){data.fill(-.31f);process(rack,data.data(),48000);}
  rack.punch=1;for(int n=0;n<12;++n){data.fill(-.31f);process(rack,data.data(),48000);}
  assert(std::fabs(data.back()+.31f)<1e-4f);
 }
 // A low device rate must not turn the second formant into unstable hidden state.
 {
  studio::Rack rack;std::array<float,block*2> data{};rack.punch=34;
  for(int n=0;n<180;++n){source(data.data(),n*block,8000);process(rack,data.data(),8000);}
  double e=0;for(float v:data){assert(std::isfinite(v));e+=v*v;}assert(e>1e-5);
 }
 std::cout<<"PASS: all 64 DSP gestures audible, distinct, finite, dry on release; zero C++ allocations in processing. Worst 256-frame test block "<<worstUs<<" us / "<<studio::punch_name(worstId)<<" (host measurement incl. test comparisons).\n";
}
void ui(const std::filesystem::path& out){
 StdFileSystem fs((out/"app-data").string());auto engine=std::make_unique<AudioEngine>();songcore::SongcoreHost host(engine.get(),48000);
 engine->setDeviceSampleRate(48000);auto project=songcore::make_default_project();project.tracks[0].chainRefs={0};project.chains[0].phraseRefs[0]=0;
 project.phrases[0].steps[0].note=songcore::Note::C4();project.phrases[0].steps[0].instrument=0;
 project.instruments[0].sampleFilePath=studio::custom_uri(103,50,80,100);project.instruments[0].loopMode="fwd";project.instruments[0].volume=170;project.instruments[0].modSlots=std::vector<songcore::ModSlot>(4);
 assert(host.push_project(songcore::serialize_project(project)));assert(host.load_sample(0,*project.instruments[0].sampleFilePath));host.push_params();
 const auto original=songcore::serialize_project(host.project());
 for(bool selectFirst:{false,true})for(bool releaseLFirst:{false,true}){
  AppState state;state.project=&host.edit_project();state.currentScreen=ScreenType::PHRASE;Input input(state,host,*engine,fs);
  ButtonMods l;l.l=true;ButtonMods sel;sel.select=true;auto both=l;both.select=true;
  input.event(selectFirst?Button::SELECT:Button::L_SHIFT,ButtonAction::PRESSED,selectFirst?sel:l);
  input.event(selectFirst?Button::L_SHIFT:Button::SELECT,ButtonAction::PRESSED,both);assert(state.studioOpen&&state.studioTab==3);
  input.event(releaseLFirst?Button::L_SHIFT:Button::SELECT,ButtonAction::RELEASED,releaseLFirst?sel:l);
  input.event(releaseLFirst?Button::SELECT:Button::L_SHIFT,ButtonAction::RELEASED,{});
  assert(!state.studioHelp&&!state.helpOpen&&!state.helpFull&&!input.mapper.selectPressedAlone);
 }
 AppState state;state.project=&host.edit_project();state.studioOpen=true;state.studioTab=3;state.theme=theme_by_name("PETROL",VisualizerType::OCTA);
 Input input(state,host,*engine,fs);host.play_song(0);assert(host.is_playing());ButtonMods a;a.a=true;
 for(int bank=0;bank<8;++bank){state.studioPunch=studio::punch_id(bank,0);
  input.event(Button::DPAD_RIGHT,ButtonAction::PRESSED);assert(state.studioPunch==studio::punch_id(bank,1));
  input.event(Button::DPAD_DOWN,ButtonAction::PRESSED);assert(state.studioPunch==studio::punch_id(bank,5));
  input.event(Button::DPAD_LEFT,ButtonAction::PRESSED);assert(state.studioPunch==studio::punch_id(bank,4));
  input.event(Button::DPAD_UP,ButtonAction::PRESSED);assert(state.studioPunch==studio::punch_id(bank,0));
  input.event(Button::A,ButtonAction::PRESSED,a);assert(engine->studioRack.punch==state.studioPunch);
  input.now+=10000;input.dispatch.set_now(input.now);assert(!state.studioHelp&&state.studioAUsed);
  input.event(Button::DPAD_RIGHT,ButtonAction::PRESSED,a);assert(engine->studioRack.punch==studio::punch_id(bank,1));
  snapshot(out/("punch-"+std::to_string(bank)+"-active.ppm"),state);
  input.event(Button::A,ButtonAction::RELEASED);assert(engine->studioRack.punch==0&&!state.studioAUsed);
  snapshot(out/("punch-"+std::to_string(bank)+".ppm"),state);
  input.event(Button::A,ButtonAction::PRESSED,a);input.event(Button::R_SHIFT,ButtonAction::PRESSED,a);
  assert(engine->studioRack.punch==0&&!state.studioAUsed&&studio::punch_bank(state.studioPunch)==(bank+1)%8);
  input.event(Button::A,ButtonAction::RELEASED);assert(engine->studioRack.punch==0);
 }
 // Actual sequencer output is muted by held A and resumes after release; transport/project survive.
 state.studioPunch=24;host.stop();host.play_song(0);host.poll();std::array<float,block*2> data{};
 for(int n=0;n<12;++n){host.poll();engine->processLiveBlock(data.data(),block,2,48000);}
 input.event(Button::A,ButtonAction::PRESSED,a);for(int n=0;n<12;++n){host.poll();engine->processLiveBlock(data.data(),block,2,48000);}
 double muted=0;for(float v:data)muted+=v*v;assert(muted<1e-8);
 input.event(Button::A,ButtonAction::RELEASED);for(int n=0;n<12;++n){host.poll();engine->processLiveBlock(data.data(),block,2,48000);}
 double restored=0;for(float v:data)restored+=v*v;assert(restored>1e-5&&host.is_playing());
 // START cancels the hold and remains transport, with A still physically held.
 input.event(Button::A,ButtonAction::PRESSED,a);input.event(Button::START,ButtonAction::PRESSED,a);
 assert(!host.is_playing()&&!state.studioAUsed&&engine->studioRack.punch==0);
 input.event(Button::DPAD_RIGHT,ButtonAction::PRESSED,a);assert(engine->studioRack.punch==0);
 input.event(Button::START,ButtonAction::RELEASED,a);input.event(Button::A,ButtonAction::RELEASED);
 host.play_song(0);host.poll();
 input.event(Button::A,ButtonAction::PRESSED,a);input.event(Button::SELECT,ButtonAction::PRESSED);assert(engine->studioRack.punch==0);
 input.event(Button::SELECT,ButtonAction::RELEASED);assert(state.studioHelp);input.event(Button::A,ButtonAction::RELEASED);snapshot(out/"punch-help.ppm",state);
 input.event(Button::B,ButtonAction::RELEASED);assert(!state.studioHelp);
 input.event(Button::DPAD_RIGHT,ButtonAction::PRESSED,a);assert(engine->studioRack.punch==0&&!state.studioAUsed);
 input.event(Button::A,ButtonAction::PRESSED,a);ButtonMods b;b.b=true;input.event(Button::DPAD_RIGHT,ButtonAction::PRESSED,b);assert(state.studioTab==4&&engine->studioRack.punch==0);input.event(Button::B,ButtonAction::RELEASED);
 state.studioTab=3;input.event(Button::A,ButtonAction::PRESSED,a);input.event(Button::B,ButtonAction::RELEASED);assert(!state.studioOpen&&engine->studioRack.punch==0);
 state.studioOpen=true;state.studioTab=3;input.event(Button::A,ButtonAction::PRESSED,a);engine->stopAllRamped();assert(engine->studioRack.punch==0);
 input.event(Button::DPAD_RIGHT,ButtonAction::PRESSED,a);assert(engine->studioRack.punch==0&&!state.studioAUsed);
 assert(songcore::serialize_project(host.project())==original);
 state.studioOpen=true;state.studioTab=6;
 const auto themes=theme_builtins();assert(themes.size()==50);std::set<std::string> names;
 const char* legacyThemes[]={"STUDIO","MIDNIGHT","PLUM","MOSS","COPPER","GLACIER","ROSE","GRAPHITE","ULTRAVIOLET","CLASSIC","AMBER","BLUE","MONO","BUBBLEGUM","BLUE OLED","AMBER OLED","CARBON","TITANIUM","SAHARA","DEEP CIRCUIT","VELVET","BLUEPRINT","EMBER","OBSIDIAN"};
 const int legacyStyles[]={0,5,6,3,1,5,6,1,4,7,1,5,7,6,7,7,2,1,3,4,6,5,0,2};
 for(int i=0;i<24;++i){assert(themes[i].name==legacyThemes[i]);assert(themes[i].surfaceStyle==legacyStyles[i]);}
 const char* beta2Themes[]={"PORCELAIN NOIR","AURORA","OXBLOOD","PETROL","MOONSTONE","BRASS CONSOLE","OCEAN GLASS","FOREST VELVET"};
 const int beta2Styles[]={8,10,11,9,8,11,10,9};
 for(int i=0;i<8;++i){assert(themes[i+24].name==beta2Themes[i]);assert(themes[i+24].surfaceStyle==beta2Styles[i]);}
 const char* beta3Themes[]={"BUNKA LAB","VIOLET VECTOR","RASTER BLOOM","CHROME DUSK","ISO STATION","PIXEL GARDEN","COPPER ROUTES","NIGHT ASSEMBLY"};
 const int beta3Styles[]={12,14,13,12,15,16,17,17};
 for(int i=0;i<8;++i){assert(themes[i+32].name==beta3Themes[i]);assert(themes[i+32].surfaceStyle==beta3Styles[i]);}
 assert(theme_default().name=="STUDIO"&&theme_default().surfaceStyle==0&&theme_default().visualizerType==VisualizerType::OCTA);
 for(size_t i=0;i<themes.size();++i){const auto& theme=themes[i];assert(names.insert(theme.name).second);Theme restoredTheme;
  assert(parse_theme(serialize_theme(theme),restoredTheme));assert(serialize_theme(theme)==serialize_theme(restoredTheme));
  if(i>=24){state.theme=theme;snapshot(out/("theme-"+theme.name+".ppm"),state);state.studioTab=3;snapshot(out/("punch-theme-"+theme.name+".ppm"),state);state.studioTab=6;}
 }
 Theme custom=themes[16];custom.name="MY SAVED MATERIAL";custom.textParam=0xFFAABBCC;custom.surfaceStyle=2;custom.visualizerType=VisualizerType::SPECTRUM;
 SettingsValues settings;settings.abxyIndex=2;settings.scalingBilinear=false;settings.helpMode=2;settings.buttonSoundEnabled=false;
 assert(save_settings(fs,settings,custom));SettingsValues loaded;Theme loadedTheme;assert(load_settings(fs,loaded,loadedTheme));
 assert(serialize_theme(custom)==serialize_theme(loadedTheme)&&loaded.abxyIndex==2&&!loaded.scalingBilinear&&loaded.helpMode==2&&!loaded.buttonSoundEnabled);
 assert(save_settings_if_changed(fs,loaded,loadedTheme)==SettingsWrite::UNCHANGED);
 const std::string mapping=R"({"controller":{"abxy":"nintendo"},"keyboard":{"A":["X"],"B":["Z"],"SELECT":["Tab"]},"gamepad":{"A":["b"],"SELECT":["back"]},"repeat":{"delay":630,"interval":47}})";
 assert(fs.write_file(fs.config_path(),mapping));InputConfig config;std::vector<InputConfigWarning> warnings;
 assert(load_input_config(fs,config,warnings)&&warnings.empty()&&config.abxy==AbxyLayout::NINTENDO);
 assert(config.keyboard[Button::A]->at(0)=="X"&&config.keyboard[Button::SELECT]->at(0)=="Tab"&&config.gamepad[Button::A]->at(0)=="b"&&config.repeat.delay==630&&config.repeat.interval==47);
 std::string savedMapping;assert(fs.read_file(fs.config_path(),savedMapping)&&savedMapping==mapping);
 for(int style=0;style<kSurfaceStyleCount;++style){custom.surfaceStyle=style;Theme roundtrip;assert(parse_theme(serialize_theme(custom),roundtrip));assert(roundtrip.surfaceStyle==style);}
 std::cout<<"PASS: shortcut orders, 8-bank pad navigation, held A/release/help/page/exit/stop safety, running sequencer audio, project unchanged, all 40 existing theme names/material IDs plus 10 new themes, 50 theme roundtrips, 18 materials and saved settings preserved.\n";
}
}
int main(int argc,char** argv){assert(argc==2);const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);dsp();ui(out);}
