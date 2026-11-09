#include "vendor/mutable/braids/analog_oscillator.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <new>

// Integer PCM baselines captured from beta.1 before the signed-output fix.
// Explicitly zero the fixture's initial storage: the original hardware class has
// an empty constructor and expects static-zero-initialized firmware storage.
// This keeps native and sanitizer allocator fill patterns out of the test.
int main(int argc,char**){
 constexpr braids::AnalogOscillatorShape shapes[]={braids::OSC_SHAPE_CSAW,braids::OSC_SHAPE_SQUARE,braids::OSC_SHAPE_SAW,braids::OSC_SHAPE_VARIABLE_SAW};
 constexpr uint64_t expected[]={5242044014059889927ULL,15384787669366127106ULL,18384463596011844720ULL,8742216858460017116ULL};
 for(size_t model=0;model<std::size(shapes);++model){
  uint64_t hash=1469598103934665603ULL;bool negative=false,positive=false;
  for(int variant=0;variant<3;++variant)for(int pitch:{24,60,96}){
   alignas(braids::AnalogOscillator) std::array<unsigned char,sizeof(braids::AnalogOscillator)> storage{};
   auto* synth=new(storage.data())braids::AnalogOscillator;synth->Init();synth->set_shape(shapes[model]);
   std::array<uint8_t,48> sync{},syncOut{};std::array<int16_t,48> samples{};
   synth->Render(sync.data(),samples.data(),syncOut.data(),samples.size());
   for(int block=0;block<48;++block){
    synth->set_pitch(pitch*128);synth->set_parameter(variant==0?0:variant==1?16384:32767);synth->set_aux_parameter(variant==0?0:variant==1?30000:12000);
    sync.fill(0);if(block%5==0)sync[7]=128;
    synth->Render(sync.data(),samples.data(),syncOut.data(),samples.size());
    for(const int16_t sample:samples){negative|=sample<0;positive|=sample>0;const uint16_t pcm=static_cast<uint16_t>(sample);
     hash^=pcm&255;hash*=1099511628211ULL;hash^=pcm>>8;hash*=1099511628211ULL;
    }
   }
   synth->~AnalogOscillator();
  }
  assert(negative&&positive);
  if(argc>1)std::cout<<static_cast<int>(shapes[model])<<" "<<hash<<"\n";
  else assert(hash==expected[model]);
 }
 if(argc==1)std::cout<<"PASS: CSaw/Square/Saw/VariableSaw PCM matches original hashes across macro values, pitches and fractional sync\n";
}
