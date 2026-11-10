// SPDX-License-Identifier: GPL-3.0-or-later
// Rings firmware normally lives in zeroed BSS. Heap/placement construction must produce
// the same real PCM after Init, including the FM feedback and reverb filter histories.
#include "rings/dsp/part.h"
#include "stmlib/utils/random.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <vector>
namespace {
constexpr int rate=48000,block=24,frames=rate*4;
struct Storage { alignas(rings::Part) unsigned char bytes[sizeof(rings::Part)]; };
std::vector<float> render(int model,unsigned char fill) {
    auto memory=std::make_unique<Storage>();
    std::memset(memory->bytes,fill,sizeof(memory->bytes));
    auto* part=new(memory->bytes)rings::Part;
    auto reverb=std::make_unique<std::array<uint16_t,32768>>();
    stmlib::Random::Seed(0x12345678);
    part->Init(reverb->data());part->set_polyphony(1);
    part->set_model(static_cast<rings::ResonatorModel>(model));
    rings::Patch patch{};patch.structure=61/255.f;patch.brightness=109/255.f;
    patch.damping=.6f;patch.position=.35f;
    rings::PerformanceState performance{};performance.note=48;performance.internal_exciter=false;
    std::array<float,block> input{},out{},aux{};
    std::vector<float> pcm;pcm.reserve(frames*2);
    double energy=0;
    for(int offset=0;offset<frames;offset+=block) {
        for(int i=0;i<block;++i) {
            const int frame=offset+i;const double t=frame/double(rate);
            const uint32_t seed=uint32_t(frame)*1664525u+1013904223u;
            const float noise=(int(seed>>24)-128)/128.f;
            const float pulse=std::exp(-std::fmod(t,.125)*39);
            input[i]=t>1.2?0:.1f*std::sin(6.2831853*113*t)+
                .04f*std::sin(6.2831853*339*t)+noise*.058f*pulse;
        }
        part->Process(performance,patch,input.data(),out.data(),aux.data(),block);
        for(int i=0;i<block;++i)for(float sample:{out[i],aux[i]}) {
            assert(std::isfinite(sample)&&std::abs(sample)<1.f);
            energy+=sample*sample;pcm.push_back(sample);
        }
    }
    assert(energy>1e-8);part->~Part();return pcm;
}
}
int main() {
    for(int model=0;model<6;++model) {
        const auto baseline=render(model,0);
        for(unsigned char fill:{0x00,0x55,0x7f,0xa5,0xff}) {
            const auto candidate=render(model,fill);
            assert(candidate.size()==baseline.size());
            assert(std::memcmp(candidate.data(),baseline.data(),baseline.size()*sizeof(float))==0);
        }
    }
    std::puts("PASS: all6 Rings external-exciter models, five poisoned storage fills,4s native PCM per case, all finite/nonzero/bounded and byte-identical to zero/BSS initialization.");
}
