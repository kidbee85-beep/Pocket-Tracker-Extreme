#include "ui/live_resample.h"
#include "ui/live_resample_files.h"
#include "ui/studio.h"
#include "ui/studio_synth_picker.h"
#include "ui/helpers.h"
#include "songcore/project_io.h"
#include "common/platform_memory.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <cerrno>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/stdio.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pt::ui {
namespace live_resample_files {
std::FILE* create_exclusive(const std::string& path){return std::fopen(path.c_str(),"wbx");}
bool publish_no_replace(const std::string& temp,const std::string& destination,
                        std::error_code& error,PublishMode mode){
    namespace fs=std::filesystem;
    if(mode!=PublishMode::EXCLUSIVE_COPY){
        // A link atomically creates a new name without replacing anything on ext4/APFS.
        if(mode==PublishMode::AUTO){
            fs::create_hard_link(temp,destination,error);
            if(!error)return true;
            if(error==std::errc::file_exists)return false;
        }
#if defined(__linux__) && defined(SYS_renameat2)
        // FAT/exFAT has no hard links. Linux's no-replace rename covers that case.
        if(::syscall(SYS_renameat2,AT_FDCWD,temp.c_str(),AT_FDCWD,destination.c_str(),1u)==0){error.clear();return true;}
        error=std::error_code(errno,std::generic_category());
        if(error==std::errc::file_exists)return false;
#elif defined(__APPLE__)
        if(::renamex_np(temp.c_str(),destination.c_str(),RENAME_EXCL)==0){error.clear();return true;}
        error=std::error_code(errno,std::generic_category());
        if(error==std::errc::file_exists)return false;
#elif defined(_WIN32)
        if(::MoveFileW(fs::path(temp).c_str(),fs::path(destination).c_str())){error.clear();return true;}
        error=std::error_code(static_cast<int>(::GetLastError()),std::system_category());
        if(error==std::errc::file_exists)return false;
#endif
    }
    // Some filesystems (including macOS FSKit exFAT) reject both atomic operations.
    // The host copy_file(...none) implementation allowed two concurrent winners in our fixture.
    // Open the new destination exclusively ourselves, then stream only into that owned handle.
    using File=std::unique_ptr<std::FILE,decltype(&std::fclose)>;
    File input(std::fopen(temp.c_str(),"rb"),&std::fclose);
    if(!input){error=std::error_code(errno,std::generic_category());return false;}
    File output(create_exclusive(destination),&std::fclose);
    if(!output){error=std::error_code(errno,std::generic_category());return false;}
    std::array<unsigned char,8192> buffer{};
    bool copied=true;
    while(const size_t count=std::fread(buffer.data(),1,buffer.size(),input.get()))
        if(std::fwrite(buffer.data(),1,count,output.get())!=count){copied=false;break;}
    copied=copied&&!std::ferror(input.get());
    const bool closed=std::fclose(output.release())==0;
    if(!copied||!closed){error=std::make_error_code(std::errc::io_error);return false;}
    // On failure its own partial new destination may remain; no Ready state or recovery index is
    // published, PCM is retained, and retry chooses a fresh path. Never remove a competing entry.
    error.clear();return true;
}
}
struct LiveResampleSave {
    std::thread worker;
    std::atomic<bool> done{false};
    std::vector<int16_t> pcm;
    int frames=0,rate=48000;
    bool ok=false;
    std::string path, manifestPath;
    ~LiveResampleSave(){if(worker.joinable())worker.join();}
};
namespace {
constexpr int lengths[]={0,1,2,4,8};
std::string length_name(const LiveResampleView& v){
    const int bars=lengths[std::clamp(v.lengthIndex,0,4)];
    return bars?std::to_string(bars)+(bars==1?" BAR":" BARS"):"FREE / 60s";
}
void tell(AppState& s,const std::string& message){
    s.resampleView.message=message;s.studioMessage=message;
    s.statusMessage=message;s.statusSuccess=true;
}
void release_synth_root(AppState& s, songcore::SongcoreHost& host) {
    if (!s.resampleView.ownsSynthRoot) return;
    s.resampleView.ownsSynthRoot = false;
    studio_synth_stop_audition(s, host);
}
bool write_take(const LiveResampleSave& job){
    if(job.frames<=0||job.pcm.size()<static_cast<size_t>(job.frames)*2)return false;
    const std::string temp=job.path+".part";
    std::error_code ec;
    std::unique_ptr<std::FILE,decltype(&std::fclose)> file(live_resample_files::create_exclusive(temp),&std::fclose);
    if(!file)return false;
    std::array<unsigned char,44> header{};
    auto tag=[&](int p,const char* t){for(int i=0;i<4;++i)header[p+i]=t[i];};
    auto u32=[&](int p,uint32_t v){for(int i=0;i<4;++i)header[p+i]=static_cast<unsigned char>(v>>(i*8));};
    auto u16=[&](int p,int v){header[p]=v;header[p+1]=v>>8;};
    tag(0,"RIFF");u32(4,36+job.frames*4);tag(8,"WAVE");tag(12,"fmt ");u32(16,16);
    u16(20,1);u16(22,2);u32(24,job.rate);u32(28,job.rate*4);u16(32,4);u16(34,16);
    tag(36,"data");u32(40,job.frames*4);
    bool written=std::fwrite(header.data(),1,header.size(),file.get())==header.size();
    std::array<unsigned char,8192> bytes{};
    for(size_t first=0,total=static_cast<size_t>(job.frames)*2;first<total;){
        const size_t take=std::min(total-first,bytes.size()/2);
        for(size_t i=0;i<take;++i){const uint16_t q=static_cast<uint16_t>(job.pcm[first+i]);bytes[2*i]=q;bytes[2*i+1]=q>>8;}
        if(!written||std::fwrite(bytes.data(),1,take*2,file.get())!=take*2){written=false;break;}
        first+=take;
    }
    const bool closed=std::fclose(file.release())==0;
    if(!written||!closed){std::filesystem::remove(temp,ec);return false;}
    if(!live_resample_files::publish_no_replace(temp,job.path,ec)){std::filesystem::remove(temp,ec);return false;}
    std::filesystem::remove(temp,ec);  // A successful rename already consumed it; a link/copy did not.
    // Recovery metadata is separate from the already completed audio file.
    const std::string indexTemp=job.manifestPath+".tmp";
    std::ofstream index(indexTemp,std::ios::binary|std::ios::trunc);
    if(index){index<<songcore::json{{"path",job.path},{"frames",job.frames},{"rate",job.rate}}.dump();index.close();
        if(index){std::filesystem::rename(indexTemp,job.manifestPath,ec);if(ec)std::filesystem::remove(indexTemp,ec);}}
    return true;
}
void launch_save(AppState& s){
    auto& v=s.resampleView;
    if(!v.job)return;
    if(v.job->worker.joinable())v.job->worker.join();
    v.job->done=false;v.saving=true;v.failed=false;
    // The worker owns the detached PCM and touches no UI/project/FileSystem/JNI.
    LiveResampleSave* job=v.job.get();
    try{job->worker=std::thread([job]{try{job->ok=write_take(*job);}catch(...){job->ok=false;}job->done.store(true,std::memory_order_release);});}
    catch(...){v.saving=false;v.failed=true;tell(s,"Take kept in memory; retry L+R+Start");}
}
std::string take_path(FileSystem& fs){
    // Android's settings parent is app-private native storage, independent of
    // SAF grants. Publishing into the sample library happens on stopped import.
    const std::filesystem::path cache=std::filesystem::path(fs.settings_path()).parent_path()/".live-resample";
    std::error_code ec;std::filesystem::create_directories(cache,ec);if(ec)return {};
    const std::string library=fs.resampled_directory();
    const std::filesystem::path dir=library.find("://")==std::string::npos&&!library.empty()?std::filesystem::path(library):cache;
    std::filesystem::create_directories(dir,ec);if(ec)return {};
    const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for(int suffix=0;suffix<10000;++suffix){const auto p=dir/("LiveTake_"+std::to_string(stamp)+"_"+std::to_string(suffix)+".wav");
        if(!std::filesystem::exists(p,ec)&&!std::filesystem::exists(p.string()+".part",ec)&&!ec)return p.string();}
    return {};
}
}
static void restore_latest(AppState& s,FileSystem& fs){
    auto& v=s.resampleView;if(v.recoveryChecked)return;v.recoveryChecked=true;
    if(v.active||v.saving||v.frames>0)return;
    try{
        std::ifstream index(std::filesystem::path(fs.settings_path()).parent_path()/".live-resample/last-take.json");
        if(!index)return;songcore::json data;index>>data;
        const auto path=data.at("path").get<std::string>();const int frames=data.at("frames").get<int>(),rate=data.at("rate").get<int>();
        if(rate<8000||rate>192000||frames<=0||frames>rate*LiveResampler::MAX_SECONDS)return;
        std::error_code ec;if(std::filesystem::file_size(path,ec)!=44+static_cast<uint64_t>(frames)*4||ec)return;
        std::ifstream wav(path,std::ios::binary);std::array<char,44> h{};wav.read(h.data(),h.size());
        if(!wav||std::string(h.data(),4)!="RIFF"||std::string(h.data()+8,4)!="WAVE"||std::string(h.data()+36,4)!="data")return;
        v.path=path;v.frames=frames;v.rate=rate;v.limit=frames;v.message="Previous resample ready - Punch B+Up loads";
    }catch(...){/* A missing/invalid index never affects project loading. */}
}
void live_resample_toggle(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs){
    auto& v=s.resampleView;
    v.recoveryChecked=true;
    if(v.saving){tell(s,"Saving take in background...");return;}
    if(v.failed&&v.job){
        const auto retry=take_path(fs);
        if(retry.empty()){tell(s,"Storage unavailable; take remains in memory");return;}
        v.path=retry;v.job->path=retry;launch_save(s);return;
    }
    if(v.active){live_resample_stop(s,engine,fs);release_synth_root(s,host);return;}
    if(s.loading.running||s.renderDialog.isOpen||s.qwerty.isOpen){tell(s,"Finish this dialog before resampling");return;}
    const std::string path=take_path(fs);if(path.empty()){tell(s,"Resample storage unavailable");return;}
    const int rate=engine.getSampleRate();
    const int bars=lengths[std::clamp(v.lengthIndex,0,4)];
    const int frames=bars?std::max(1,static_cast<int>(std::llround(double(rate)*240*bars/std::clamp(host.project().tempo,20,999)))):0;
    const int requestedFrames=frames>0?std::min(frames,rate*LiveResampler::MAX_SECONDS):rate*LiveResampler::MAX_SECONDS;
    const int64_t needed=static_cast<int64_t>(requestedFrames)*2*sizeof(int16_t);
    const int64_t available=pt::available_memory_bytes();
    if(available>0&&needed>available){tell(s,"Not enough free RAM for this length; previous take kept");return;}
    const bool soloSynth = s.studioOpen && s.studioTab == 0 && !sample_granular_studio(s)
                          && !s.studioHelp && !s.studioWorkspace.open && !studio_synth_picker_open(s)
                          && !host.is_playing();
    const bool startAudition = soloSynth && !s.studioAuditionHeld;
    if (startAudition) studio_synth_audition(s, host, true);
    if (soloSynth && !s.studioAuditionHeld) { tell(s, "Could not audition this synth"); return; }
    if(!engine.liveResampler.start(rate,frames)){
        if (startAudition) studio_synth_stop_audition(s,host);
        tell(s,"Not enough memory for this resample length");return;
    }
    v.previousPath=v.path;v.previousImportedPath=v.importedPath;
    v.previousFrames=v.frames;v.previousRate=v.rate;v.previousLimit=v.limit;
    v.job.reset();v.active=true;v.saving=false;v.failed=false;v.frames=0;v.rate=rate;
    v.ownsSynthRoot = soloSynth;
    v.limit=engine.liveResampler.limit();v.path=path;v.importedPath.clear();
    tell(s,soloSynth?"REC SYNTH ROOT / L+R+Start to save":"RESAMPLING - L+R+Start stops / saves");
}
void live_resample_stop(AppState& s,AudioEngine& engine,FileSystem& fs){
    auto& v=s.resampleView;if(!v.active)return;
    engine.liveResampler.stop();v.active=false;v.frames=engine.liveResampler.frames();
    if(v.frames<=0){
        engine.liveResampler.take_pcm();
        v.path=v.previousPath;v.importedPath=v.previousImportedPath;
        v.frames=v.previousFrames;v.rate=v.previousRate;v.limit=v.previousLimit;
        tell(s,"No audio frames received; previous take kept");return;
    }
    auto job=std::make_shared<LiveResampleSave>();job->frames=v.frames;job->rate=v.rate;job->path=v.path;
    job->manifestPath=(std::filesystem::path(fs.settings_path()).parent_path()/".live-resample/last-take.json").string();
    job->pcm=engine.liveResampler.take_pcm();v.job=std::move(job);launch_save(s);
    if(v.saving)tell(s,"Saving live take in background...");
}
bool live_resample_poll(AppState& s,AudioEngine& engine,FileSystem& fs,songcore::SongcoreHost* auditionHost){
    restore_latest(s,fs);
    auto& v=s.resampleView;bool changed=false;
    if(v.active){const int frames=engine.liveResampler.frames();changed=frames!=v.frames;v.frames=frames;
        if(!engine.liveResampler.recording()){live_resample_stop(s,engine,fs);changed=true;}}
    if (auditionHost && v.ownsSynthRoot && (!v.active || !s.studioOpen || s.studioTab != 0
            || sample_granular_studio(s) || !s.studioAuditionHeld || auditionHost->is_playing())) {
        release_synth_root(s,*auditionHost);
        changed=true;
    }
    if(v.saving&&v.job&&v.job->done.load(std::memory_order_acquire)){
        v.job->worker.join();v.saving=false;v.failed=!v.job->ok;
        if(v.failed)tell(s,"Save failed; take kept - L+R+Start retries");
        else{std::vector<int16_t>().swap(v.job->pcm);tell(s,"Take ready / B+Up loads into empty slot");}
        changed=true;
    }
    return changed;
}
void live_resample_use(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs,int targetInstrument){
    auto& v=s.resampleView;
    if(v.active||v.saving||v.failed||v.frames<=0||v.path.empty()){tell(s,"Finish and save a resample take first");return;}
    if(host.is_playing()){tell(s,"Stop playback, then B+Up loads the saved take");return;}
    if(s.currentScreen==ScreenType::SAMPLE_EDITOR&&s.sampleEditor.isModified){tell(s,"Save current sample edits first; take is kept");return;}
    int slot=targetInstrument;
    if(slot>=int(host.project().instruments.size())||(slot>=0&&!songcore::instrument_is_free(host.project().instruments[slot]))){tell(s,"Choose an empty target slot; saved resample is kept");return;}
    if(slot<0)for(int i=0;i<int(host.project().instruments.size());++i)
        if(songcore::instrument_is_free(host.project().instruments[i])){slot=i;break;}
    if(slot<0){tell(s,"No free instrument; saved take is kept");return;}
    const int previous=s.currentInstrument;s.currentInstrument=slot;
    if(std::filesystem::path(v.path).parent_path().string()==fs.resampled_directory())v.importedPath=v.path;
    if(studio_import_take(s,host,engine,fs,v.path,v.importedPath,"Live resample","Samples/Resampled")){
        s.studioOpen=false;s.pendingSampleEditor=true;v.message="Resample loaded into "+std::to_string(slot);
    }else s.currentInstrument=previous;
}
void live_resample_cycle_length(AppState& s,int direction){auto& v=s.resampleView;
    if(v.active||v.saving||v.failed){tell(s,"Finish this take before changing length");return;}
    v.lengthIndex=(v.lengthIndex+(direction>0?1:4))%5;tell(s,"Next resample: "+length_name(v));
}
void draw_live_resample_strip(Canvas& c,const AppState& s,int x,int y,int w,int h){
    const auto& v=s.resampleView;const auto& t=s.theme;
    c.panel_rect(x,y,w,h,t.rowEvery4th,with_alpha(v.active?0xffff7777:t.rowCursor,.35f),4);
    char time[32];std::snprintf(time,sizeof(time),"%.1fs",v.rate>0?double(v.frames)/v.rate:0.0);
    const std::string state=v.active?(v.frames>0?"REC ":"WAIT "):v.saving?"SAVING ":v.failed?"RETRY SAVE ":v.frames>0?"TAKE READY ":"RESAMPLE ";
    c.draw_label(state+time+" / "+length_name(v),x+10,y+4,v.active?0xffffaaaa:t.textValue,12);
    c.draw_label("L+R+Start: REC/STOP   B+Up: Use take   B+Down: Length",x+10,y+19,t.textParam,12);
}
void draw_live_resample_indicator(Canvas& c,const AppState& s){
    const auto& v=s.resampleView;if(!v.active&&!v.saving&&!v.failed)return;
    const Argb color=v.failed?0xffffb070:v.saving?s.theme.rowCursor:0xffff7777;
    c.fill_rect(0,477,640,3,with_alpha(color,.20f));
    const int width=v.active&&v.limit>0?std::clamp(int(640.0*v.frames/v.limit),1,640):640;
    c.fill_rect(0,477,width,3,color);
}
}
