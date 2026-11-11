// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "studio/granular_exchange.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <new>
namespace {thread_local bool allocationGuard=false;std::atomic<int> allocations{0};}
void* operator new(std::size_t n){if(allocationGuard)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
namespace {
void u16(std::ofstream& f,uint16_t x){char b[]={char(x),char(x>>8)};f.write(b,2);}void u32(std::ofstream& f,uint32_t x){u16(f,uint16_t(x));u16(f,uint16_t(x>>16));}
void wav(const std::filesystem::path& path,int rate,int frames,int channels){
 std::filesystem::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);assert(f);
 f.write("RIFF",4);u32(f,uint32_t(36+frames*channels*2));f.write("WAVEfmt ",8);u32(f,16);u16(f,1);u16(f,uint16_t(channels));u32(f,uint32_t(rate));u32(f,uint32_t(rate*channels*2));u16(f,uint16_t(channels*2));u16(f,16);f.write("data",4);u32(f,uint32_t(frames*channels*2));
 for(int i=0;i<frames;++i)for(int c=0;c<channels;++c){float v=.4f*std::sin(float(i)*(.023f+c*.011f))+.1f*std::sin(float(i)*.003f);u16(f,uint16_t(int16_t(std::lround(v*32767))));}assert(f);
}
std::vector<float> render(AudioEngine& e,int frames){std::vector<float> out(size_t(frames)*2);for(int at=0;at<frames;at+=256){const int n=std::min(256,frames-at);allocationGuard=true;e.processLiveBlock(out.data()+size_t(at)*2,n,2,48000);allocationGuard=false;}return out;}
double energy(const std::vector<float>& x){double e=0;for(float v:x){assert(std::isfinite(v));e+=double(v)*v;}return e;}
int16_t quant(float x){return int16_t(std::clamp(std::lround(std::clamp(x,-1.f,1.f)*32768.f),-32768L,32767L));}
void schema(){
 auto old=songcore::parse_project(songcore::json::parse(songcore::serialize_project(songcore::make_default_project())));const auto legacy=songcore::serialize_project(old);assert(legacy.find("granular")==std::string::npos);
 auto again=songcore::parse_project(songcore::json::parse(legacy));assert(songcore::serialize_project(again)==legacy);
 auto& i=old.instruments[3];i.sampleFilePath="Samples/Granular/voice.wav";i.granularEnabled=true;i.granularSeed=9123;
 i.granularParams.position=.1234567f;i.granularParams.sprayMs=73.25f;i.granularParams.freeze=true;i.granularParams.scanSpeed=-.125f;
 const auto serialized=songcore::serialize_project(old);again=songcore::parse_project(songcore::json::parse(serialized));
 assert(again.instruments[3].sampleFilePath==i.sampleFilePath&&again.instruments[3].granularEnabled&&again.instruments[3].granularSeed==9123);
 assert(studio::granular::parametersEqual(again.instruments[3].granularParams,i.granularParams));assert(songcore::serialize_project(again)==serialized);
 const auto preset=songcore::serialize_instrument_preset(songcore::make_instrument_preset(old,3));
 const auto ip=songcore::parse_instrument_preset(songcore::json::parse(preset));assert(ip.instrument.sampleFilePath==i.sampleFilePath&&ip.instrument.granularEnabled);
 assert(!songcore::detail::valid_granular_object(songcore::json{{"enabled",true},{"densityHz",999}}));
 std::cout<<"PASS legacy-default-byte-roundtrip and portable-granular-project-preset-roundtrip\n";
}
struct Fixture {
 std::unique_ptr<AudioEngine> e=std::make_unique<AudioEngine>();songcore::SongcoreHost host{e.get(),48000};
 explicit Fixture(const std::string& source,bool granular=true){
  e->setDeviceSampleRate(48000);auto p=songcore::make_default_project();
  auto& i=p.instruments[0];i.sampleFilePath=source;i.loopMode="fwd";i.granularEnabled=granular;i.granularSeed=23;
  p.tracks[0].chainRefs={0};p.chains[0].phraseRefs[0]=0;p.phrases[0].steps[0].note=songcore::Note::C4();p.phrases[0].steps[0].instrument=0;
  assert(host.push_project(songcore::serialize_project(p)));assert(host.load_media("").failed==0);host.push_params();
 }
};
void live(const std::filesystem::path& root,const std::string& source){
 Fixture f(source);auto initial=f.host.sample_granular_snapshot(0);assert(initial.sourceValid&&initial.sourceFrames==88200&&initial.sourceRate>44099&&initial.sourceRate<44101);
 f.host.preview_instrument(0);auto heard=render(*f.e,8192);assert(energy(heard)>1);
 auto before=f.host.sample_granular_snapshot(0,AudioEngine::PREVIEW_LANE);assert(before.active&&before.voiceSlot==SAMPLE_PREVIEW_VOICE&&before.trackId==8&&before.activeGrains>0);
 assert(std::fabs(before.readIncrement-.91875)<.001);
 const auto path=f.host.project().instruments[0].sampleFilePath;auto settings=f.host.project().instruments[0].granularParams;settings.pitchSemitones=12;settings.sprayMs=50;
 assert(f.host.apply_sample_granular_settings(0,settings));assert(f.host.project().instruments[0].sampleFilePath==path);
 heard=render(*f.e,8192);auto after=f.host.sample_granular_snapshot(0,8);assert(after.active&&after.sourceGeneration==before.sourceGeneration&&after.renderedFrames>before.renderedFrames&&after.spawned>=before.spawned);
 assert(std::fabs(after.readIncrement-1.8375)<.002&&energy(heard)>1);
 assert(f.e->liveResampler.start(48000,4096));heard=render(*f.e,4096);f.e->liveResampler.stop();assert(f.e->liveResampler.frames()==4096);
 const auto& pcm=f.e->liveResampler.pcm();for(size_t n=0;n<heard.size();++n)assert(pcm[n]==quant(heard[n]));assert(energy(heard)>.1);
 auto document=songcore::serialize_project(f.host.project());const auto generation=f.host.sample_granular_snapshot(0).sourceGeneration;
 assert(!f.host.load_sample_granular_patch(0,(root/"absent.wav").string(),settings));assert(songcore::serialize_project(f.host.project())==document);assert(f.host.sample_granular_snapshot(0).sourceGeneration==generation);
 // Complete patch resolves an explicit relative source against its own directory.
 const auto package=root/"portable-patch";std::filesystem::create_directories(package/"Samples");
 std::filesystem::copy_file(source,package/"Samples/material.wav",std::filesystem::copy_options::overwrite_existing);
 auto portable=songcore::make_default_project();auto& pi=portable.instruments[2];pi.sampleFilePath="Samples/material.wav";pi.granularEnabled=true;pi.granularParams=settings;
 const auto presetBytes=songcore::serialize_instrument_preset(songcore::make_instrument_preset(portable,2));
 {std::ofstream presetFile(package/"patch.pti");presetFile<<presetBytes;}
 assert(f.host.load_instrument_preset(2,(package/"patch.pti").string()));
 assert(f.host.project().instruments[2].granularEnabled&&f.e->getSampleLength(2)==88200);
 const auto protectedDocument=songcore::serialize_project(f.host.project());
 auto invalid=songcore::json::parse(presetBytes);invalid["instrument"].erase("sampleFilePath");
 {std::ofstream presetFile(package/"invalid.pti");presetFile<<invalid.dump();}
 assert(!f.host.load_instrument_preset(2,(package/"invalid.pti").string()));assert(songcore::serialize_project(f.host.project())==protectedDocument);
 const auto second=(root/"second.wav").string();wav(second,48000,4800,1);assert(f.host.load_sample_granular_patch(0,second,settings,99));
 auto replaced=f.host.sample_granular_snapshot(0);assert(replaced.sourceGeneration!=generation&&replaced.sourceFrames==4800&&!replaced.active);
 render(*f.e,512);f.host.preview_instrument(0);assert(energy(render(*f.e,4096))>0);
 f.e->scheduleNoteOff(f.e->getCurrentFrame(),AudioEngine::PREVIEW_LANE);render(*f.e,8192);assert(f.e->getActiveVoiceCount()==0);
 f.host.stop_preview(true);render(*f.e,512);
 f.host.play_song(0);f.host.poll();assert(energy(render(*f.e,8192))>0);
 const auto sequenced=f.host.sample_granular_snapshot(0,0);assert(sequenced.active&&sequenced.trackId==0);
 settings.pitchSemitones=0;settings.freeze=true;
 assert(f.host.update_sample_granular(0,true,settings));assert(energy(render(*f.e,8192))>0);
 const auto editedSong=f.host.sample_granular_snapshot(0,0);
 assert(f.host.is_playing()&&editedSong.active&&editedSong.trackId==0&&editedSong.frozen);
 assert(editedSong.renderedFrames>sequenced.renderedFrames&&editedSong.sourceGeneration==sequenced.sourceGeneration);
 assert(std::fabs(editedSong.readIncrement-1.)<.002);
 f.host.stop();render(*f.e,1024);
 f.host.sample_granular_snapshot(-1);
 assert(f.host.load_sample(0,studio::uri(studio::preset_code(0,0))));assert(!f.host.project().instruments[0].granularEnabled);
 std::cout<<"PASS actual-WAV tracker and audition, live-edits during both, portable-preset load/rollback, post-master recording, source-generation and release\n";
}
void previewCapacity(const std::string& source){
 Fixture f(source,false);auto& second=f.host.edit_project().instruments[1];second.sampleFilePath=source;second.loopMode="fwd";
 assert(f.host.load_sample(1,source));assert(f.host.apply_sample_granular_settings(1,studio::granular::Parameters{}));f.host.push_params();
 constexpr float c4=261.625565f;
 for(int t=0;t<8;++t)f.e->scheduleNote(f.e->getCurrentFrame()+10,0,t,c4,c4*48000.f/44100.f,.4f);
 render(*f.e,2048);assert(f.e->getActiveVoiceCount()==8);
 f.host.sample_granular_snapshot(1);f.host.preview_instrument(1);render(*f.e,4096);
 const auto snapshot=f.host.sample_granular_snapshot(1,8);assert(snapshot.active&&snapshot.voiceSlot==SAMPLE_PREVIEW_VOICE&&f.e->getActiveVoiceCount()==9);
 int notes[9];f.e->getTrackActiveNotes(notes,9);for(int t=0;t<9;++t)assert(notes[t]>=0);
 f.host.stop_preview(true);render(*f.e,4096);assert(f.e->getActiveVoiceCount()==8);assert(!f.host.sample_granular_snapshot(1).active);
 std::cout<<"PASS reserved-ninth-audition-under-eight-held-song-voices\n";
}
void exchange(){
 struct Data{uint32_t id=0;uint32_t inverse=~uint32_t(0);std::array<uint32_t,128> repeated{};};
 studio::granular::Exchange32<Data> mailbox;std::atomic<bool> done{false};std::thread writer([&]{for(uint32_t n=1;n<=12000;++n){Data d;d.id=n;d.inverse=~n;d.repeated.fill(n);mailbox.publish(d);}done=true;});
 Data copy;uint32_t seen=0;int accepted=0;while(!done.load()){if(mailbox.read(copy,seen)){assert(copy.inverse==~copy.id);for(uint32_t value:copy.repeated)assert(value==copy.id);++accepted;}}
 writer.join();mailbox.read(copy,seen);assert(copy.id==12000&&copy.inverse==~copy.id);for(uint32_t value:copy.repeated)assert(value==12000);(void)accepted;
 std::cout<<"PASS atomic32-publication-consistency\n";
}
}
int main(int argc,char** argv){assert(argc==2);const auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);const auto source=(root/"material.wav").string();wav(source,44100,88200,2);schema();exchange();live(root,source);previewCapacity(source);assert(allocations==0);std::cout<<"ALL GRANULAR ENGINE INTEGRATION CHECKS PASS; no callback C++ allocation\n";}
