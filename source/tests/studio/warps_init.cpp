// SPDX-License-Identifier: GPL-3.0-or-later
#include "warps/dsp/modulator.h"
#include "studio/rack.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>
namespace {
using Storage=std::aligned_storage_t<sizeof(warps::Modulator),alignof(warps::Modulator)>;
std::vector<warps::ShortFrame> render(int fill,int mode,Storage& storage){
 std::memset(&storage,fill,sizeof(storage));auto* m=new(&storage)warps::Modulator;m->Init(96000);
 auto*p=m->mutable_parameters();p->modulation_algorithm=float(mode-7)/8.f;p->modulation_parameter=.38f;p->channel_drive[0]=p->channel_drive[1]=.5f;p->carrier_shape=1;p->note=60;
 std::array<warps::ShortFrame,96> input{},output{};std::vector<warps::ShortFrame> pcm;pcm.reserve(4*96000);
 for(int off=0;off<4*48000;off+=48){for(int i=0;i<96;++i){double t=double(off+i/2)/48000.;input[i].l=int16_t((.11*std::sin(6.2831853*113*t)+.045*std::sin(6.2831853*339*t))*32767);input[i].r=int16_t((.09*std::sin(6.2831853*127*t)+.04*std::sin(6.2831853*452*t))*32767);}m->Process(input.data(),output.data(),96);pcm.insert(pcm.end(),output.begin(),output.end());}
 m->~Modulator();return pcm;
}
}
int main(){
 auto storage=std::make_unique<Storage>();unsigned comparisons=0;
 for(int mode=7;mode<=15;++mode){const auto reference=render(0,mode,*storage);for(int fill:{0,0x55,0x7f,0xa5,0xff}){const auto dirty=render(fill,mode,*storage);assert(dirty.size()==reference.size());assert(std::memcmp(reference.data(),dirty.data(),dirty.size()*sizeof(dirty[0]))==0);++comparisons;}}
 // Preserve the exact production block cadence around the reported factory crash.
 for(int id:{195,196}){studio::Rack rack;const auto preset=studio::effect_preset(id);for(int repeat=0;repeat<3;++repeat){rack.reset();for(int key=0;key<studio::COUNT;++key)rack.control[key]=preset[key];std::array<float,512> audio{};double energy=0;
  for(int frame=0;frame<4*48000;frame+=256){for(int i=0;i<256;++i){const double t=double(frame+i)/48000.;audio[2*i]=float(.11*std::sin(6.2831853*113*t));audio[2*i+1]=float(.09*std::sin(6.2831853*127*t));}rack.process(audio.data(),256,2,48000);for(float sample:audio){assert(std::isfinite(sample)&&std::abs(sample)<1.f);energy+=sample*sample;}}
  assert(energy>1e-5);}}
 std::printf("PASS: Warps Init %u poisoned/reused-storage comparisons across9 modes; four-second stereo PCM equals zeroed reference; factory195/196 construction+3resets finite/audible.\n",comparisons);
}
