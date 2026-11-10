#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include "common/local_wav_source.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
namespace {
void put16(FILE* out,uint16_t v){const unsigned char b[]={static_cast<unsigned char>(v),static_cast<unsigned char>(v>>8)};assert(std::fwrite(b,1,2,out)==2);}
void put32(FILE* out,uint32_t v){put16(out,uint16_t(v));put16(out,uint16_t(v>>16));}
void sparse_wav(const fs::path& path,int64_t frames){
    assert(frames>8192&&uint64_t(frames)*4<=UINT32_MAX-36);
    FILE* out=std::fopen(path.c_str(),"wb");assert(out);
    assert(std::fwrite("RIFF",1,4,out)==4);put32(out,uint32_t(36+frames*4));assert(std::fwrite("WAVEfmt ",1,8,out)==8);put32(out,16);
    put16(out,1);put16(out,2);put32(out,48000);put32(out,192000);put16(out,4);put16(out,16);
    assert(std::fwrite("data",1,4,out)==4);put32(out,uint32_t(frames*4));
    auto region=[&](int64_t first){assert(!fseeko(out,off_t(44+first*4),SEEK_SET));for(int64_t f=first;f<first+4096;++f){put16(out,uint16_t(int16_t(((f%251)-125)*127)));put16(out,uint16_t(int16_t(((f%241)-120)*139)));}};
    region(0);region(frames-4096);assert(!std::fflush(out)&&int64_t(ftello(out))==44+frames*4);assert(!std::fclose(out));
}
std::string read_small(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
uint64_t file_hash(const fs::path& path){
    std::ifstream in(path,std::ios::binary);assert(in);std::array<char,65536> block{};uint64_t h=1469598103934665603ull;
    while(in){in.read(block.data(),block.size());for(std::streamsize i=0;i<in.gcount();++i){h^=uint8_t(block[size_t(i)]);h*=1099511628211ull;}}return h;
}
uint64_t rss_peak(){rusage usage{};assert(!getrusage(RUSAGE_SELF,&usage));
#ifdef __APPLE__
    return uint64_t(usage.ru_maxrss);
#else
    return uint64_t(usage.ru_maxrss)*1024;
#endif
}
template<class Test> bool until(Test test,int millis=4000){const auto deadline=Clock::now()+std::chrono::milliseconds(millis);do{if(test())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(Clock::now()<deadline);return test();}
void compare_excerpt(const fs::path& source,const fs::path& excerpt,int64_t first,int64_t end){
    pt::LocalWavSource inspect;assert(inspect.open(excerpt.string()));const auto s=inspect.snapshot();
    assert(s.rate==48000&&s.channels==2&&s.bits==16&&!s.isFloat&&s.totalFrames==end-first);
    FILE* in=std::fopen(source.c_str(),"rb");std::ifstream out(excerpt,std::ios::binary);assert(in&&out);
    // Locate the output data chunk instead of assuming no rebased cue metadata.
    char tag[4];out.read(tag,4);out.seekg(12);uint32_t size=0;bool found=false;
    while(out.read(tag,4)){unsigned char bytes[4];out.read(reinterpret_cast<char*>(bytes),4);size=uint32_t(bytes[0])|(uint32_t(bytes[1])<<8)|(uint32_t(bytes[2])<<16)|(uint32_t(bytes[3])<<24);if(!std::memcmp(tag,"data",4)){found=true;break;}out.seekg(std::streamoff(size+(size&1)),std::ios::cur);}assert(found&&size==uint64_t(end-first)*4);
    assert(!fseeko(in,off_t(44+first*4),SEEK_SET));std::array<char,4096> a{},b{};int64_t remaining=(end-first)*4;
    while(remaining){const auto n=std::min<int64_t>(remaining,a.size());assert(std::fread(a.data(),1,size_t(n),in)==size_t(n));out.read(b.data(),n);assert(out&&!std::memcmp(a.data(),b.data(),size_t(n)));remaining-=n;}assert(!std::fclose(in));
}
}
int main(int argc,char** argv){
    assert(argc==2);const auto root=fs::absolute(argv[1]);fs::create_directories(root);
    const auto source=root/"local-over-20MiB.wav",large=root/"local-160MiB.wav",huge=root/"local-over-2GiB.wav";
    sparse_wav(source,8*1024*1024);sparse_wav(large,40*1024*1024);sparse_wav(huge,600000000);
    const auto before=file_hash(source);const auto originalSize=fs::file_size(source);const auto originalTime=fs::last_write_time(source);
    const uint64_t rssBefore=rss_peak();pt::LocalWavSource wav;
    assert(wav.open(source.string()));auto snap=wav.snapshot();assert(snap.open&&!snap.playing&&snap.positionFrame==0&&snap.totalFrames==8*1024*1024);
    std::array<float,512> block;block.fill(.125f);wav.render_audio(block.data(),256,48000);for(float v:block)assert(v==.125f);
    assert(wav.play());bool nonzero=false;
    assert(until([&]{block.fill(0);wav.render_audio(block.data(),256,48000);for(float v:block)nonzero|=v!=0;return nonzero&&wav.snapshot().positionFrame>0;}));
    wav.pause();block.fill(.25f);wav.render_audio(block.data(),256,44100);for(float v:block)assert(v==.25f);
    assert(wav.seek(-500));assert(wav.snapshot().positionFrame==0&&!wav.snapshot().playing);
    assert(wav.seek(std::numeric_limits<int64_t>::max()));assert(wav.snapshot().positionFrame==snap.totalFrames);
    wav.stop();assert(wav.snapshot().positionFrame==0&&!wav.snapshot().playing);
    assert(wav.seek(snap.totalFrames-480));assert(wav.play());
    assert(until([&]{block.fill(0);wav.render_audio(block.data(),256,48000);const auto s=wav.snapshot();assert(s.positionFrame<=s.totalFrames);return s.atEnd;}));
    wav.stop();
    const auto excerpt=root/"Selected-Excerpt.wav";assert(wav.export_region(excerpt.string(),2048,6144));compare_excerpt(source,excerpt,2048,6144);
    const auto excerptBefore=file_hash(excerpt);assert(!wav.export_region(excerpt.string(),0,100));assert(file_hash(excerpt)==excerptBefore);
    assert(!wav.export_region(source.string(),0,100));assert(fs::file_size(source)==originalSize&&fs::last_write_time(source)==originalTime);
    assert(!wav.export_region((root/"missing-parent/take.wav").string(),0,100));
    const auto retry=root/"Retry-Excerpt.wav";assert(wav.export_region(retry.string(),0,256));compare_excerpt(source,retry,0,256);
    int foreignWins=0,exportWins=0;
    for(int i=0;i<100;++i){
        const auto target=root/("Publication-race-"+std::to_string(i)+".wav");std::atomic<bool> go{false};bool exported=false;int created=-1;
        std::thread writer([&]{while(!go.load(std::memory_order_acquire))std::this_thread::yield();exported=wav.export_region(target.string(),0,2048);});
        std::thread foreign([&]{while(!go.load(std::memory_order_acquire))std::this_thread::yield();created=::open(target.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);if(created>=0){static constexpr char data[]="FOREIGN USER FILE";assert(::write(created,data,sizeof(data)-1)==ssize_t(sizeof(data)-1));assert(!::close(created));}});
        go.store(true,std::memory_order_release);writer.join();foreign.join();
        assert(exported!=(created>=0));if(created>=0){++foreignWins;assert(read_small(target)=="FOREIGN USER FILE");}else{++exportWins;compare_excerpt(source,target,0,2048);}fs::remove(target);
    }
    assert(wav.open(large.string()));assert(wav.snapshot().totalFrames==40*1024*1024);
    const auto canceled=root/"Canceled-Excerpt.wav";bool canceledResult=true;std::atomic<bool> done{false};
    std::thread exporter([&]{canceledResult=wav.export_region(canceled.string(),0,40*1024*1024);done.store(true);});
    const bool sawExport=until([&]{return wav.snapshot().exporting||done.load();});assert(sawExport);
    if(!done.load())wav.cancel_export();exporter.join();assert(!canceledResult&&!fs::exists(canceled));
    assert(wav.export_region((root/"After-Cancel.wav").string(),128,384));
    // Header offsets beyond signed32-bit must remain usable without decoding the whole file.
    const bool hugeOpened=wav.open(huge.string());if(!hugeOpened)std::fprintf(stderr,"Large-offset source failed: %s\n",wav.snapshot().error.c_str());assert(hugeOpened);snap=wav.snapshot();assert(snap.totalFrames==600000000);
    assert(wav.seek(snap.totalFrames-4096));assert(wav.play());nonzero=false;
    assert(until([&]{block.fill(0);wav.render_audio(block.data(),256,48000);for(float v:block)nonzero|=v!=0;return nonzero;}));wav.pause();
    const auto tail=root/"Beyond-2GiB-Excerpt.wav";assert(wav.export_region(tail.string(),599996000,599997000));compare_excerpt(huge,tail,599996000,599997000);
    // Actual callback runs while the sole control thread repeatedly changes sources and seeks.
    std::atomic<bool> running{true};std::atomic<unsigned> callbacks{0};
    std::thread audio([&]{std::array<float,256> data{};while(running.load()){data.fill(0);wav.render_audio(data.data(),128,48000);callbacks.fetch_add(1);std::this_thread::sleep_for(std::chrono::microseconds(100));}});
    for(int i=0;i<50;++i){assert(wav.open((i%2?source:large).string()));assert(wav.play());assert(wav.seek(i*113));wav.pause();wav.stop();wav.close();}
    assert(!wav.open((root/"missing.wav").string()));assert(!wav.snapshot().open&&!wav.play());
    running.store(false);audio.join();assert(callbacks.load()>0);
    block.fill(.375f);wav.render_audio(block.data(),256,48000);for(float v:block)assert(v==.375f);
    assert(file_hash(source)==before&&fs::file_size(source)==originalSize&&fs::last_write_time(source)==originalTime);
    const auto rssAfter=rss_peak();const auto rssDelta=rssAfter>rssBefore?rssAfter-rssBefore:0;
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
#if !defined(__has_feature)
#define __has_feature(x) 0
#endif
#if !__has_feature(address_sanitizer) && !__has_feature(thread_sanitizer)
    assert(rssDelta<12u*1024u*1024u);
#endif
#endif
    for(const auto& p:fs::directory_iterator(root))assert(p.path().filename().string().find(".part")==std::string::npos);
    std::printf("PASS: actual32MiB/160MiB/>2GiB local WAV metadata/stream/exact excerpt, pause/seek/end,100 exclusive publication races (%d foreign/%d export), cancel+retry,50 callback/control reopen cycles, source hash/time preserved; RSS delta%llu bytes.\n",foreignWins,exportWins,(unsigned long long)rssDelta);
}
