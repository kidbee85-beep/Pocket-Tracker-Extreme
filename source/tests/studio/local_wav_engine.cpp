#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "songcore/wav_writer.h"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace {thread_local bool callbackGuard=false;std::atomic<unsigned> callbackAllocations{0};}
void* operator new(std::size_t n){if(callbackGuard)++callbackAllocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
namespace {
using Clock=std::chrono::steady_clock;
void source_play(pt::LocalWavSource& source,const std::string& path){assert(source.open(path));assert(source.play());std::this_thread::sleep_for(std::chrono::milliseconds(15));}
double render(AudioEngine& engine,int blocks){
    std::array<float,512> output{};double sum=0;
    for(int i=0;i<blocks;++i){callbackGuard=true;engine.processLiveBlock(output.data(),256,2,48000);callbackGuard=false;
        for(float v:output){assert(std::isfinite(v));sum+=v*v;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    return sum;
}
bool audible(AudioEngine& engine){const auto end=Clock::now()+std::chrono::seconds(4);do{if(render(engine,1)>1e-5)return true;}while(Clock::now()<end);return false;}
void put16(std::ostream& out,uint16_t u){const char b[]={char(u),char(u>>8)};out.write(b,2);}
void put32(std::ostream& out,uint32_t u){const char b[]={char(u),char(u>>8),char(u>>16),char(u>>24)};out.write(b,4);}
constexpr std::array<uint32_t,10> floatBits{{0x7fc00001u,0x7f800000u,0xff800000u,0xffc12345u,0x3e800000u,0xbe800000u,0x80000000u,0x00000001u,0x3f000000u,0xbf000000u}};
void float_fixture(const std::string& path){
    constexpr int frames=48000;std::ofstream out(path,std::ios::binary);assert(out);
    out.write("RIFF",4);put32(out,36+frames*8);out.write("WAVEfmt ",8);put32(out,16);put16(out,3);put16(out,2);put32(out,48000);put32(out,48000*8);put16(out,8);put16(out,32);out.write("data",4);put32(out,frames*8);
    for(int i=0;i<frames;++i){put32(out,floatBits[size_t(i)%floatBits.size()]);put32(out,floatBits[(size_t(i)+3)%floatBits.size()]);}
    out.close();assert(out);
}
struct OrderingEngine {
    std::array<int,2> cleared{};int count=0;bool fail=false;bool called=false;
    void clearSample(int id){assert(count<2);cleared[size_t(count++)]=id;}
    int loadSampleFromWavFile(int,const char*){called=true;assert(count==2&&cleared[0]==254&&cleared[1]==255);return fail?0:48000;}
    int loadSampleFromCompressed(int,const char*){return 0;}
    int getSampleRate(){return 48000;}
};
}
int main(){
    const auto root=std::filesystem::temp_directory_path()/("trackwerk-local-wav-engine-"+std::to_string(Clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);const auto path=(root/"original-source.wav").string();
    std::vector<float> tone(48000*8);for(size_t i=0;i<tone.size();++i)tone[i]=.16f*std::sin(6.283185307179586*187.5*double(i)/48000.);
    assert(songcore::write_wav_mono(path,tone,48000));
    auto engine=std::make_unique<AudioEngine>();songcore::SongcoreHost host(engine.get(),48000);assert(host.has_local_wav_source());assert(&host.local_wav_source()==&engine->local_wav_source());
    auto& source=host.local_wav_source();source_play(source,path);assert(audible(*engine));assert(source.snapshot().positionFrame>0); // no transport or note is playing
    engine->setMasterVolume(0);render(*engine,32);assert(render(*engine,16)<1e-10);
    engine->setMasterVolume(1);assert(audible(*engine));
    host.stop_preview();assert(source.snapshot().open&&!source.snapshot().playing);render(*engine,16);assert(render(*engine,8)<1e-10);
    assert(source.play());assert(audible(*engine));host.stop();assert(!source.snapshot().playing);
    source_play(source,path);host.play_song(0);assert(!source.snapshot().open);host.stop();
    source_play(source,path);assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));assert(!source.snapshot().open);
    source_play(source,path);host.new_project();assert(!source.snapshot().open);
    source_play(source,path);host.clear_previews();assert(!source.snapshot().open);
    source_play(source,path);assert(host.preview_file(path));assert(!source.snapshot().open);host.stop_preview();host.clear_previews();
    assert(host.load_sample(0,path));const auto oldPath=host.project().instruments[0].sampleFilePath;const int oldLength=engine->getSampleLength(0);assert(oldLength==int(tone.size()));
    std::vector<float> oldPCM(static_cast<size_t>(oldLength));engine->getSampleData(0,oldPCM.data());
    const float tiny[]={.1f,.2f,.3f};assert(engine->loadSample(254,tiny,3)&&engine->loadSample(255,tiny,3));
    assert(!host.load_sample(0,(root/"does-not-exist.wav").string()));assert(host.project().instruments[0].sampleFilePath==oldPath&&engine->getSampleLength(0)==oldLength);
    std::vector<float> retained(static_cast<size_t>(oldLength));engine->getSampleData(0,retained.data());assert(retained==oldPCM);assert(engine->getSampleLength(254)==0&&engine->getSampleLength(255)==0);
    OrderingEngine mock;songcore::Project p=songcore::make_default_project();songcore::Routing routing;mock.fail=true;p.instruments[0].sampleFilePath="preserved.wav";
    assert(!songcore::load_instrument_sample(&mock,p,0,"missing.wav",routing));assert(mock.called&&p.instruments[0].sampleFilePath=="preserved.wav");
    {
        auto clean=std::make_unique<AudioEngine>();auto& preview=clean->local_wav_source();source_play(preview,path);const auto before=preview.snapshot().positionFrame;
        std::array<float,2048> output{};clean->renderOffline(1024,output.data(),48000);for(float v:output)assert(v==0);assert(preview.snapshot().positionFrame==before);
        clean->setOfflineRendering(true);assert(!preview.snapshot().playing);assert(preview.play());clean->processLiveBlock(output.data(),1024,2,48000);for(float v:output)assert(v==0);assert(preview.snapshot().positionFrame==before);clean->setOfflineRendering(false);preview.close();
    }
    {
        const auto raw=(root/"nonfinite-source-float.wav").string(),clip=(root/"raw-float-crop.wav").string();float_fixture(raw);
        auto& preview=host.local_wav_source();assert(preview.open(raw));assert(preview.export_region(clip,7,7777));
        // The export deliberately preserves raw source bits. The instrument importer,
        // compiled with production fast-math, must sanitize nonfinite IEEE patterns.
        assert(host.load_sample(1,clip));assert(engine->getSampleLength(1)==7770);
        std::vector<float> left(7770),right(7770);engine->getSampleData(1,left.data());engine->getSampleDataRight(1,right.data());
        for(size_t i=0;i<left.size();++i)for(int channel=0;channel<2;++channel){
            uint32_t expected=floatBits[(i+7+size_t(channel)*3)%floatBits.size()];if((expected&0x7f800000u)==0x7f800000u)expected=0;
            const float value=channel?right[i]:left[i];uint32_t actual=0;std::memcpy(&actual,&value,4);assert(actual==expected);
        }
        host.preview_instrument(1);assert(render(*engine,16)>1e-5);host.stop_preview();
    }
    songcore::SongcoreHost trace(nullptr,48000);assert(!trace.has_local_wav_source());
    assert(callbackAllocations==0);source.close();std::filesystem::remove_all(root);
    std::cout<<"{\"local_wav_engine_host_pass\":true,\"idle_audible\":true,\"master_mute_obeyed\":true,\"offline_excluded\":true,\"preview_scratch_freed_before_load\":true,\"failed_load_preserves_target_PCM\":true,\"lifecycle_stops\":true,\"raw_float_crop_import_sanitized\":true,\"finite_float_bits_preserved\":true,\"callback_allocations\":"<<callbackAllocations<<"}\n";
}
