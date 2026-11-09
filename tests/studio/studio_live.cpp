#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/studio.h"
#include "ui/std_filesystem.h"
#include "studio/synth_params.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
using namespace pt::ui;
namespace {
void word(std::ofstream& f,uint16_t v){char x[]={char(v),char(v>>8)};f.write(x,2);}
void dword(std::ofstream& f,uint32_t v){char x[]={char(v),char(v>>8),char(v>>16),char(v>>24)};f.write(x,4);}
void wav(const std::filesystem::path& path){
 std::ofstream f(path,std::ios::binary);f.write("RIFF",4);dword(f,36+4096*2);f.write("WAVEfmt ",8);dword(f,16);
 word(f,1);word(f,1);dword(f,48000);dword(f,96000);word(f,2);word(f,16);f.write("data",4);dword(f,4096*2);
 for(int i=0;i<4096;++i)word(f,static_cast<uint16_t>(static_cast<int16_t>(std::sin(i*.061)*14000)));
}
double render(AudioEngine& engine,songcore::SongcoreHost& host,int blocks){
 double energy=0;float pcm[512*2]{};
 for(int i=0;i<blocks;++i){host.poll();engine.processLiveBlock(pcm,512,2,48000);for(float v:pcm){assert(std::isfinite(v));energy+=double(v)*v;}}
 return energy;
}
ButtonMods editmods(bool heldAudition=false){ButtonMods m;m.a=true;m.l=heldAudition;return m;}
void edit(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs,int row,Button direction=Button::DPAD_RIGHT){
 s.studioRow=row;
 assert(studio_button({direction,ButtonAction::PRESSED,editmods(s.studioAuditionHeld)},s,host,engine,fs));
 assert(studio_button({Button::A,ButtonAction::RELEASED,{}},s,host,engine,fs));
}
void hold(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs,bool on){
 ButtonMods m;m.l=on;assert(studio_button({Button::L_SHIFT,on?ButtonAction::PRESSED:ButtonAction::RELEASED,m},s,host,engine,fs));
}
}
int main(int argc,char**argv){
 assert(argc==2);const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
 StdFileSystem fs((out/"app-data").string());auto engine=std::make_unique<AudioEngine>();engine->setDeviceSampleRate(48000);
 songcore::SongcoreHost host(engine.get(),48000);auto project=songcore::make_default_project();
 project.tracks[0].chainRefs={0};project.chains[0].phraseRefs[0]=0;
 project.phrases[0].steps[0].note=songcore::Note::C4();project.phrases[0].steps[0].instrument=7;
 auto& source=project.instruments[7];source.sampleFilePath=studio::uri(studio::preset_code(0,0));source.loopMode="fwd";source.name="Named lead";source.volume=170;
 source.modSlots[0].type=songcore::ModType::ADSR;source.modSlots[0].dest=songcore::ModDest::VOLUME;
 source.modSlots[0].attack=0;source.modSlots[0].decay=0;source.modSlots[0].sustain=255;source.modSlots[0].release=4;
 assert(host.push_project(songcore::serialize_project(project)));assert(host.load_media("").failed==0);host.push_params();render(*engine,host,2);
 AppState s;s.project=&host.edit_project();s.currentInstrument=7;open_studio_synth(s,host.project().instruments[7]);
 assert(studio_synth_live(s));host.play_song(0);assert(host.is_playing());assert(render(*engine,host,16)>1e-7);
 const auto frameBefore=engine->getCurrentFrame();const auto version=s.projectVersion;
 // An edit publishes to the sounding source and document immediately, without Load or transport restart.
 edit(s,host,*engine,fs,3);const auto custom=studio::custom_uri(0,s.studioHarmonics,s.studioTimbre,s.studioMorph);
 assert(*host.project().instruments[7].sampleFilePath==custom&&engine->studioCodes[7].load()==studio::uri_code(custom.c_str()));
 assert(s.projectVersion>version&&host.is_playing()&&studio_synth_live(s));assert(host.project().instruments[7].name=="Named lead");assert(render(*engine,host,8)>1e-7);
 assert(engine->getCurrentFrame()>frameBefore&&engine->getActiveVoiceCount()>0);
 edit(s,host,*engine,fs,2);assert(!s.studioSynthCustom&&*host.project().instruments[7].sampleFilePath==studio::uri(studio::preset_code(0,1)));
 edit(s,host,*engine,fs,1);assert(s.studioModel==1&&host.is_playing());assert(render(*engine,host,8)>1e-7);
 // Model-specific parameters are editable and saved in both songs and instrument presets.
 assert(studio::synth_param_count(s.studioModel)>0);s.studioSynthPage=1;
 edit(s,host,*engine,fs,3);assert(s.studioDraft.studioSynthParams[0]>=0);
 assert(host.project().instruments[7].studioSynthParams==s.studioDraft.studioSynthParams);
 const int extraPages=(studio::synth_param_count(s.studioModel)+2)/3;
 s.studioSynthPage=extraPages+1;edit(s,host,*engine,fs,3);assert(host.project().instruments[7].filterType=="lp");
 edit(s,host,*engine,fs,4,Button::DPAD_LEFT);assert(host.project().instruments[7].filterCut==s.studioDraft.filterCut);
 s.studioSynthPage=extraPages+2;const int volume=s.studioDraft.volume;edit(s,host,*engine,fs,3);assert(host.project().instruments[7].volume==volume+1);
 s.studioSynthPage=extraPages+3;edit(s,host,*engine,fs,3);assert(host.project().instruments[7].modSlots[0].attack==1);
 const auto stored=songcore::serialize_project(host.project());const auto roundtrip=songcore::parse_project(songcore::json::parse(stored));
 assert(roundtrip.instruments[7].studioSynthParams==s.studioDraft.studioSynthParams);
 songcore::InstrumentPreset preset;preset.instrument=host.project().instruments[7];
 const auto restored=songcore::parse_instrument_preset(songcore::json::parse(songcore::serialize_instrument_preset(preset)));
 assert(restored.instrument.studioSynthParams==preset.instrument.studioSynthParams);
 auto legacy=songcore::json::parse(stored);legacy["instruments"][7].erase("studioSynthParams");
 assert(songcore::parse_project(legacy).instruments[7].studioSynthParams==studio::default_synth_params());
 host.stop();render(*engine,host,32);

 // The visible arrangement determines focus: horizontal peers never move on Up/Down.
 s.studioSynthPage=0;s.studioModel=0;s.studioRow=3;s.studioSynthColumn=0;
 studio_synth_navigate(s,Button::DPAD_RIGHT);assert(s.studioRow==4);
 studio_synth_navigate(s,Button::DPAD_RIGHT);assert(s.studioRow==5);
 studio_synth_navigate(s,Button::DPAD_DOWN);assert(s.studioRow==6);
 studio_synth_navigate(s,Button::DPAD_UP);assert(s.studioRow==5);
 studio_synth_navigate(s,Button::DPAD_UP);assert(s.studioRow==2);
 studio_synth_navigate(s,Button::DPAD_DOWN);assert(s.studioRow==5);
 s.studioRow=0;studio_synth_navigate(s,Button::DPAD_RIGHT);assert(s.studioRow==1);
 studio_synth_navigate(s,Button::DPAD_DOWN);assert(s.studioRow==2);
 studio_synth_navigate(s,Button::DPAD_LEFT);assert(s.studioRow==2);
 s.studioModel=24;s.studioRow=3;s.studioSynthColumn=0;assert(studio_synth_control_count(s)==2);
 studio_synth_navigate(s,Button::DPAD_RIGHT);assert(s.studioRow==4);
 studio_synth_navigate(s,Button::DPAD_RIGHT);assert(s.studioRow==4);
 studio_synth_navigate(s,Button::DPAD_DOWN);assert(s.studioRow==6);
 studio_synth_navigate(s,Button::DPAD_UP);assert(s.studioRow==4);
 s.studioModel=0;s.studioSynthPage=(studio::synth_param_count(0)+2)/3+1;s.studioRow=3;s.studioSynthColumn=0;
 assert(studio_synth_control_count(s)==4);
 for(int i=0;i<3;++i)studio_synth_navigate(s,Button::DPAD_RIGHT);assert(s.studioRow==6);
 studio_synth_navigate(s,Button::DPAD_DOWN);assert(s.studioRow==7);
 studio_synth_navigate(s,Button::DPAD_UP);assert(s.studioRow==6);

 // Draft audition uses an isolated source. Existing sample data, parameters and the project stay
 // byte-identical while a fundamental rings and the draft macros are changed around it.
 const auto samplePath=out/"owned-sample.wav";wav(samplePath);assert(host.load_sample(9,samplePath.string()));
 host.edit_project().instruments[9].name="Keep original sample";host.edit_project().instruments[9].drive=23;
 s.currentInstrument=9;open_studio_synth(s,host.project().instruments[9]);assert(!studio_synth_live(s));
 const auto beforeDraft=songcore::serialize_project(host.project());const auto draftVersion=s.projectVersion;
 hold(s,host,*engine,fs,true);assert(s.studioAuditionHeld);assert(render(*engine,host,24)>1e-7);
 assert(engine->getActiveVoiceCount()>0);edit(s,host,*engine,fs,3);
 assert(s.projectVersion==draftVersion&&songcore::serialize_project(host.project())==beforeDraft&&engine->studioCodes[9].load()<0);
 assert(render(*engine,host,8)>1e-7);hold(s,host,*engine,fs,false);assert(!s.studioAuditionHeld);
 render(*engine,host,16);assert(engine->getActiveVoiceCount()==0);
 // Leaving the page or using the transport cannot strand a held audition.
 hold(s,host,*engine,fs,true);render(*engine,host,4);ButtonMods page;page.b=true;page.l=true;
 studio_button({Button::DPAD_RIGHT,ButtonAction::PRESSED,page},s,host,*engine,fs);
 assert(s.studioTab==1&&!s.studioAuditionHeld);render(*engine,host,16);assert(engine->getActiveVoiceCount()==0);
 studio_button({Button::B,ButtonAction::RELEASED,{}},s,host,*engine,fs);assert(s.studioOpen&&!s.studioBUsed);
 s.studioTab=0;hold(s,host,*engine,fs,true);render(*engine,host,4);
 assert(!studio_button({Button::START,ButtonAction::PRESSED,{}},s,host,*engine,fs));assert(!s.studioAuditionHeld);
 render(*engine,host,16);assert(engine->getActiveVoiceCount()==0);
 assert(songcore::serialize_project(host.project())==beforeDraft);
 // Replacing a sample still requires explicit confirmation once. All later changes edit live.
 assert(!studio_synth_apply(s,host,*engine)&&s.studioConfirm);
 assert(songcore::serialize_project(host.project())==beforeDraft);
 assert(studio_synth_apply(s,host,*engine)&&studio_synth_live(s));host.stop_preview(true);render(*engine,host,16);
 const auto loaded=*host.project().instruments[9].sampleFilePath;
 edit(s,host,*engine,fs,3);assert(*host.project().instruments[9].sampleFilePath!=loaded&&studio_synth_live(s));
 hold(s,host,*engine,fs,true);render(*engine,host,4);
 studio_button({Button::B,ButtonAction::RELEASED,{}},s,host,*engine,fs);assert(!s.studioOpen&&!s.studioAuditionHeld);
 render(*engine,host,16);assert(engine->getActiveVoiceCount()==0);
 std::puts("PASS: live sequence edits, model/preset/macros, engine parameter persistence, safe held draft audition/release, explicit first load and geometric Synth navigation");
}
