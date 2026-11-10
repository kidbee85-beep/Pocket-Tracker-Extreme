#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> audit{false};std::atomic<int> allocations{0};
}
void* operator new(std::size_t bytes){if(audit.load())++allocations;if(void* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
namespace {
using songcore::json;
constexpr int block=256;
struct Fixture {
 std::unique_ptr<AudioEngine> engine=std::make_unique<AudioEngine>();
 songcore::SongcoreHost host{engine.get(),48000};
 Fixture(int model=103){
  engine->setDeviceSampleRate(48000);auto p=songcore::make_default_project();
  p.tracks[0].chainRefs={0};p.chains[0].phraseRefs[0]=0;
  p.phrases[0].steps[0].note=songcore::Note::C4();p.phrases[0].steps[0].instrument=0;
  auto& ins=p.instruments[0];ins.sampleFilePath=studio::custom_uri(model,50,80,100);ins.loopMode="fwd";ins.volume=170;
  ins.modSlots=std::vector<songcore::ModSlot>(4);
  assert(host.push_project(songcore::serialize_project(p)));assert(host.load_media("").failed==0);host.push_params();
 }
};
std::vector<float> render(AudioEngine& engine,int blocks,bool noAllocation=false){
 std::vector<float> out;out.reserve(size_t(blocks*block*2));float data[block*2];
 if(noAllocation){allocations=0;audit=true;}
 for(int i=0;i<blocks;++i){engine.processLiveBlock(data,block,2,48000);for(float sample:data){assert(std::isfinite(sample));out.push_back(sample);}}
 audit=false;if(noAllocation)assert(allocations==0);return out;
}
double energy(const std::vector<float>& v){double e=0;for(float x:v)e+=double(x)*x;return e;}
double difference(const std::vector<float>& a,const std::vector<float>& b){assert(a.size()==b.size());double d=0;for(size_t i=0;i<a.size();++i)d+=std::fabs(a[i]-b[i]);return d;}
int voice(AudioEngine& engine,int track){return engine.studioVoiceState(track).index;}
double channel(const std::vector<float>& values,int ch){double e=0;for(size_t i=ch;i<values.size();i+=2)e+=double(values[i])*values[i];return e;}
double frequency(const std::vector<float>& values){double first=-1,last=-1;int crossings=0;for(size_t i=2;i<values.size();i+=2){double a=values[i-2]+values[i-1],b=values[i]+values[i+1];if(a<=0&&b>0){double where=double(i/2-1)-a/(b-a);if(first<0)first=where;last=where;++crossings;}}assert(crossings>2);return (crossings-1)*48000/(last-first);}
void common(Fixture& f,const songcore::Instrument& updated){auto before=f.host.project().instruments[0];f.host.edit_project().instruments[0]=updated;f.host.push_instrument(0);f.host.update_studio_voice(before,updated);}

}
int main(int argc,char** argv){
 assert(argc==2);std::filesystem::create_directories(argv[1]);json report;
 // The sequencer has one long note. A macro update must alter THAT ringing note without a
 // replacement sample, a fresh trigger, transport reset or a hidden preview note.
 Fixture changed,control;changed.host.play_song(0);control.host.play_song(0);changed.host.poll();control.host.poll();
 auto before=render(*changed.engine,8);auto twin=render(*control.engine,8);assert(before==twin&&energy(before)>1e-4);
 const int index=voice(*changed.engine,0);assert(index>=0);
 const auto originalVoice=changed.engine->studioVoiceState(0);
 const int oldCode=changed.engine->studioCodes[0].load();auto parameters=studio::default_synth_params();
 assert(changed.host.update_studio_synth(0,studio::custom_uri(103,230,220,10),parameters));
 auto after=render(*changed.engine,8,true);auto unchanged=render(*control.engine,8);
 assert(difference(after,unchanged)>1);assert(changed.host.is_playing());
 const auto& updated=changed.engine->studioVoiceState(0);
 assert(updated.active&&updated.generation==originalVoice.generation&&updated.note==originalVoice.note&&updated.track==0);
 assert(voice(*changed.engine,AudioEngine::PREVIEW_LANE)<0);
 assert(changed.engine->studioVoices[index].code()!=oldCode);
 assert(changed.host.project().instruments[0].sampleFilePath==studio::custom_uri(103,230,220,10));
 report["held_sequencer_note_changed_without_retrigger"]=true;
 // The 16-control snapshots reach the same active voice, and persist in the ordinary project.
 parameters[0]=0;parameters[1]=180;
 assert(changed.host.update_studio_synth(0,studio::custom_uri(103,230,220,10),parameters));
 auto shaped=render(*changed.engine,8,true);assert(energy(shaped)>0);
 auto parsed=songcore::parse_project(json::parse(songcore::serialize_project(changed.host.project())));
 assert(parsed.instruments[0].studioSynthParams==parameters);
 report["advanced_control_persistence"]=true;
 // A real low-pass edit attenuates this held waveform. Both deterministic instances retain the
 // identical oscillator phase, so this measures the control rather than a later note onset.
 studio::Synth dry,filtered;const int vectorCode=studio::custom_code(103,230,220,10);
 dry.start(vectorCode);filtered.start(vectorCode);
 for(int i=0;i<2048;++i)assert(dry.next(440,48000)==filtered.next(440,48000));
 auto low=studio::default_synth_params();low[0]=0;filtered.update(vectorCode,low);
 double dryPower=0,lowPower=0;
 for(int i=0;i<8192;++i){const float a=dry.next(440,48000),b=filtered.next(440,48000);if(i>1024){dryPower+=a*a;lowPower+=b*b;}}
 assert(dryPower>1&&lowPower<dryPower*.25);report["live_tone_control_attenuation"]=lowPower/dryPower;
 studio::Synth quantized;const int braidsCode=studio::custom_code(24,140,170,0);
 quantized.start(braidsCode);for(int i=0;i<1024;++i)quantized.next(440,48000);
 auto crushed=studio::default_synth_params();crushed[0]=0;quantized.update(braidsCode,crushed);
 for(int i=0;i<512;++i)quantized.next(440,48000);
 for(int i=0;i<1024;++i){const float sample=quantized.next(440,48000)*16;assert(std::fabs(sample-std::round(sample))<1e-5f);}
 report["braids_live_bit_depth_quantization"]=true;
 // Loading a different engine is still a live operation on the same note, with a bounded smooth
 // transition. All renderer families are exercised while that voice and generation survive.
 for(int model:{0,24,72,82,94,102}){
  auto defaults=studio::default_synth_params();
  assert(changed.host.update_studio_synth(0,studio::custom_uri(model,130,160,190),defaults));
  render(*changed.engine,4,true);
  assert(changed.engine->studioVoiceState(0).generation==originalVoice.generation);
  assert(changed.engine->studioVoiceState(0).note==originalVoice.note&&changed.host.is_playing());
 }
 report["live_model_families"]=6;
 changed.host.stop();control.host.stop();render(*changed.engine,8);render(*control.engine,8);
 // A held draft audition uses slot255 and cannot change a PCM/SoundFont/empty project slot or
 // overwrite program127. Updates keep its own root and gate; releasing ends just this lane.
 Fixture preview(103);float pcm[64];for(int i=0;i<64;++i)pcm[i]=std::sin(i*.1f);
 assert(preview.engine->loadSample(5,pcm,64));preview.host.edit_project().instruments[5].name="Preserve PCM";
 preview.host.edit_project().instruments[127].name="Preserve program127";
 const auto document=songcore::serialize_project(preview.host.project());
 songcore::Instrument draft(5);draft.root={7,3};draft.loopMode="fwd";draft.volume=180;
 draft.modSlots=std::vector<songcore::ModSlot>(4);draft.studioSynthParams[0]=210;
 assert(preview.host.preview_studio_synth(draft,studio::custom_uri(103,70,50,10),true));
 auto audible=render(*preview.engine,10,true);assert(energy(audible)>1e-4);
 const int pv=voice(*preview.engine,AudioEngine::PREVIEW_LANE);assert(pv>=0);
 assert(preview.engine->studioVoiceState(AudioEngine::PREVIEW_LANE).sampleId==255&&preview.engine->studioVoiceState(AudioEngine::PREVIEW_LANE).note==43);
 const auto previewGeneration=preview.engine->studioVoiceState(AudioEngine::PREVIEW_LANE).generation;
 assert(preview.host.preview_studio_synth(draft,studio::custom_uri(103,170,240,110),false));
 render(*preview.engine,6,true);assert(preview.engine->studioVoiceState(AudioEngine::PREVIEW_LANE).generation==previewGeneration);
 assert(songcore::serialize_project(preview.host.project())==document&&preview.engine->getSampleLength(5)==64);
 preview.host.stop_preview(true);render(*preview.engine,8,true);assert(voice(*preview.engine,AudioEngine::PREVIEW_LANE)<0);
 preview.host.play_song(0);preview.host.poll();
 assert(preview.host.preview_studio_synth(draft,studio::custom_uri(103,70,50,10),true));
 preview.host.stop_preview(true);auto shortTap=render(*preview.engine,8,true);
 assert(voice(*preview.engine,AudioEngine::PREVIEW_LANE)<0&&preview.host.is_playing()&&energy(shortTap)>1e-4);
 preview.host.stop();render(*preview.engine,8);
 report["draft_audition_project_untouched_and_release"]=true;
 // Illegal/unloaded/PCM sources never turn into synths through the live-update shortcut.
 assert(!preview.host.update_studio_synth(5,studio::uri(0),parameters));
 assert(!preview.host.update_studio_synth(-1,studio::uri(0),parameters));
 assert(!preview.host.update_studio_synth(0,"/not/a/synth.wav",parameters));
 auto invalid=parameters;invalid[0]=256;assert(!preview.host.update_studio_synth(0,studio::uri(0),invalid));
 assert(songcore::serialize_project(preview.host.project())==document);
 // One UI publisher can stream complete parameter records while the callback consumes them.
 // No callback heap allocation is needed for updates or for model/parameter switching.
 Fixture concurrent(103);concurrent.host.preview_instrument(0);render(*concurrent.engine,4);
 std::atomic<bool> start{false};std::thread writer([&]{while(!start.load(std::memory_order_acquire))std::this_thread::yield();for(int i=0;i<4000;++i){auto p=studio::default_synth_params();p[0]=i%256;p[1]=(i*3)%256;assert(concurrent.engine->setLiveStudioCode(0,studio::custom_code(103,i%256,(i*5)%256,(i*7)%256)));concurrent.engine->setStudioSynthParams(0,p);}});
 start.store(true,std::memory_order_release);render(*concurrent.engine,80,true);writer.join();
 assert(voice(*concurrent.engine,AudioEngine::PREVIEW_LANE)>=0);concurrent.host.stop_preview(true);render(*concurrent.engine,8);
 report["concurrent_parameter_publications"]=4000;
 // Parameter names/defaults are finite, model-dependent and stay in the stored byte domain.
 for(int model=0;model<studio::model_count();++model){assert(studio::synth_param_count(model)>0);for(int p=0;p<studio::synth_param_count(model);++p){assert(*studio::synth_param_name(model,p));assert(studio::synth_param_default(model,p)>=0&&studio::synth_param_default(model,p)<=255);}}
 assert(studio::synth_param_adjust(72,1,-1,1,false)==128);
 assert(studio::synth_param_adjust(72,1,128,1,false)==255);
 assert(studio::synth_param_adjust(72,1,255,-1,false)==128);
 assert(studio::synth_param_value(24,0,studio::synth_param_adjust(24,0,-1,-1,false))=="15 bit");
 assert(studio::synth_param_value(24,1,studio::synth_param_adjust(24,1,-1,-1,false))=="48.0 kHz");
 assert(studio::synth_param_value(76,3,studio::synth_param_adjust(76,3,-1,1,false))=="Chord 2");
 // Performance controls must change actual held audio, not just the document or next note.
 Fixture performance(103);assert(performance.host.load_sample(0,studio::custom_uri(103,0,0,0)));performance.host.push_instrument(0);
 performance.host.preview_instrument(0);render(*performance.engine,12);
 const auto perfVoice=performance.engine->studioVoiceState(AudioEngine::PREVIEW_LANE);
 const double initialHz=frequency(render(*performance.engine,32));
 auto edit=performance.host.project().instruments[0];edit.volume=0;common(performance,edit);render(*performance.engine,4);
 assert(energy(render(*performance.engine,8,true))<1e-12);
 edit.volume=170;common(performance,edit);render(*performance.engine,4);assert(energy(render(*performance.engine,8,true))>1e-3);
 edit.pan=0;common(performance,edit);render(*performance.engine,4);auto left=render(*performance.engine,8,true);assert(channel(left,0)>channel(left,1)*1000);
 edit.pan=255;common(performance,edit);render(*performance.engine,4);auto right=render(*performance.engine,8,true);assert(channel(right,1)>channel(right,0)*1000);
 edit.detune=224;common(performance,edit);render(*performance.engine,4);const double tunedHz=frequency(render(*performance.engine,32));assert(tunedHz/initialHz>1.40&&tunedHz/initialHz<1.43);
 auto& envelope=edit.modSlots[0];envelope.type=songcore::ModType::ADSR;envelope.dest=songcore::ModDest::VOLUME;envelope.attack=0;envelope.decay=0;envelope.sustain=64;envelope.amount=255;
 common(performance,edit);render(*performance.engine,4);const double lowSustain=energy(render(*performance.engine,16,true));
 envelope.sustain=255;common(performance,edit);render(*performance.engine,4);const double fullSustain=energy(render(*performance.engine,16,true));assert(fullSustain>lowSustain*10);
 const auto afterPerformance=performance.engine->studioVoiceState(AudioEngine::PREVIEW_LANE);
 assert(afterPerformance.active&&afterPerformance.index==perfVoice.index&&afterPerformance.generation==perfVoice.generation&&afterPerformance.note==perfVoice.note);
 performance.host.stop_preview(true);render(*performance.engine,8);performance.host.preview_instrument(0);render(*performance.engine,8);
 const double nextHz=frequency(render(*performance.engine,32));assert(std::fabs(nextHz-tunedHz)<2);
 performance.engine->scheduleTrackPhraseVol(performance.engine->getCurrentFrame(),AudioEngine::PREVIEW_LANE,0);render(*performance.engine,4);
 edit.volume=85;common(performance,edit);render(*performance.engine,4);assert(energy(render(*performance.engine,8,true))<1e-12);
 performance.host.stop_preview(true);render(*performance.engine,8);
 Fixture initialZero(103);auto zero=initialZero.host.project().instruments[0];zero.volume=0;initialZero.host.edit_project().instruments[0]=zero;initialZero.host.push_instrument(0);
 initialZero.host.preview_instrument(0);render(*initialZero.engine,8);assert(energy(render(*initialZero.engine,8))<1e-12);
 zero.volume=170;common(initialZero,zero);render(*initialZero.engine,4);assert(energy(render(*initialZero.engine,8,true))>1e-3);initialZero.host.stop_preview(true);render(*initialZero.engine,8);
 report["held_volume_pan_detune_envelope_audio"]=true;report["initially_zero_volume_recovered"]=true;report["phrase_mute_preserved"]=true;
 report["initial_hz"]=initialHz;report["edited_hz"]=tunedHz;report["next_note_hz"]=nextHz;report["sustain_power_ratio"]=fullSustain/lowSustain;
 report["callback_allocations"]=0;
 std::ofstream(std::filesystem::path(argv[1])/"live-dsp-validation.json")<<report.dump(2)<<'\n';
 std::puts("PASS: live sequencer macros/model/advanced controls without reload, gate/generation preservation, draft root audition isolation, release and callback allocation audit");
}
