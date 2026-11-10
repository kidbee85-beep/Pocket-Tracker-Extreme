// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/local_wav_source.h"
#include "common/load_progress.h"
#include "common/byte_source.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs=std::filesystem;
thread_local bool inCallback=false;
std::atomic<size_t> callbackAllocations{0};
void* operator new(size_t n){if(inCallback)++callbackAllocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
namespace {
using Bytes=std::vector<unsigned char>;
void put16(Bytes& b,unsigned v){b.push_back(v);b.push_back(v>>8);}
void put32(Bytes& b,uint32_t v){put16(b,v);put16(b,v>>16);}
uint32_t get32(const unsigned char* b){return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;}
void text(Bytes& b,const char* s,int n){b.insert(b.end(),s,s+n);}
void chunk(Bytes& b,const char* name,const Bytes& data){text(b,name,4);put32(b,uint32_t(data.size()));b.insert(b.end(),data.begin(),data.end());if(data.size()&1)b.push_back(0);}
Bytes read(const fs::path& path){std::ifstream f(path,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});}
void write(const fs::path& path,const Bytes& bytes){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());assert(f.good());}
struct Wav {Bytes file,pcm;size_t dataOffset=0;};
int value(int frame,int channel,int bits){if(bits==8)return 63+(frame*7+channel*31)%129;return (frame*41+channel*733)%50001-25000;}
float expected(int frame,int channel,int bits,bool floating){
    if(floating){if(frame==0)return 0.f;if(frame==1)return 0.f;if(frame==2)return 1.f;return std::sin(float(frame+channel*13)*.021f)*.4f;}
    const int v=value(frame,channel,bits);if(bits==8)return (v-128)/128.f;
    return v/32768.f;
}
Wav make_wav(int bits,bool floating,int channels,bool extensible,int rate=48000){
    Wav w;Bytes fmt;put16(fmt,extensible?0xfffe:(floating?3:1));put16(fmt,channels);put32(fmt,rate);
    put32(fmt,rate*channels*bits/8);put16(fmt,channels*bits/8);put16(fmt,bits);
    if(extensible){put16(fmt,22);put16(fmt,bits);put32(fmt,channels==2?3:4);put32(fmt,floating?3:1);put16(fmt,0);put16(fmt,0x10);fmt.insert(fmt.end(),{0x80,0,0,0xaa,0,0x38,0x9b,0x71});}
    for(int f=0;f<4096;++f)for(int c=0;c<channels;++c){
        const int v=value(f,c,bits);
        if(floating){float x=std::sin(float(f+c*13)*.021f)*.4f;uint32_t raw;
            if(f==0)raw=0x7fc00001u;else if(f==1)raw=0x7f800000u;else if(f==2){x=2.f;std::memcpy(&raw,&x,4);}else std::memcpy(&raw,&x,4);put32(w.pcm,raw);
        }else if(bits==8)w.pcm.push_back(v);
        else if(bits==16)put16(w.pcm,uint16_t(v));
        else if(bits==24){const uint32_t raw=uint32_t(v*256);w.pcm.push_back(raw);w.pcm.push_back(raw>>8);w.pcm.push_back(raw>>16);}
        else put32(w.pcm,uint32_t(int64_t(v)*65536));
    }
    Bytes body;text(body,"WAVE",4);chunk(body,"JUNK",Bytes{1,2,3});chunk(body,"fmt ",fmt);
    w.dataOffset=8+body.size()+8;chunk(body,"data",w.pcm);
    Bytes cues;put32(cues,4);unsigned id=0;for(unsigned p:{0,19,100,1024}){put32(cues,++id);put32(cues,p);text(cues,"data",4);put32(cues,0);put32(cues,0);put32(cues,p);}chunk(body,"cue ",cues);
    text(w.file,"RIFF",4);put32(w.file,uint32_t(body.size()));w.file.insert(w.file.end(),body.begin(),body.end());return w;
}
Bytes data_chunk(const Bytes& wav){
    assert(wav.size()>=12);for(size_t at=12;at+8<=wav.size();){const uint32_t n=get32(wav.data()+at+4);assert(at+8+uint64_t(n)<=wav.size());if(!std::memcmp(wav.data()+at,"data",4))return Bytes(wav.begin()+at+8,wav.begin()+at+8+n);at+=8+n+(n&1);}
    assert(false);return {};
}
std::vector<unsigned> cue_points(const Bytes& wav){
    for(size_t at=12;at+8<=wav.size();){const uint32_t n=get32(wav.data()+at+4);if(!std::memcmp(wav.data()+at,"cue ",4)){std::vector<unsigned> out;const auto* p=wav.data()+at+8;for(unsigned i=0;i<get32(p);++i)out.push_back(get32(p+4+24*i+20));return out;}at+=8+n+(n&1);}
    return {};
}
void render(pt::LocalWavSource& source,float* out,int frames,int rate){inCallback=true;source.render_audio(out,frames,rate);inCallback=false;}
void format_case(const fs::path& dir,int bits,bool floating,int channels,bool extensible){
    const auto stem=std::to_string(bits)+(floating?"F":"P")+std::to_string(channels)+(extensible?"X":"N");
    const auto path=dir/(stem+".wav"),excerpt=dir/(stem+"-excerpt.wav");
    const int rate=extensible?44100:48000;const Wav wav=make_wav(bits,floating,channels,extensible,rate);write(path,wav.file);
    pt::LocalWavSource source;assert(source.open(path.string()));auto state=source.snapshot();
    assert(state.open&&!state.playing&&state.positionFrame==0&&state.totalFrames==4096&&state.dataBytes==int64_t(wav.pcm.size()));
    assert(state.rate==rate&&state.channels==channels&&state.bits==bits&&state.isFloat==floating);
    std::vector<float> audio(8192*2,.125f);render(source,audio.data(),512,rate);for(float v:audio)assert(v==.125f);
    assert(source.play());render(source,audio.data(),512,rate);
    for(int f=0;f<512;++f)for(int c=0;c<2;++c)assert(std::abs(audio[f*2+c]-(.125f+expected(f,std::min(c,channels-1),bits,floating)))<2e-6f);
    source.pause();assert(!source.snapshot().playing&&source.snapshot().positionFrame==512);
    assert(source.seek(100));assert(source.snapshot().positionFrame==100&&!source.snapshot().playing);
    std::fill(audio.begin(),audio.end(),0.f);assert(source.play());render(source,audio.data(),128,rate);
    for(int f=0;f<128;++f)for(int c=0;c<2;++c)assert(std::abs(audio[f*2+c]-expected(100+f,std::min(c,channels-1),bits,floating))<2e-6f);
    source.stop();assert(source.snapshot().positionFrame==0&&!source.snapshot().playing);
    assert(source.play());std::fill(audio.begin(),audio.end(),0.f);render(source,audio.data(),8192,rate);
    state=source.snapshot();assert(state.atEnd&&!state.playing&&state.positionFrame==4096);
    assert(source.play());assert(source.snapshot().positionFrame==0&&source.snapshot().playing);source.pause();
    assert(source.seek(std::numeric_limits<int64_t>::max())&&source.snapshot().positionFrame==4096);
    assert(source.seek(-99)&&source.snapshot().positionFrame==0);
    pt::begin_load();assert(source.export_region(excerpt.string(),19,1000));pt::end_load();
    const Bytes result=read(excerpt),pcm=data_chunk(result);
    const size_t align=size_t(channels*bits/8);assert(pcm==Bytes(wav.pcm.begin()+19*align,wav.pcm.begin()+1000*align));
    assert(cue_points(result)==std::vector<unsigned>({0,81}));
    pt::LocalWavSource cropped;assert(cropped.open(excerpt.string()));const auto meta=cropped.snapshot();
    assert(meta.rate==rate&&meta.channels==channels&&meta.bits==bits&&meta.isFloat==floating&&meta.totalFrames==981);
    assert(!source.export_region(excerpt.string(),0,10)&&read(excerpt)==result);
    assert(read(path)==wav.file);source.close();assert(!source.snapshot().open&&!source.play());
}
void rate_case(const fs::path& dir,int inputRate,int outputRate){
    const auto path=dir/("rate-"+std::to_string(inputRate)+"-"+std::to_string(outputRate)+".wav");
    const Wav wav=make_wav(16,false,2,false,inputRate);write(path,wav.file);
    pt::LocalWavSource source;assert(source.open(path.string()));std::array<float,2048> audio{};
    render(source,audio.data(),1,outputRate);assert(source.play());render(source,audio.data(),1024,outputRate);
    for(int i=0;i<1024;++i){
        const int64_t phase=int64_t(i)*inputRate;const int at=int(phase/outputRate);
        const float part=float(phase%outputRate)/outputRate;
        for(int c=0;c<2;++c){const float a=expected(at,c,16,false),b=expected(at+1,c,16,false);
            assert(std::abs(audio[i*2+c]-(a+(b-a)*part))<2e-6f);}
    }
    assert(source.snapshot().positionFrame==int64_t(1024)*inputRate/outputRate);
    source.pause();assert(source.seek(3000));
    const int changedRate=outputRate==32000?48000:32000;
    audio.fill(0.f);render(source,audio.data(),1,changedRate);assert(source.play());render(source,audio.data(),64,changedRate);
    for(int i=0;i<64;++i){const int64_t phase=int64_t(i)*inputRate;const int at=3000+int(phase/changedRate);
        const float part=float(phase%changedRate)/changedRate;
        for(int c=0;c<2;++c){const float a=expected(at,c,16,false),b=expected(at+1,c,16,false);assert(std::abs(audio[i*2+c]-(a+(b-a)*part))<2e-6f);}}
    assert(source.snapshot().positionFrame==3000+int64_t(64)*inputRate/changedRate);
}
#if !defined(_WIN32)
std::string hookPath;int pipeFd=-1;
int hook_open(const char* path,const char* mode){
    assert(!std::strcmp(mode,"rb"));
    if(!std::strcmp(path,"fixture://source"))return ::open(hookPath.c_str(),O_RDONLY);
    if(!std::strcmp(path,"fixture://pipe"))return ::dup(pipeFd);
    return -1;
}
#endif
}
int main(int argc,char** argv){
    assert(argc==2);const fs::path base=fs::absolute(argv[1]);fs::create_directories(base);
    const fs::path dir=base/("formats-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(dir));
    for(int bits:{8,16,24,32})for(int channels:{1,2})for(bool ext:{false,true})format_case(dir,bits,false,channels,ext);
    for(int channels:{1,2})for(bool ext:{false,true})format_case(dir,32,true,channels,ext);
    rate_case(dir,44100,48000);rate_case(dir,48000,44100);rate_case(dir,48000,32000);
    const Wav valid=make_wav(16,false,2,false);const auto path=dir/"valid.wav";write(path,valid.file);
    pt::LocalWavSource source;assert(source.open(path.string()));assert(source.play());
    Bytes truncated=valid.file;truncated.pop_back();write(dir/"truncated.wav",truncated);
    assert(!source.open((dir/"truncated.wav").string())&&!source.snapshot().open);
    std::array<float,128> unchanged{};unchanged.fill(.25f);render(source,unchanged.data(),64,48000);for(float v:unchanged)assert(v==.25f);
    Bytes broken=valid.file;broken[0]='X';write(dir/"bad-magic.wav",broken);assert(!source.open((dir/"bad-magic.wav").string()));
    assert(source.open(path.string()));assert(!source.export_region((dir/"invalid.wav").string(),20,20)&&!fs::exists(dir/"invalid.wav"));
    assert(!source.export_region("document://not-exclusive",0,100));
    // Cancellation through the actual existing UI load sink removes only our new output.
    const auto cancelled=dir/"cancelled.wav";pt::set_load_tick([](float p){return p<.3f;});pt::begin_load();
    assert(!source.export_region(cancelled.string(),0,4096)&&pt::load_cancelled()&&!fs::exists(cancelled));pt::end_load();
    pt::set_load_tick({});pt::begin_load();assert(source.export_region(cancelled.string(),0,4096));pt::end_load();
    assert(data_chunk(read(cancelled))==valid.pcm&&read(path)==valid.file);
#if !defined(_WIN32)
    hookPath=path.string();pt_set_file_hooks({hook_open,nullptr,nullptr});
    assert(source.open("fixture://source")&&source.snapshot().totalFrames==4096);
    pt::begin_load();assert(source.export_region((dir/"uri-source-excerpt.wav").string(),50,100));pt::end_load();
    int pipes[2];assert(::pipe(pipes)==0);pipeFd=pipes[0];assert(::write(pipes[1],valid.file.data(),44)==44);::close(pipes[1]);
    assert(!source.open("fixture://pipe")&&!source.snapshot().open);::close(pipeFd);pipeFd=-1;
    source.close();pt_set_file_hooks({});
#endif
    assert(callbackAllocations.load()==0);
    std::cout<<"PASS: 20 PCM/float mono/stereo/extensible cases; full preview/additive output; exact linear PCM at44.1/48/32k and output-rate changes; pause/seek/end/replay; exact raw excerpts and rebased cues; exclusive output; load_tick cancellation and retry; failed-open stale guard; seekable URI source/nonseekable rejection; zero callback C++ allocations.\n";
}
