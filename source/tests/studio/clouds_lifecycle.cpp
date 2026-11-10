// SPDX-License-Identifier: GPL-3.0-or-later
// Targeted regressions for heap-backed Clouds: initialization, restart, word alignment and
// ongoing background service. Compile this test and correlator.cc with UBSAN as well.
#include "audio-engine.h"
#include "studio/rack.h"
#include "clouds/dsp/correlator.h"
#include "clouds/dsp/window.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <new>
#include <vector>
namespace {std::atomic<bool> guard{false};std::atomic<int> allocations{0};}
void* operator new(std::size_t n){if(guard.load())++allocations;if(void*p=std::malloc(n))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
int main(){
 // The callback's normal denormal setup, on the same thread used by the direct module tests.
 {auto e=std::make_unique<AudioEngine>();float z[512]{};e->processLiveBlock(z,256,2,48000);}
 for(int poison:{0,0x7f,0x80,0xff}){
  alignas(clouds::Correlator) unsigned char bytes[sizeof(clouds::Correlator)];std::memset(bytes,poison,sizeof(bytes));
  auto*c=new(bytes)clouds::Correlator;std::array<uint32_t,32> source{};std::array<uint32_t,64> target{};
  c->Init(source.data(),target.data());assert(c->done()&&c->best_match()==0);c->EvaluateSomeCandidates();assert(c->done());
  uint32_t state=0x1254feedu;for(auto&v:target){state=state*1664525u+1013904223u;v=state;}
  for(int shift:{0,5,31}){
   for(int i=0;i<32;++i)source[i]=shift?(target[i]<<shift)|(target[i+1]>>(32-shift)):target[i];
   c->StartSearch(1024,17,65536);int rounds=0;while(!c->done()){c->EvaluateSomeCandidates();assert(++rounds<10);}
   assert(c->best_match()==17+shift);c->EvaluateSomeCandidates();assert(c->best_match()==17+shift);
  }
  c->~Correlator();
 }
 // A new and a recycled overlap window both become live after Start.
 std::array<int16_t,264> raw{},tail{};clouds::AudioBuffer<clouds::RESOLUTION_16_BIT> buffer;
 buffer.Init(raw.data(),int(raw.size()),tail.data());for(auto&x:raw)x=6553;
 alignas(clouds::Window) unsigned char windowBytes[sizeof(clouds::Window)];std::memset(windowBytes,0x80,sizeof(windowBytes));
 auto*window=new(windowBytes)clouds::Window;window->Init();assert(window->done());
 for(int cycle=0;cycle<3;++cycle){window->Start(buffer.size(),8,32,65536);assert(!window->done()&&!window->needs_regeneration());double energy=0;
  for(int i=0;i<40;++i){float frame[2]{};window->OverlapAdd(&buffer,frame,1);for(float x:frame){assert(std::isfinite(x));energy+=x*x;}}
  assert(window->done()&&energy>.1);
 }window->~Window();
 std::vector<double> costs,cpuCosts;costs.reserve(4000);cpuCosts.reserve(4000);int blocks=0;
 for(int fx=16;fx<=19;++fx){
  studio::Rack rack;for(int i=0;i<studio::COUNT;++i)rack.control[i]=studio::defaults[i];rack.control[studio::ENABLE]=1;rack.control[studio::FX]=fx;rack.control[studio::FX_MIX]=255;rack.control[studio::COLOR]=103;rack.control[studio::TEXTURE]=116;rack.control[studio::FEEDBACK]=58;
  std::array<float,512> data{};double energy=0,peak=0;int nonzero=0;
  // Start with silence, then introduce real input; wet-only testing cannot be passed by a dry mix.
  for(int off=0;off<48000*4;off+=256){for(int i=0;i<256;++i){const double t=(off+i)/48000.;const bool signal=t>=.4&&t<2.4;data[i*2]=signal?.17*std::sin(6.2831853*(220*t+31*t*t)):0;data[i*2+1]=signal?.13*std::sin(6.2831853*331*t):0;}
   const auto cpuStart=std::clock();const auto start=std::chrono::steady_clock::now();guard=true;rack.process(data.data(),256,2,48000);guard=false;
   costs.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());++blocks;
   cpuCosts.push_back(double(std::clock()-cpuStart)*1000000/CLOCKS_PER_SEC);
   for(float x:data){assert(std::isfinite(x)&&std::abs(x)<1);energy+=x*x;peak=std::max(peak,double(std::abs(x)));if(std::abs(x)>.0001)++nonzero;}
  }
  assert(energy>1&&nonzero>1000);std::cout<<"FX "<<fx<<" wet-only energy "<<energy<<" peak "<<peak<<"\n";
  // Changing mode with audio running exercises workspace reconfiguration; it cannot allocate.
  for(int cycle=0;cycle<16;++cycle){rack.control[studio::FX]=16+cycle%4;for(int i=0;i<512;++i)data[i]=.07f*std::sin(i*.031f+cycle);guard=true;rack.process(data.data(),256,2,48000);guard=false;for(float x:data)assert(std::isfinite(x)&&std::abs(x)<1);}
  rack.reset(); // Reset is intentionally control-thread/stopped work, outside the allocation guard.
 }
 assert(allocations.load()==0);std::sort(costs.begin(),costs.end());std::sort(cpuCosts.begin(),cpuCosts.end());
 std::cout<<"PASS: poisoned/reused Correlator Init, aligned/5/31-bit active searches under UBSAN, Window start/restart, all4Clouds wet-only audible, silence-to-signal, 64mode switches, zero C++ audio allocations. Host "<<blocks<<" x256-frame blocks median "<<costs[costs.size()/2]<<"us p95 "<<costs[costs.size()*95/100]<<"us worst "<<costs.back()<<"us; not a physical handheld benchmark.\n";
 std::cout<<"Single DSP-thread process CPU cost median "<<cpuCosts[cpuCosts.size()/2]<<"us p95 "<<cpuCosts[cpuCosts.size()*95/100]<<"us worst "<<cpuCosts.back()<<"us (wall-time outliers can include OS scheduling).\n";
}
