#include "studio/synth.h"
#include "studio/rack.h"
#include <cstdio>
#include <cmath>
#include <cassert>
#include <vector>
#include <limits>
#include <algorithm>
#include <string>
int main(){
 studio::Synth s;
 assert(studio::model_count()==116);
 assert(std::string(studio::model_name(0))=="P VCF"&&std::string(studio::model_name(81))=="E OMINOUS");
 assert(std::string(studio::model_name(111))=="S WAVEFOLD");
 assert(studio::preset_code(81,3)==327&&studio::preset_code(0,4)==((1<<29)|4));
 for(int model=0;model<studio::model_count();++model)for(int variant=0;variant<studio::variation_count();++variant){
  const int c=studio::preset_code(model,variant);
  if(variant<4)assert(c==model*4+variant);
  assert(studio::preset_model(c)==model&&studio::preset_variation(c)==variant);
  assert(studio::uri_code(studio::uri(c).c_str())==c);s.start(c);double energy=0;
  for(int i=0;i<24000;++i){float v=s.next(220,48000);assert(std::isfinite(v));energy+=v*v;}
  if(energy<1e-8){std::fprintf(stderr,"SILENT %d %s\n",c,studio::preset_name(c).c_str());return 2;}
 }
 // New articulations and oscillators must speak from a cold voice, not from the tail of the
 // preceding patch. Exercise low/high notes and the control endpoints at each output clock.
 for(int model=82;model<studio::model_count();++model)
  for(float rate:{44100.f,48000.f,96000.f})for(float hz:{110.f,440.f})for(int macro:{0,128,255}){
   s.reset();const int code=studio::custom_code(model,macro,macro,macro);
   assert(studio::uri_code(studio::custom_uri(model,macro,macro,macro).c_str())==code);
   s.start(code);double energy=0;float peak=0;
   const int frames=int(rate*.3f);
   for(int i=0;i<frames;++i){const float v=s.next(hz,rate);assert(std::isfinite(v));peak=std::max(peak,std::fabs(v));energy+=v*v;}
   if(energy/frames<1e-8||peak<.002f){std::fprintf(stderr,"COLD SILENT %s %.0fHz %.0fHz macro%d rms%g peak%g\n",studio::model_name(model),hz,rate,macro,std::sqrt(energy/frames),peak);return 3;}
   assert(peak<=2.001f);
  }
 // The vector's sine corner is a reference oscillator for output-clock conversion. Measuring
 // crossings after its onset catches an accidental 32/48/96-kHz model clock assignment.
 for(float rate:{44100.f,48000.f,96000.f})for(float hz:{220.f,440.f}){
  s.reset();s.start(studio::custom_code(103,0,0,0));
  const int skip=int(rate/50),frames=int(rate/2);int crossings=0;float previous=0;
  for(int i=0;i<frames;++i){const float v=s.next(hz,rate);if(i>skip&&previous<=0&&v>0)++crossings;previous=v;}
  const float measured=crossings*rate/(frames-skip);assert(std::fabs(measured-hz)<3.f);
 }
 // Extreme forward and backward orbit sweeps cross several octave wraps.
 // Keep phase through the wrap and leave headroom even with no root anchor.
 for(int rate:{0,255}) {
  s.reset();
  s.start(studio::custom_code(studio::kOctaveOrbitModel,rate,255,0));
  float previous=0.f;
  double energy=0;
  for(int i=0;i<96000;++i){
   const float value=s.next(110.f,48000.f);
   assert(std::isfinite(value)&&std::fabs(value)<.65f);
   if(i>0)assert(std::fabs(value-previous)<.16f);
   energy+=double(value)*value;
   previous=value;
  }
  assert(energy/96000>.005);
 }
 std::puts("PASS Octave Orbit forward/backward wrap continuity and headroom");
 // All three motions make distinct, repeatable recordings from fresh voices.
 // Changing the scale affects subsequent impacts and leaves onset/root intact.
 std::vector<float> previousGravity;
 for(int model=studio::kGravityFirstModel;model<studio::model_count();++model){
  assert(studio::synth_param_count(model)==3);
  assert(std::string(studio::synth_param_name(model,2))=="Pitch scale");
  const int code=studio::custom_code(model,255,185,255);
  std::vector<float> take(64000);
  s.reset();s.start(code);
  for(float& sample:take){sample=s.next(185.f,48000.f);assert(std::isfinite(sample)&&std::fabs(sample)<.69f);}
  assert(take!=previousGravity);
  s.reset();s.start(code);
  for(float sample:take)assert(s.next(185.f,48000.f)==sample);
  s.reset();s.start(code);
  auto params=studio::default_synth_params();params[2]=255;
  s.update(code,params);
  double difference=0;
  for(size_t i=0;i<take.size();++i){
   const float sample=s.next(185.f,48000.f);
   if(i<2048)assert(sample==take[i]);
   difference+=std::abs(sample-take[i]);
  }
  assert(difference>5.);
  previousGravity=std::move(take);
 }
 std::puts("PASS distinct Gravity motions, deterministic replay, scale-quantized variation and safe output");
 // A damped/dispersive Karplus loop must compensate its filter delay, rather than playing
 // progressively flatter as the brightness/dispersion preset changes.
 for(float hz:{220.f,440.f})for(int variant=0;variant<studio::variation_count();++variant){
  s.reset();s.start(studio::preset_code(107,variant));std::vector<float> wave(16000);
  for(auto& v:wave)v=s.next(hz,48000);
  const int period=int(48000/hz);int bestLag=0;double best=-2;
  for(int lag=int(period*.9f);lag<=int(period*1.1f);++lag){
   double cross=0,left=0,right=0;
   for(int i=4000;i<12000;++i){cross+=wave[i]*wave[i+lag];left+=wave[i]*wave[i];right+=wave[i+lag]*wave[i+lag];}
   const double score=cross/std::sqrt(std::max(1e-30,left*right));
   if(score>best){best=score;bestLag=lag;}
  }
  assert(best>.9&&std::fabs(1200*std::log2(48000.0/bestLag/hz))<20);
 }
 // Synthetic grain randomness belongs to the voice. A fresh voice gives a reproducible cloud,
 // independent of which Mutable stochastic model was played before it.
 for(int model=94;model<=101;++model){
  std::vector<float> first(4096);s.reset();s.start(studio::preset_code(model,0));
  for(auto& v:first)v=s.next(220,48000);
  s.reset();s.start(studio::preset_code(model,0));
  for(float v:first)assert(s.next(220,48000)==v);
 }
 s.start(0);assert(s.next(std::numeric_limits<float>::infinity(),48000)==0);
 assert(s.next(220,std::numeric_limits<float>::quiet_NaN())==0);
 // Voice reuse deliberately retains oscillator phase/tails. Repeated triggers of
 // sustained, percussive and resonator engines must still produce finite audio.
 for(int code:{10*4,21*4,(studio::model_count()-1)*4}){
  for(int repeat=0;repeat<8;++repeat){s.start(code);double e=0;
   for(int i=0;i<4800;++i){float v=s.next(220,48000);assert(std::isfinite(v));e+=v*v;}
   assert(e>1e-8);
  }
 }
 studio::Rack rack;std::vector<float> x(960);
 for(int fx=0;fx<20;++fx){rack.control[studio::ENABLE]=1;rack.control[studio::FX]=fx;rack.control[studio::FX_MIX]=128;
  for(int b=0;b<60;++b){for(int i=0;i<960;++i)x[i]=.2f*std::sin((i+b*960)*.04f);rack.process(x.data(),480,2,48000);for(float v:x)assert(std::isfinite(v));}
 }
 // Every authored FX/master preset is in range and produces finite processing output.
 for(int id=0;id<studio::effect_preset_count();++id){
  auto p=studio::effect_preset(id);assert(!studio::effect_preset_name(id).empty());
  for(int k=0;k<studio::COUNT;++k){assert(p[k]>=0&&p[k]<=255);rack.control[k]=p[k];}
  for(int b=0;b<10;++b){for(int i=0;i<960;++i)x[i]=.2f*std::sin((i+b*960)*.04f);rack.process(x.data(),480,2,48000);for(float v:x)assert(std::isfinite(v));}
 }
 for(int id=0;id<studio::master_preset_count();++id){auto p=studio::master_preset(id);assert(*studio::master_preset_name(id));for(int k=0;k<studio::COUNT;++k){assert(p[k]>=0&&p[k]<=255);rack.control[k]=p[k];}rack.process(x.data(),480,2,48000);for(float v:x)assert(std::isfinite(v));}
 // Disabled rack must preserve every sample exactly while retaining pre-roll for punches.
 studio::Rack bypass;std::vector<float> source(960);
 for(int i=0;i<960;++i)source[i]=.2f*std::sin(i*.04f);auto exact=source;
 bypass.process(source.data(),480,2,48000);assert(source==exact);
 for(int punch=1;punch<=24;++punch){
  bypass.punch=punch;
  for(int b=0;b<20;++b){auto block=exact;bypass.process(block.data(),480,2,48000);for(float v:block)assert(std::isfinite(v));}
  bypass.punch=0;
  for(int b=0;b<20;++b){auto block=exact;bypass.process(block.data(),480,2,48000);for(float v:block)assert(std::isfinite(v));}
 }
 // A mute gesture must fade back on release instead of jumping straight to dry.
 studio::Rack release;
 std::vector<float> held(4000,.25f);release.punch=24;
 release.process(held.data(),2000,2,48000);
 assert(std::fabs(held.back())<1e-6f);
 release.punch=0;float first[2]={.25f,.25f};release.process(first,1,2,48000);
 assert(first[0]>0.f&&first[0]<.01f);assert(first[1]==first[0]);
 std::vector<float> tail(4000,.25f);release.process(tail.data(),2000,2,48000);
 auto dry=exact;release.process(dry.data(),480,2,48000);assert(dry==exact);
 // Lifecycle reset must clear captured audio and punches but preserve authored controls.
 release.control[studio::DRIVE]=61;release.tempo=87;release.punch=1;release.reset();
 assert(release.punch==0&&release.control[studio::DRIVE]==61&&release.tempo==87);
 auto resetDry=exact;release.process(resetDry.data(),480,2,48000);assert(resetDry==exact);
 s.reset();assert(!s.active());assert(s.next(220,48000)==0);
 s.start(0);double resetEnergy=0;for(int i=0;i<480;++i){float v=s.next(220,48000);assert(std::isfinite(v));resetEnergy+=v*v;}assert(resetEnergy>1e-8);
 assert(studio::uri_code("/@mutable/999999.wav")==-1);
 assert(studio::uri_code("/@mutable/1.wav/extra")==-1);
 std::printf("PASS %d synth presets, 20 effect modes\n",studio::model_count()*studio::variation_count());
}
