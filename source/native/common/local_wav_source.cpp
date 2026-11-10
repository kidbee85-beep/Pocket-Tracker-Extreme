// SPDX-License-Identifier: GPL-3.0-or-later
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
#include "local_wav_source.h"
#include "byte_source.h"
#include "load_progress.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pt {
namespace {
constexpr uint32_t kRing=32768;
constexpr int kReadFrames=4096;
static_assert(std::atomic<uint32_t>::is_always_lock_free,"Preview callback needs lock-free 32-bit atomics");
struct File {
    FILE* p=nullptr;
    explicit File(FILE* f=nullptr):p(f){}
    ~File(){if(p)std::fclose(p);}
    File(const File&)=delete;File& operator=(const File&)=delete;
};
uint16_t u16(const unsigned char* p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
uint32_t u32(const unsigned char* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
bool seek_file(FILE* f,int64_t n){
    if(n<0)return false;
#ifdef _WIN32
    return _fseeki64(f,n,SEEK_SET)==0;
#else
    return fseeko(f,static_cast<off_t>(n),SEEK_SET)==0;
#endif
}
int64_t file_size(FILE* f){
#ifdef _WIN32
    if(_fseeki64(f,0,SEEK_END)!=0)return -1;return _ftelli64(f);
#else
    if(fseeko(f,0,SEEK_END)!=0)return -1;return static_cast<int64_t>(ftello(f));
#endif
}
void read_exact(FILE* f,void* p,size_t n){if(std::fread(p,1,n,f)!=n)throw std::runtime_error("WAV is truncated or unreadable");}
void write_exact(FILE* f,const void* p,size_t n){if(std::fwrite(p,1,n,f)!=n)throw std::runtime_error("Could not write excerpt; check free storage");}
void write_u32(FILE* f,uint32_t n){const unsigned char b[]={static_cast<unsigned char>(n),static_cast<unsigned char>(n>>8),static_cast<unsigned char>(n>>16),static_cast<unsigned char>(n>>24)};write_exact(f,b,4);}
struct Meta {
    int rate=0,channels=0,bits=0,align=0;bool isFloat=false;
    int64_t frames=0,bytes=0,dataOffset=0;
    std::string path;
    std::vector<unsigned char> fmt;
    std::vector<int64_t> cues;
};
Meta parse(FILE* f,const std::string& path){
    const int64_t size=file_size(f);
    if(size<12||!seek_file(f,0))throw std::runtime_error("WAV must be a readable seekable file");
    unsigned char head[12];read_exact(f,head,12);
    if(std::memcmp(head,"RIFF",4)||std::memcmp(head+8,"WAVE",4))throw std::runtime_error("Choose a RIFF/WAVE audio file");
    const int64_t end=int64_t(u32(head+4))+8;
    if(end<12||end>size)throw std::runtime_error("WAV RIFF data is truncated");
    Meta m;m.path=path;bool haveData=false;unsigned chunks=0;
    for(int64_t at=12;at+8<=end;){
        if(++chunks>65536)throw std::runtime_error("WAV contains too many metadata chunks");
        if(!seek_file(f,at))throw std::runtime_error("Cannot seek WAV chunks");
        unsigned char ch[8];read_exact(f,ch,8);const uint32_t n=u32(ch+4);
        const int64_t body=at+8,next=body+int64_t(n)+(n&1u);
        if(next>end)throw std::runtime_error("WAV chunk is truncated");
        if(!std::memcmp(ch,"fmt ",4)&&m.fmt.empty()){
            if(n<16||n>4096)throw std::runtime_error("Unsupported WAV format header");
            m.fmt.resize(n);read_exact(f,m.fmt.data(),n);const auto* b=m.fmt.data();
            int format=u16(b);m.channels=u16(b+2);const uint32_t rate=u32(b+4);
            m.align=u16(b+12);m.bits=u16(b+14);
            if(format==0xfffe){
                static const unsigned char tail[]={0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if(n<40||u16(b+16)<22||std::memcmp(b+28,tail,sizeof tail)||u16(b+26)!=0)
                    throw std::runtime_error("Unsupported extensible WAV subtype");
                const int valid=u16(b+18);if(valid<1||valid>m.bits)throw std::runtime_error("Invalid WAV valid-bit count");
                format=u16(b+24);
            }
            m.isFloat=format==3;
            if(m.channels<1||m.channels>2||rate<1||rate>768000||
               !((format==1&&(m.bits==8||m.bits==16||m.bits==24||m.bits==32))||(format==3&&m.bits==32))||
               m.align!=m.channels*(m.bits/8))throw std::runtime_error("WAV needs mono/stereo PCM8/16/24/32 or float32");
            m.rate=static_cast<int>(rate);
            if(u32(b+8)!=uint32_t(m.rate)*uint32_t(m.align))throw std::runtime_error("Invalid WAV byte rate");
        }else if(!std::memcmp(ch,"data",4)&&!haveData){m.dataOffset=body;m.bytes=n;haveData=true;}
        else if(!std::memcmp(ch,"cue ",4)){
            if(n<4)throw std::runtime_error("Invalid WAV cue chunk");
            unsigned char countBytes[4];read_exact(f,countBytes,4);const uint32_t count=u32(countBytes);
            if(uint64_t(count)*24+4>n)throw std::runtime_error("Truncated WAV cue points");
            // Tracker slice metadata is bounded independently of hostile cue counts.
            const uint32_t keep=std::min<uint32_t>(count,uint32_t(256-m.cues.size()));
            for(uint32_t i=0;i<keep;++i){
                unsigned char cue[24];read_exact(f,cue,24);
                if(!std::memcmp(cue+8,"data",4)&&u32(cue+12)==0&&u32(cue+16)==0)m.cues.push_back(u32(cue+20));
            }
        }
        at=next;
    }
    if(m.fmt.empty()||!haveData||m.bytes<1||m.bytes%m.align)throw std::runtime_error("WAV has no complete audio frames");
    m.frames=m.bytes/m.align;
    std::sort(m.cues.begin(),m.cues.end());m.cues.erase(std::unique(m.cues.begin(),m.cues.end()),m.cues.end());
    m.cues.erase(std::remove_if(m.cues.begin(),m.cues.end(),[&](int64_t p){return p<0||p>=m.frames;}),m.cues.end());
    return m;
}
float decode(const unsigned char* p,const Meta& m){
    if(m.isFloat){
        const uint32_t raw=u32(p);
        // pockettracker is built with fast-math on some hosts. Classify input
        // bits before forming a float; std::isfinite can otherwise be folded
        // away and let malformed WAV NaN/Inf reach the live callback.
        if((raw&0x7f800000u)==0x7f800000u)return 0.f;
        float v;std::memcpy(&v,&raw,4);return std::clamp(v,-1.f,1.f);
    }
    if(m.bits==8)return (int(p[0])-128)/128.f;
    if(m.bits==16)return static_cast<int16_t>(u16(p))/32768.f;
    if(m.bits==24){uint32_t v=uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16;if(v&0x800000)v|=0xff000000;return static_cast<int32_t>(v)/8388608.f;}
    return static_cast<int32_t>(u32(p))/2147483648.f;
}
}

struct LocalWavSource::Impl {
    std::array<float,kRing*2> ring{};
    std::array<unsigned char,kReadFrames*8> raw{};
    std::array<float,kReadFrames*2> cache{};
    std::atomic<uint32_t> read{0},write{0},readers{0};
    std::atomic<uint32_t> positionSequence{0},positionLow{0},positionHigh{0};
    std::atomic<bool> enabled{false},playing{false},eof{false},atEnd{false},quit{false};
    std::atomic<int> requestedRate{48000};
    std::atomic<uint32_t> cancelEpoch{0},exportProgress{0};
    std::atomic<bool> exporting{false};
    mutable std::mutex stateMutex;
    Meta meta;std::string error;
    std::mutex serialMutex,commandMutex,exportMutex;
    std::condition_variable commandWake,commandDone;
    enum class Command{NONE,OPEN,CLOSE,PLAY,PAUSE,STOP,SEEK};
    Command command=Command::NONE;std::string commandPath;int64_t commandFrame=0;
    bool pending=false,done=false,commandResult=false;
    FILE* file=nullptr;
    int64_t cacheBase=-1;int cacheCount=0;
    int activeRate=48000,sourceRate=0;int64_t sourceFrames=0,baseFrame=0,producerFrame=0;
    uint64_t consumedFrames=0;uint32_t producerFraction=0;
    std::thread worker;
    Impl():worker([this]{run();}){}
    ~Impl(){
        ++cancelEpoch;
        {std::lock_guard<std::mutex> lock(commandMutex);quit=true;}
        commandWake.notify_all();if(worker.joinable())worker.join();
        // A previously started export sees cancellation and owns its own input descriptor.
        std::lock_guard<std::mutex> waitForExport(exportMutex);
    }
    void set_position(int64_t frame)noexcept{
        const auto u=static_cast<uint64_t>(std::max<int64_t>(0,frame));
        positionSequence.fetch_add(1,std::memory_order_seq_cst);
        positionLow.store(uint32_t(u),std::memory_order_seq_cst);positionHigh.store(uint32_t(u>>32),std::memory_order_seq_cst);
        positionSequence.fetch_add(1,std::memory_order_seq_cst);
    }
    int64_t position()const noexcept{
        for(;;){const uint32_t a=positionSequence.load(std::memory_order_seq_cst);if(a&1)continue;
            const uint32_t lo=positionLow.load(std::memory_order_seq_cst),hi=positionHigh.load(std::memory_order_seq_cst);
            if(a==positionSequence.load(std::memory_order_seq_cst))return static_cast<int64_t>((uint64_t(hi)<<32)|lo);}
    }
    // Gate entry and quiescence share a total order: a callback observing the
    // prior enabled=true must have published its reader before this count can
    // be observed as zero. Acquire/release on two separate atomics is weaker.
    void quiesce()noexcept{enabled.store(false,std::memory_order_seq_cst);while(readers.load(std::memory_order_seq_cst))std::this_thread::yield();}
    void reset_ring(int64_t frame){
        quiesce();read=write=0;eof=false;atEnd=false;cacheBase=-1;cacheCount=0;
        baseFrame=producerFrame=frame;producerFraction=0;consumedFrames=0;
        activeRate=requestedRate.load();set_position(frame);
        if(frame>=sourceFrames){eof=true;atEnd=true;playing=false;}
    }
    void clear_source(){
        quiesce();playing=false;++cancelEpoch;
        if(file){std::fclose(file);file=nullptr;}
        sourceRate=0;sourceFrames=0;reset_ring(0);atEnd=false;
        std::lock_guard<std::mutex> lock(stateMutex);meta={};error.clear();
    }
    void fail(const std::string& message){
        quiesce();playing=false;read=write=0;eof=false;
        std::lock_guard<std::mutex> lock(stateMutex);error=message;
    }
    float sample(int64_t frame,int channel){
        frame=std::clamp<int64_t>(frame,0,sourceFrames-1);
        if(frame<cacheBase||frame>=cacheBase+cacheCount){
            cacheBase=frame;cacheCount=int(std::min<int64_t>(kReadFrames,sourceFrames-frame));
            if(!seek_file(file,meta.dataOffset+frame*meta.align))throw std::runtime_error("Cannot seek WAV audio");
            read_exact(file,raw.data(),size_t(cacheCount)*meta.align);
            const int bytes=meta.bits/8;
            for(int i=0;i<cacheCount;++i)for(int c=0;c<2;++c)
                cache[size_t(i)*2+c]=decode(raw.data()+size_t(i)*meta.align+std::min(c,meta.channels-1)*bytes,meta);
        }
        return cache[size_t(frame-cacheBase)*2+channel];
    }
    void fill(){
        if(!file||!playing.load())return;
        const uint32_t r=read.load(std::memory_order_acquire);uint32_t w=write.load(std::memory_order_relaxed);
        while(uint32_t(w-r)<kRing-2&&producerFrame<sourceFrames){
            const float fraction=float(producerFraction)/activeRate;
            const float l=sample(producerFrame,0),rr=sample(producerFrame,1);
            const int64_t next=std::min(producerFrame+1,sourceFrames-1);
            ring[(w%kRing)*2]=l+(sample(next,0)-l)*fraction;
            ring[(w%kRing)*2+1]=rr+(sample(next,1)-rr)*fraction;++w;
            const uint64_t phase=uint64_t(producerFraction)+uint32_t(sourceRate);
            producerFrame+=phase/uint32_t(activeRate);producerFraction=uint32_t(phase%uint32_t(activeRate));
        }
        write.store(w,std::memory_order_release);
        if(producerFrame>=sourceFrames)eof.store(true,std::memory_order_release);
    }
    bool perform(Command c,const std::string& path,int64_t frame){
        if(c==Command::OPEN){
            clear_source();File fresh(pt_fopen(path.c_str(),"rb"));
            if(!fresh.p)throw std::runtime_error("Could not open local WAV");
            Meta next=parse(fresh.p,path);sourceRate=next.rate;sourceFrames=next.frames;
            {std::lock_guard<std::mutex> lock(stateMutex);meta=std::move(next);error.clear();}
            file=fresh.p;fresh.p=nullptr;reset_ring(0);return true;
        }
        if(c==Command::CLOSE){clear_source();return true;}
        if(c==Command::PAUSE){quiesce();playing=false;return true;}
        if(!file)throw std::runtime_error("Open a local WAV first");
        if(c==Command::STOP){playing=false;reset_ring(0);return true;}
        if(c==Command::PLAY){
            int64_t at=position();if(at>=sourceFrames)at=0;
            reset_ring(at);playing=true;return true;
        }
        if(c==Command::SEEK){const bool resume=playing.load();reset_ring(std::clamp<int64_t>(frame,0,sourceFrames));playing=resume&&position()<sourceFrames;return true;}
        return true;
    }
    void run()noexcept{
        for(;;){
            Command c=Command::NONE;std::string path;int64_t frame=0;
            {std::unique_lock<std::mutex> lock(commandMutex);
                commandWake.wait_for(lock,std::chrono::milliseconds(4),[&]{return pending||quit.load();});
                if(quit.load())break;
                if(pending){c=command;path=std::move(commandPath);frame=commandFrame;pending=false;}}
            bool ok=true;
            try{
                if(c!=Command::NONE)ok=perform(c,path,frame);
                if(playing.load()&&activeRate!=requestedRate.load())reset_ring(position());
                fill();
                if(playing.load()&&write.load()!=read.load())enabled.store(true,std::memory_order_seq_cst);
            }catch(const std::exception& e){fail(e.what());ok=false;}
             catch(...){fail("Local WAV operation failed");ok=false;}
            if(c!=Command::NONE){std::lock_guard<std::mutex> lock(commandMutex);commandResult=ok;done=true;commandDone.notify_all();}
        }
        quiesce();playing=false;if(file){std::fclose(file);file=nullptr;}
        {std::lock_guard<std::mutex> lock(commandMutex);done=true;commandResult=false;}commandDone.notify_all();
    }
    bool submit(Command c,const std::string& path={},int64_t frame=0){
        std::lock_guard<std::mutex> serial(serialMutex);std::unique_lock<std::mutex> lock(commandMutex);
        if(quit.load())return false;command=c;commandPath=path;commandFrame=frame;done=false;pending=true;commandWake.notify_one();
        commandDone.wait(lock,[&]{return done||quit.load();});return done&&commandResult;
    }
};

LocalWavSource::LocalWavSource():p_(new Impl){}
LocalWavSource::~LocalWavSource()=default;
bool LocalWavSource::open(const std::string& path){try{return p_->submit(Impl::Command::OPEN,path);}catch(...){return false;}}
void LocalWavSource::close()noexcept{try{++p_->cancelEpoch;p_->submit(Impl::Command::CLOSE);}catch(...){}}
bool LocalWavSource::play()noexcept{try{return p_->submit(Impl::Command::PLAY);}catch(...){return false;}}
void LocalWavSource::pause()noexcept{try{p_->submit(Impl::Command::PAUSE);}catch(...){}}
void LocalWavSource::stop()noexcept{try{p_->submit(Impl::Command::STOP);}catch(...){}}
bool LocalWavSource::seek(int64_t frame)noexcept{try{return p_->submit(Impl::Command::SEEK,{},frame);}catch(...){return false;}}
void LocalWavSource::cancel_export()noexcept{++p_->cancelEpoch;}
LocalWavSnapshot LocalWavSource::snapshot()const{
    LocalWavSnapshot out;
    {std::lock_guard<std::mutex> lock(p_->stateMutex);const auto& m=p_->meta;out.open=m.frames>0;out.rate=m.rate;out.channels=m.channels;out.bits=m.bits;out.isFloat=m.isFloat;out.totalFrames=m.frames;out.dataBytes=m.bytes;out.path=m.path;out.error=p_->error;}
    out.positionFrame=std::clamp<int64_t>(p_->position(),0,out.totalFrames);
    out.playing=p_->playing.load();out.atEnd=p_->atEnd.load();out.exporting=p_->exporting.load();out.exportProgress=p_->exportProgress.load()/1000000.;return out;
}
void LocalWavSource::render_audio(float* stereo,int frames,int rate)noexcept{
    if(!stereo||frames<1||rate<1||rate>768000)return;
    auto& p=*p_;p.requestedRate.store(rate,std::memory_order_relaxed);p.readers.fetch_add(1,std::memory_order_seq_cst);
    if(!p.enabled.load(std::memory_order_seq_cst)||p.activeRate!=rate){p.readers.fetch_sub(1,std::memory_order_seq_cst);return;}
    const uint32_t r=p.read.load(std::memory_order_relaxed),w=p.write.load(std::memory_order_acquire);
    const uint32_t n=std::min<uint32_t>(uint32_t(frames),w-r);
    for(uint32_t i=0;i<n;++i){stereo[size_t(i)*2]+=p.ring[((r+i)%kRing)*2];stereo[size_t(i)*2+1]+=p.ring[((r+i)%kRing)*2+1];}
    p.consumedFrames+=n;
    p.set_position(std::min<int64_t>(p.sourceFrames,p.baseFrame+int64_t(p.consumedFrames*uint32_t(p.sourceRate)/uint32_t(rate))));
    p.read.store(r+n,std::memory_order_release);
    if(r+n==w&&p.eof.load(std::memory_order_acquire)){p.atEnd=true;p.playing=false;p.enabled=false;p.set_position(p.sourceFrames);}
    p.readers.fetch_sub(1,std::memory_order_seq_cst);
}

bool LocalWavSource::export_region(const std::string& destination,int64_t first,int64_t end){
    auto& p=*p_;std::lock_guard<std::mutex> serial(p.exportMutex);
    Meta m;{std::lock_guard<std::mutex> lock(p.stateMutex);m=p.meta;p.error.clear();}
    auto error=[&](const std::string& message){std::lock_guard<std::mutex> lock(p.stateMutex);p.error=message;};
    if(m.frames<1||first<0||end<=first||end>m.frames){error("Choose a valid WAV IN/OUT interval");return false;}
    if(pt_path_is_uri(destination.c_str())){error("Export needs a new native cache file; import it to the document folder afterwards");return false;}
    const uint32_t epoch=p.cancelEpoch.load();p.exporting=true;p.exportProgress=0;
    bool owned=false;FILE* output=nullptr;
#if !defined(_WIN32)
    struct stat identity{};bool haveIdentity=false;
#endif
    try{
        File input(pt_fopen(m.path.c_str(),"rb"));if(!input.p)throw std::runtime_error("Could not reopen source WAV");
        // Detect externally replaced/truncated source metadata before creating output.
        const Meta actual=parse(input.p,m.path);
        if(actual.frames!=m.frames||actual.dataOffset!=m.dataOffset||actual.fmt!=m.fmt||actual.bytes!=m.bytes)throw std::runtime_error("Source WAV changed; reopen it before export");
        if(!seek_file(input.p,m.dataOffset+first*m.align))throw std::runtime_error("Cannot seek selected WAV interval");
        std::vector<int64_t> cues;for(int64_t cue:m.cues)if(cue>=first&&cue<end)cues.push_back(cue-first);
        const uint64_t bytes=uint64_t(end-first)*uint32_t(m.align);
        const uint64_t cueBytes=cues.empty()?0:8+4+24*cues.size();
        const uint64_t riff=4+8+m.fmt.size()+(m.fmt.size()&1)+uint64_t(m.isFloat?12:0)+8+bytes+(bytes&1)+cueBytes;
        if(bytes>UINT32_MAX||riff>UINT32_MAX)throw std::runtime_error("Selected WAV exceeds the RIFF size limit");
        if(p.cancelEpoch.load()!=epoch||!pt::load_tick(0.f))throw std::runtime_error("WAV export cancelled");
        output=pt_fopen(destination.c_str(),"wbx");if(!output)throw std::runtime_error("Excerpt file already exists or cannot be created exclusively");owned=true;
#if !defined(_WIN32)
        haveIdentity=fstat(fileno(output),&identity)==0;
#endif
        write_exact(output,"RIFF",4);write_u32(output,uint32_t(riff));write_exact(output,"WAVEfmt ",8);write_u32(output,uint32_t(m.fmt.size()));write_exact(output,m.fmt.data(),m.fmt.size());
        const unsigned char zero=0;if(m.fmt.size()&1)write_exact(output,&zero,1);
        if(m.isFloat){write_exact(output,"fact",4);write_u32(output,4);write_u32(output,uint32_t(end-first));}
        write_exact(output,"data",4);write_u32(output,uint32_t(bytes));
        std::array<unsigned char,32768> block{};uint64_t done=0;
        while(done<bytes){
            if(p.cancelEpoch.load()!=epoch)throw std::runtime_error("WAV export cancelled");
            const size_t count=size_t(std::min<uint64_t>(block.size(),bytes-done));read_exact(input.p,block.data(),count);write_exact(output,block.data(),count);done+=count;
            p.exportProgress=uint32_t(done*1000000/bytes);
            if(!pt::load_tick(float(double(done)/bytes)))throw std::runtime_error("WAV export cancelled");
        }
        if(bytes&1)write_exact(output,&zero,1);
        if(!cues.empty()){
            write_exact(output,"cue ",4);write_u32(output,uint32_t(4+24*cues.size()));write_u32(output,uint32_t(cues.size()));
            uint32_t id=0;for(int64_t cue:cues){write_u32(output,++id);write_u32(output,uint32_t(cue));write_exact(output,"data",4);write_u32(output,0);write_u32(output,0);write_u32(output,uint32_t(cue));}
        }
        if(p.cancelEpoch.load()!=epoch||!pt::load_tick(1.f))throw std::runtime_error("WAV export cancelled");
        if(std::fflush(output)!=0)throw std::runtime_error("Could not finish WAV excerpt");
        const int rc=std::fclose(output);output=nullptr;if(rc)throw std::runtime_error("Could not close WAV excerpt");
        p.exportProgress=1000000;p.exporting=false;return true;
    }catch(const std::exception& e){error(e.what());}catch(...){error("WAV export failed");}
    if(output)std::fclose(output);
    if(owned){
#if !defined(_WIN32)
        struct stat current{};if(haveIdentity&&lstat(destination.c_str(),&current)==0&&current.st_dev==identity.st_dev&&current.st_ino==identity.st_ino)std::remove(destination.c_str());
#else
        std::remove(destination.c_str());
#endif
    }
    p.exporting=false;return false;
}
} // namespace pt
