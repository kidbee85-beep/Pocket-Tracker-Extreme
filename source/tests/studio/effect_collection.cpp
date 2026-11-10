// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/studio.h"
#include "ui/input_dispatcher.h"
#include "ui/std_filesystem.h"
#include "ui/studio_preset_io.h"
#include "ui/fx_preset_io.h"
#include "ui/fx_preset_bank.h"
#include "ui/layout.h"
#include "effects/modules/reverb-module.h"
#include "effects/modules/delay-module.h"
#include "studio/preset_catalog.h"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <thread>
#include <vector>
#include <cstring>
#include <new>

// Test-only allocator fill: firmware-style zeroed storage and reused dirty heap
// must both survive the complete native preset collection. Never linked into the app.
void* operator new(std::size_t size){
 void* p=std::malloc(size?size:1);if(!p)throw std::bad_alloc();
 if(const char* fill=std::getenv("PT_EFFECT_HEAP_FILL"))std::memset(p,int(std::strtol(fill,nullptr,0))&255,size);
 return p;
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using namespace pt::ui;
using json=songcore::json;
namespace fs=std::filesystem;
namespace {
constexpr int rate=48000,block=256;
std::string read(const fs::path& p){std::ifstream f(p);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
void ppm(const fs::path& p,const AppState& s){Canvas c;TrackerLayout l;l.draw(c,s);std::ofstream f(p,std::ios::binary);f<<"P6\n640 480\n255\n";for(int i=0;i<640*480;++i){const auto q=c.pixels()[i];const char b[]={char(q>>16),char(q>>8),char(q)};f.write(b,3);}assert(f.good());}
void source(float* l,float* r,int offset,bool sustained=false){
 for(int i=0;i<block;++i){const int f=offset+i;const double t=double(f)/rate;
  if(t>(sustained?1.2:.6)){l[i]=r[i]=0;continue;}
  const uint32_t seed=uint32_t(f)*1664525u+1013904223u;const float noise=(int(seed>>24)-128)/128.f;
  const float pulse=std::exp(-std::fmod(t,.125)*39);
  l[i]=.11f*std::sin(6.2831853*113*t)+.045f*std::sin(6.2831853*339*t)+noise*.065f*pulse;
  r[i]=.09f*std::sin(6.2831853*127*t)+.04f*std::sin(6.2831853*452*t)+noise*.052f*pulse;
 }
}
struct Stats {double energy=0,peak=0,early=0,late=0;int64_t frames=0;uint64_t hash=1469598103934665603ull;
 void add(float l,float r,int offset){for(float x:{l,r}){assert(std::isfinite(x)&&std::abs(x)<2.f);peak=std::max(peak,double(std::abs(x)));energy+=x*x;if(offset>=rate&&offset<4*rate)early+=x*x;if(offset>=8*rate)late+=x*x;hash=(hash^uint32_t(int32_t(x*10000000)))*1099511628211ull;}++frames;}
 json result()const{return {{"peak",peak},{"rms",std::sqrt(energy/(frames*2))},{"tail_energy_8_to_10s",late},{"response_signature",std::to_string(hash)}};}
};
// A broken DSP loop must fail with its exact preset code rather than pinning an unattended test.
// Stages: 1000+reverbID, 2000+delayID, 3000+FXID, 4000+masterID. This is test-only.
struct Watchdog {
 std::atomic<int> stage{0};std::thread thread;
 Watchdog():thread([this]{int last=-1;auto since=std::chrono::steady_clock::now();for(;;){const int current=stage.load();if(current<0)return;const auto now=std::chrono::steady_clock::now();if(current!=last){last=current;since=now;}if(now-since>std::chrono::seconds(3)){std::fprintf(stderr,"FAILED: preset test stage %d exceeded 3 seconds\n",current);std::fflush(stderr);std::_Exit(124);}std::this_thread::sleep_for(std::chrono::milliseconds(20));}}){}
 ~Watchdog(){stage=-1;thread.join();}
};
void wav(const fs::path&p,const std::vector<int16_t>& data){std::ofstream f(p,std::ios::binary);auto u16=[&](unsigned v){for(int i=0;i<2;++i)f.put(char(v>>(8*i)));};auto u32=[&](unsigned v){for(int i=0;i<4;++i)f.put(char(v>>(8*i)));};f.write("RIFF",4);u32(36+data.size()*2);f.write("WAVEfmt ",8);u32(16);u16(1);u16(2);u32(rate);u32(rate*4);u16(4);u16(16);f.write("data",4);u32(data.size()*2);for(auto x:data)u16(uint16_t(x));assert(f.good());}
void choose(InputDispatcher& d,AppState& s,const std::string& path){bool found=false;for(size_t i=0;i<s.fileBrowser.items.size();++i)if(s.fileBrowser.items[i].path==path){s.fileBrowser.cursor=int(i);found=true;break;}assert(found);d.on_button_a();}
void legacy_presets(){
 // Fingerprints captured from the frozen beta.2 source/library, before the append-only expansion.
 // Every old name and parameter is covered; new category/description metadata is not audio data.
 uint64_t h=1469598103934665603ull;auto number=[&](int v){h=(h^uint32_t(v))*1099511628211ull;};
 auto name=[&](const char*s){while(*s)h=(h^uint8_t(*s++))*1099511628211ull;number(0);};
 for(int i=0;i<20;++i){const auto p=kReverbPresets[i];name(p.name);for(int v:{p.algo,p.decay,p.room,p.damp,p.pre,p.width,p.mod})number(v);}assert(h==4693087645594699445ull);
 h=1469598103934665603ull;for(int i=0;i<20;++i){const auto p=kDelayPresets[i];name(p.name);for(int v:{int(p.pong),int(p.sync),p.time,p.feedback,p.tone,p.wobble,p.reverbSend})number(v);}assert(h==3470732013701148508ull);
 h=1469598103934665603ull;for(int i=0;i<160;++i){name(studio::effect_preset_name(i).c_str());for(auto v:studio::effect_preset(i))number(v);}assert(h==14675086671320068091ull);
 h=1469598103934665603ull;for(int i=0;i<32;++i){name(studio::master_preset_name(i));for(auto v:studio::master_preset(i))number(v);}assert(h==11387399138178123126ull);
}
void ui(const fs::path& factory,const fs::path& out){
 auto engine=std::make_unique<AudioEngine>();songcore::SongcoreHost host(engine.get(),rate);
 auto& p=host.edit_project();p.instruments[0].sampleFilePath="/@mutable/p0-128-128-128.wav";
 p.tracks[0].chainRefs={0};p.chains[0].phraseRefs[0]=0;p.phrases[0].steps[0].note=songcore::Note::C4();p.phrases[0].steps[0].instrument=0;
 host.push_params();
 StdFileSystem files((out/"test-appdata").string());AppState s;s.project=&p;s.theme=theme_by_name("BUNKA LAB",VisualizerType::OCTA);s.currentScreen=ScreenType::INSTRUMENT;s.studioOpen=true;s.studioTab=1;s.studioRow=10;
 s.settings.lastSampleFolder="unchanged-sample-folder";s.settings.lastSampleSelection="unchanged.wav";
 InputDispatcher d(s,host,files);int64_t now=1000;
 const auto custom=files.make_app_folder("Studio")+"/Custom-FX.json";assert(files.write_file(custom,"USER CUSTOM DO NOT REPLACE"));
 auto open=[&](bool effect){s.studioOpen=true;s.studioTab=effect?1:2;s.studioRow=effect?10:14;s.studioAUsed=false;studio_button({Button::A,ButtonAction::RELEASED,{}},s,host,*engine,files);assert(s.pendingStudioPresetBrowser);d.set_now(now+=20);assert(!s.studioOpen&&s.currentScreen==ScreenType::FILE_BROWSER);assert(s.browserPurpose==(effect?AppState::BrowserPurpose::LOAD_STUDIO_FX:AppState::BrowserPurpose::LOAD_STUDIO_MASTER));};
 host.play_song(0);assert(host.is_playing());
 const auto initialMaster=studio::master_preset(43);p.studioMaster.assign(initialMaster.begin(),initialMaster.end());host.push_globals();const auto masterBefore=p.studioMaster;
 open(true);const auto effectFile=fs::absolute(factory/"StudioFX/Extreme_Collection/13_Granular/Small_Grain_Veil.ptfx").string();navigate_to_folder(s.fileBrowser,files,fs::path(effectFile).parent_path().string());ppm(out/"studio-fx-library.ppm",s);choose(d,s,effectFile);
 assert(s.studioOpen&&host.is_playing()&&s.studioTab==1);auto effectValues=studio::defaults;assert(parse_studio_preset(true,read(effectFile),effectValues));
 for(int i=0;i<studio::COUNT;++i){assert(p.studioMaster[i]==(studio_preset_key(true,i)?effectValues[i]:masterBefore[i]));assert(engine->studioRack.control[i].load()==p.studioMaster[i]);}
 s.studioRow=10;ppm(out/"fx-library-loaded.ppm",s);
 const auto fxBefore=p.studioMaster;open(false);const auto masterFile=fs::absolute(factory/"Mastering/Extreme_Collection/02_Drum_Buses/Organic_Drum_Glue.ptmaster").string();navigate_to_folder(s.fileBrowser,files,fs::path(masterFile).parent_path().string());choose(d,s,masterFile);
 assert(s.studioOpen&&host.is_playing()&&s.studioTab==2);auto masterValues=studio::defaults;assert(parse_studio_preset(false,read(masterFile),masterValues));
 for(int i=0;i<studio::COUNT;++i){assert(p.studioMaster[i]==(studio_preset_key(false,i)?masterValues[i]:fxBefore[i]));assert(engine->studioRack.control[i].load()==p.studioMaster[i]);}
 s.studioRow=14;ppm(out/"master-library-loaded.ppm",s);
 const auto before=songcore::serialize_project(p);
 open(true);d.on_button_b();assert(s.studioOpen&&s.studioTab==1&&songcore::serialize_project(p)==before&&host.is_playing());
 const auto dir=files.make_app_folder("Presets/StudioFX/Invalid");
 const std::vector<std::string> bad={"not json","{}",read(masterFile),"{\"version\":1,\"kind\":\"effects\",\"master\":[1]}","{\"version\":2,\"kind\":\"effects\",\"master\":[1,0,0,128,128,32,0,255,0,32,96,128,128,250,0]}","{\"version\":1,\"kind\":\"effects\",\"master\":[1,0,0,128,128,32,0,18446744073709551615,0,32,96,128,128,250,0]}"};
 for(size_t i=0;i<bad.size();++i){const auto path=dir+"/Bad"+std::to_string(i)+".ptfx";assert(files.write_file(path,bad[i]));open(true);navigate_to_folder(s.fileBrowser,files,dir);choose(d,s,path);assert(s.currentScreen==ScreenType::FILE_BROWSER&&!s.fileBrowser.statusSuccess);assert(songcore::serialize_project(p)==before&&host.is_playing());d.on_button_b();}
 assert(read(custom)=="USER CUSTOM DO NOT REPLACE");assert(s.settings.lastSampleFolder=="unchanged-sample-folder"&&s.settings.lastSampleSelection=="unchanged.wav");
 // Legacy custom files lacking kind still load through their original fixed user-slot action.
 const auto legacy=files.make_app_folder("Studio")+"/Custom-Master.json";json old={{"version",1},{"master",studio::master_preset(34)}};assert(files.write_file(legacy,old.dump()));s.studioOpen=true;s.studioTab=2;s.studioRow=13;s.studioAUsed=false;studio_button({Button::A,ButtonAction::RELEASED,{}},s,host,*engine,files);assert(s.studioMessage=="CUSTOM PRESET LOADED");
 host.stop();
}
}
int main(int argc,char**argv){
 assert(argc==3||(argc==4&&std::string(argv[3])=="--ui-only"));const bool uiOnly=argc==4;
 const fs::path factory=fs::absolute(argv[1]),out=fs::absolute(argv[2]);fs::create_directories(out);fs::create_directories(out/"Auditions");
 assert(kReverbPresetCount==84&&kDelayPresetCount==84&&studio::effect_preset_count()==256&&studio::master_preset_count()==64);
 legacy_presets();
 auto manifest=json::parse(read(factory.parent_path()/"effect-catalog.json"));assert(manifest["files"].size()==256);
 StdFileSystem filesystem(out.string());int reverbFiles=0,delayFiles=0,rackFiles=0,masterFiles=0;
 for(const auto& entry:manifest["files"]){const auto path=factory.parent_path()/entry["path"].get<std::string>();const auto text=read(path);const auto kind=entry["kind"].get<std::string>();
  if(kind=="reverbs"){ReverbCells v;assert(load_reverb_file(filesystem,path.string(),v));auto p=songcore::make_default_project();apply_reverb_cells(p,v);const auto back=serialize_reverb_cells(reverb_cells_of(p));ReverbCells rt;assert(parse_reverb_cells(back,"",rt));assert(back==serialize_reverb_cells(rt));assert(reverb_bank_match(p)>=20);++reverbFiles;}
  else if(kind=="delays"){DelayCells v;assert(load_delay_file(filesystem,path.string(),v));auto p=songcore::make_default_project();apply_delay_cells(p,v);const auto back=serialize_delay_cells(delay_cells_of(p));DelayCells rt;assert(parse_delay_cells(back,"",rt));assert(back==serialize_delay_cells(rt));assert(delay_bank_match(p)>=20);++delayFiles;}
  else {const bool fx=kind=="studio_fx";std::array<int,studio::COUNT> v{};assert(parse_studio_preset(fx,text,v));auto rt=studio::defaults;assert(parse_studio_preset(fx,serialize_studio_preset(fx,v),rt)&&v==rt);assert(!parse_studio_preset(!fx,text,rt));(fx?rackFiles:masterFiles)++;}
 }
 assert(reverbFiles==64&&delayFiles==64&&rackFiles==96&&masterFiles==32);
 if(uiOnly){ui(factory,out);std::cout<<"PASS: all256 file roundtrips plus live library/group/cancel/malformed/custom-slot fixtures.\n";return 0;}
 // Module fixtures use the same per-thread denormal policy as the shipping callback.
 {auto warm=std::make_unique<AudioEngine>();std::array<float,block*2> zero{};warm->processLiveBlock(zero.data(),block,2,rate);}
 Watchdog watchdog;
 json results=json::object();results["reverbs"]=json::array();results["delays"]=json::array();results["studio_fx"]=json::array();results["masters"]=json::array();
 std::array<float,block> l{},r{},ol{},orr{};std::set<std::array<int,7>> revSignatures,delaySignatures;
 for(int i=20;i<84;++i){watchdog.stage=1000+i;const auto& p=kReverbPresets[i];assert(p.decay<255);assert(revSignatures.insert({p.algo,p.decay,p.room,p.damp,p.pre,p.width,p.mod}).second);
  auto reverb=std::make_unique<ReverbModule>();reverb->reset(rate);reverb->setAlgo(p.algo);reverb->setParams(p.decay,p.damp,p.room);reverb->setCharacter(p.pre,p.width,p.mod);reverb->dragonfly.prepare(p.algo,p.decay,p.room,p.damp,p.mod,rate);
  Stats stats;std::vector<int16_t> audition;const bool save=(i-20)%8==0;
  for(int off=0;off<10*rate;off+=block){source(l.data(),r.data(),off);reverb->process(l.data(),r.data(),ol.data(),orr.data(),block);for(int k=0;k<block;++k){stats.add(ol[k],orr[k],off+k);if(save&&off<4*rate){audition.push_back(int16_t(std::clamp(l[k]+ol[k]*.7f,-.99f,.99f)*32767));audition.push_back(int16_t(std::clamp(r[k]+orr[k]*.7f,-.99f,.99f)*32767));}}}
  assert(stats.energy>1e-8&&stats.peak<1);assert(stats.late/2<=stats.early/3*1.6+1e-7);
  auto j=stats.result();j["name"]=p.name;j["id"]=i;results["reverbs"].push_back(j);if(save)wav(out/"Auditions"/(std::string("Reverb-")+p.category+".wav"),audition);
 }
 std::cerr<<"PASS: 64 new reverb DSP paths and decaying tails.\n";
 for(int i=20;i<84;++i){watchdog.stage=2000+i;const auto& p=kDelayPresets[i];assert(p.feedback<224);assert(delaySignatures.insert({p.pong,p.sync,p.time,p.feedback,p.tone,p.wobble,p.reverbSend}).second);
  auto delay=std::make_unique<DelayModule>();delay->reset(rate);delay->setParamsFree(p.time,p.feedback);if(p.sync)delay->setTimeSync(p.time,120);delay->setCharacter(p.pong,p.tone,p.wobble);
  Stats stats;std::vector<int16_t> audition;const bool save=(i-20)%8==0;
  for(int off=0;off<10*rate;off+=block){source(l.data(),r.data(),off);delay->process(l.data(),r.data(),ol.data(),orr.data(),block);for(int k=0;k<block;++k){stats.add(ol[k],orr[k],off+k);if(save&&off<4*rate){audition.push_back(int16_t(std::clamp(l[k]+ol[k]*.7f,-.99f,.99f)*32767));audition.push_back(int16_t(std::clamp(r[k]+orr[k]*.7f,-.99f,.99f)*32767));}}}
  assert(stats.energy>1e-8&&stats.peak<1);assert(stats.late/2<=stats.early/3*1.6+1e-7);
  auto j=stats.result();j["name"]=p.name;j["id"]=i;results["delays"].push_back(j);if(save)wav(out/"Auditions"/(std::string("Delay-")+p.category+".wav"),audition);
 }
 std::cerr<<"PASS: 64 new delay DSP paths and decaying tails.\n";
 for(bool effect:{true,false}){std::set<std::array<int,studio::COUNT>> signatures;const int start=effect?160:32,end=effect?256:64;
  for(int id=start;id<end;++id){watchdog.stage=(effect?3000:4000)+id;const auto p=effect?studio::effect_preset(id):studio::master_preset(id);assert(signatures.insert(p).second);studio::Rack rack;for(int k=0;k<studio::COUNT;++k)rack.control[k]=p[k];
   Stats stats;std::array<float,block*2> data{};std::vector<int16_t> audition;const bool save=(id-start)%(effect?6:8)==0;
   for(int off=0;off<4*rate;off+=block){source(l.data(),r.data(),off,true);for(int k=0;k<block;++k){data[k*2]=l[k];data[k*2+1]=r[k];}rack.process(data.data(),block,2,rate);for(int k=0;k<block;++k){stats.add(data[k*2],data[k*2+1],off+k);if(save){audition.push_back(int16_t(data[k*2]*32767));audition.push_back(int16_t(data[k*2+1]*32767));}}}
   if(!(stats.energy>1e-5&&stats.peak<1))std::fprintf(stderr,"FAILED: %s%d energy=%.12g peak=%.12g\n",effect?"FX ":"Master ",id,stats.energy,stats.peak);
   assert(stats.energy>1e-5&&stats.peak<1);auto j=stats.result();j["id"]=id;j["name"]=effect?studio::effect_preset_name(id):std::string(studio::master_preset_name(id));results[effect?"studio_fx":"masters"].push_back(j);
   if(save)wav(out/"Auditions"/((effect?"StudioFX-":"Master-")+std::to_string(id)+".wav"),audition);
   std::cerr<<"Checked "<<(effect?"FX ":"Master ")<<id<<"\n";
  }
 }
 watchdog.stage=5000;ui(factory,out);results["ok"]=true;results["new_presets"]=256;results["files_roundtrip"]=true;results["group_preservation_and_live_push"]=true;results["wrong_kind_malformed_and_cancel_preserve_mix"]=true;results["legacy_custom_slot_and_sample_memory_preserved"]=true;results["native_DSP"]="48 kHz, stereo, production denormal policy, signal bursts plus decaying tails; all finite, nonzero, peak below full scale";
 std::ofstream f(out/"effect-collection-qa.json");f<<results.dump(2)<<"\n";assert(f.good());std::cout<<"PASS: 256 authored preset files, native bank match, roundtrip, categorized Studio browser/load/cancel, malformed/wrong-kind rejection, independent groups/live push; all256 native stereo48kDSP renders finite/audible/bounded,128send tails decay.\n";
}
