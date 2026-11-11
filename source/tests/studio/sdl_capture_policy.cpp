#include "linux/sample-recorder.h"
#include "vendor/nlohmann/json.hpp"
#include <SDL.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <unistd.h>

// This test links the production recorder against only these SDL entry points.
// It opens no host audio device and verifies the produced WAV, not a UI mock.
namespace {
using Json = nlohmann::json;
struct FakeSdl {
    std::mutex mutex;
    std::condition_variable changed;
    bool blockEnumeration=false, enumerationEntered=false;
    bool blockOpen=false, openEntered=false, blockClose=false, closeEntered=false;
    bool initialized=true, named=false, failOpen=false, open=false, paused=true;
    int opens=0, closes=0, defaultOpens=0, chunks=0, enumerations=0;
    SDL_AudioSpec spec{};
} fake;
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
bool active(const Json& s) { const auto v=s.value("state","");return v=="starting"||v=="recording"||v=="stopping"; }
template<class P> Json waitFor(ptshell::SdlSampleRecorder& recorder,P predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(7);
    Json state;
    do {state=Json::parse(recorder.state_json());if(predicate(state))return state;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("Recorder timeout: "+state.dump());
}
template<class P> Json waitInputs(ptshell::SdlSampleRecorder& recorder,P predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    Json rows;
    do {rows=Json::parse(recorder.inputs_json());if(predicate(rows))return rows;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("Input discovery timeout: "+rows.dump());
}
int opens() {std::lock_guard<std::mutex> lock(fake.mutex);return fake.opens;}
uint32_t le32(const std::vector<unsigned char>& bytes,size_t at) {
    return bytes.at(at)|(uint32_t(bytes.at(at+1))<<8)|(uint32_t(bytes.at(at+2))<<16)|(uint32_t(bytes.at(at+3))<<24);
}
void checkWav(const Json& state,const std::filesystem::path& root) {
    require(state.value("state","")=="ready","default probe did not finish a take");
    const uint64_t frames=state.at("frames");require(frames>0,"default probe wrote no frames");
    const std::filesystem::path path=state.at("path").get<std::string>();
    require(path.parent_path()==root/"Recordings","take escaped its recording directory");
    std::ifstream f(path,std::ios::binary);const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});
    require(bytes.size()==44+frames*2,"WAV bytes/frame count mismatch");
    require(std::memcmp(bytes.data(),"RIFF",4)==0&&std::memcmp(bytes.data()+8,"WAVEfmt ",8)==0,"WAV header missing");
    require(bytes.at(20)==1&&bytes.at(22)==1&&bytes.at(34)==16&&le32(bytes,24)==48000,"WAV is not mono48k PCM16");
    require(le32(bytes,40)==frames*2&&le32(bytes,4)==bytes.size()-8,"WAV lengths were not finalized");
    for(size_t i=44;i<bytes.size();i+=2)require(bytes[i]==0xD2&&bytes[i+1]==0x04,"captured PCM bytes changed");
}
void blockedEnumeration(ptshell::SdlSampleRecorder& recorder,bool start) {
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        fake.blockEnumeration=true;fake.enumerationEntered=false;
    }
    auto call=std::async(std::launch::async,[&] {
        if(start)return recorder.start(0,1,48000);
        recorder.inputs_json();return true;
    });
    bool entered=false;
    {
        std::unique_lock<std::mutex> lock(fake.mutex);
        entered=fake.changed.wait_for(lock,std::chrono::seconds(2),[]{return fake.enumerationEntered;});
    }
    const bool responsive=call.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
    // Always release the simulated driver, including on the baseline failure.
    {std::lock_guard<std::mutex> lock(fake.mutex);fake.blockEnumeration=false;}
    fake.changed.notify_all();
    const bool accepted=call.get();
    if(start){recorder.stop();waitFor(recorder,[](const Json& s){return !active(s);});}
    require(entered,"enumeration blocking fixture was not reached");
    require(responsive,start?"Record blocks UI on SDL enumeration":"listing inputs blocks UI on SDL enumeration");
    require(accepted,"responsive operation was refused");
}
struct ReleaseDriver {
    ~ReleaseDriver() {
        {std::lock_guard<std::mutex> lock(fake.mutex);fake.blockEnumeration=fake.blockOpen=fake.blockClose=false;}
        fake.changed.notify_all();
    }
};
template<class F> void responsive(F call,const char* message) {
    auto result=std::async(std::launch::async,call);
    const bool ready=result.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
    if(!ready){
        // A baseline failure must release its mocked driver before unwinding.
        {std::lock_guard<std::mutex> lock(fake.mutex);fake.blockEnumeration=fake.blockOpen=fake.blockClose=false;}
        fake.changed.notify_all();
    }
    result.get();require(ready,message);
}
void blockedDriver(ptshell::SdlSampleRecorder& recorder,const std::filesystem::path& root,bool closing) {
    ReleaseDriver release;
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        fake.blockOpen=!closing;fake.blockClose=closing;fake.openEntered=fake.closeEntered=false;
    }
    require(recorder.start(0,1,48000),"blocked-driver fixture did not start");
    if(closing){
        waitFor(recorder,[](const Json& s){return s.value("frames",uint64_t(0))>=256;});
        recorder.stop();
    }
    {
        std::unique_lock<std::mutex> lock(fake.mutex);
        require(fake.changed.wait_for(lock,std::chrono::seconds(2),[&]{return closing?fake.closeEntered:fake.openEntered;}),"blocked driver fixture was not reached");
    }
    responsive([&]{recorder.inputs_json();},"input list waited for open/close driver");
    responsive([&]{recorder.state_json();},"status waited for open/close driver");
    responsive([&]{recorder.stop();},"Stop waited for open/close driver");
    const int before=opens();
    require(!recorder.start(0,1,48000)&&opens()==before,"outstanding driver allowed a second recording");
    const auto pending=waitFor(recorder,[](const Json& s){return s.value("driverTimedOut",false);});
    require(pending.value("driverPending",false)&&pending.value("state","")=="stopping","driver timeout falsely reported a closed input");
    require(pending.value("driverPhase","")== (closing?"close":"open"),"timeout lost the pending driver phase");
    require(pending.value("message","").find("not responding")!=std::string::npos,"driver timeout was not explained");
    if(closing){auto saved=pending;saved["state"]="ready";checkWav(saved,root);}
    {
        std::lock_guard<std::mutex> lock(fake.mutex);fake.blockOpen=fake.blockClose=false;
    }
    fake.changed.notify_all();
    const auto done=waitFor(recorder,[](const Json& s){return !active(s);});
    require(done.value("state","")=="error"&&!done.value("driverPending",true),"released timed-out driver did not finish safely");
    if(closing){auto saved=done;saved["state"]="ready";checkWav(saved,root);}
    else require(done.value("path","").empty()&&done.value("frames",uint64_t(0))==0,"cancelled blocked open captured audio");
}
void disconnectedActive(ptshell::SdlSampleRecorder& recorder,const std::filesystem::path& root) {
    {std::lock_guard<std::mutex> lock(fake.mutex);fake.named=true;}
    const auto rows=waitInputs(recorder,[](const Json& r){return r.size()==2;});
    require(recorder.start(rows[1].at("id"),1,48000),"named capture did not start");
    waitFor(recorder,[](const Json& s){return s.value("frames",uint64_t(0))>=256;});
    {std::lock_guard<std::mutex> lock(fake.mutex);fake.named=false;}
    auto done=waitFor(recorder,[](const Json& s){return !active(s);});
    require(done.value("state","")=="error"&&done.value("message","").find("disconnected")!=std::string::npos,"active unplug was not detected");
    {std::lock_guard<std::mutex> lock(fake.mutex);require(fake.defaultOpens==0&&fake.opens==1&&fake.closes==1,"active unplug reopened or fell back to default");}
    done["state"]="ready";checkWav(done,root);
}
}

extern "C" {
Uint32 SDLCALL SDL_WasInit(Uint32 flags) {std::lock_guard<std::mutex> lock(fake.mutex);return fake.initialized?(SDL_INIT_AUDIO&flags):0;}
int SDLCALL SDL_GetNumAudioDevices(int capture) {
    std::unique_lock<std::mutex> lock(fake.mutex);
    ++fake.enumerations;
    if(fake.blockEnumeration){fake.enumerationEntered=true;fake.changed.notify_all();fake.changed.wait(lock,[]{return !fake.blockEnumeration;});}
    return capture&&fake.named?1:0;
}
const char* SDLCALL SDL_GetAudioDeviceName(int index,int capture) {
    std::lock_guard<std::mutex> lock(fake.mutex);return capture&&fake.named&&index==0?"USB fixture":nullptr;
}
const char* SDLCALL SDL_GetError() {return "Fixture default capture unavailable";}
SDL_AudioDeviceID SDLCALL SDL_OpenAudioDevice(const char* name,int capture,const SDL_AudioSpec* want,SDL_AudioSpec* got,int allowed) {
    std::unique_lock<std::mutex> lock(fake.mutex);++fake.opens;if(!name)++fake.defaultOpens;
    if(fake.blockOpen){fake.openEntered=true;fake.changed.notify_all();fake.changed.wait(lock,[]{return !fake.blockOpen;});}
    require(capture==SDL_TRUE&&allowed==0,"capture changed its SDL conversion policy");
    require(!name||std::strcmp(name,"USB fixture")==0,"capture opened an unintended device name");
    if(fake.failOpen)return 0;
    fake.open=true;fake.paused=true;fake.spec=*want;*got=*want;fake.chunks=8;return 7;
}
void SDLCALL SDL_PauseAudioDevice(SDL_AudioDeviceID id,int pause) {std::lock_guard<std::mutex> lock(fake.mutex);require(id==7,"invalid capture handle");fake.paused=pause!=0;}
SDL_AudioStatus SDLCALL SDL_GetAudioDeviceStatus(SDL_AudioDeviceID id) {
    std::lock_guard<std::mutex> lock(fake.mutex);return id!=7||!fake.open?SDL_AUDIO_STOPPED:fake.paused?SDL_AUDIO_PAUSED:SDL_AUDIO_PLAYING;
}
Uint32 SDLCALL SDL_GetQueuedAudioSize(SDL_AudioDeviceID) {std::lock_guard<std::mutex> lock(fake.mutex);return Uint32(fake.chunks*256*fake.spec.channels*2);}
Uint32 SDLCALL SDL_DequeueAudio(SDL_AudioDeviceID,void* data,Uint32 maxBytes) {
    std::lock_guard<std::mutex> lock(fake.mutex);if(!fake.chunks)return 0;
    const Uint32 bytes=256*fake.spec.channels*2;require(maxBytes>=bytes,"capture read buffer too small");
    auto* dst=static_cast<unsigned char*>(data);for(Uint32 i=0;i<bytes;i+=2){dst[i]=0xD2;dst[i+1]=0x04;}
    --fake.chunks;return bytes;
}
void SDLCALL SDL_ClearQueuedAudio(SDL_AudioDeviceID) {std::lock_guard<std::mutex> lock(fake.mutex);fake.chunks=0;}
void SDLCALL SDL_CloseAudioDevice(SDL_AudioDeviceID id) {
    std::unique_lock<std::mutex> lock(fake.mutex);require(id==7&&fake.open,"closed invalid capture");
    if(fake.blockClose){fake.closeEntered=true;fake.changed.notify_all();fake.changed.wait(lock,[]{return !fake.blockClose;});}
    fake.open=false;++fake.closes;
}
}

int main(int argc,char** argv) {
    std::filesystem::path root;
    try {
        std::string pattern=(std::filesystem::temp_directory_path()/"trackwerk-capture-policy-XXXXXX").string();
        std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);const auto created=mkdtemp(name.data());
        require(created,"cannot make fixture directory");root=created;
        {
            ptshell::SdlSampleRecorder recorder(root.string());
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            {std::lock_guard<std::mutex> lock(fake.mutex);require(fake.enumerations==0,"constructor probed audio before an explicit input/Record request");}
            if(argc>1){
                const std::string mode=argv[1];
                if(mode=="listing-block"||mode=="start-block")blockedEnumeration(recorder,mode=="start-block");
                else if(mode=="open-block"||mode=="close-block")blockedDriver(recorder,root,mode=="close-block");
                else if(mode=="active-disconnect")disconnectedActive(recorder,root);
                else require(false,"unknown fixture mode");
                std::filesystem::remove_all(root);
                std::cout<<"PASS: "<<mode<<"; mocked SDL only.\n";
                return 0;
            }
            const auto rows=Json::parse(recorder.inputs_json());
            require(rows.size()==1&&rows[0].at("id")==0,"zero enumeration hid the explicit default probe");
            require(rows[0].value("detail","").find("checked when Record")!=std::string::npos,"default row falsely implied verified capture");
            require(opens()==0,"enumerating inputs activated a microphone");
            require(recorder.start(0,1,48000),"explicit default probe refused");
            waitFor(recorder,[](const Json& s){return s.value("frames",uint64_t(0))>=256||!active(s);});
            recorder.stop();const auto take=waitFor(recorder,[](const Json& s){return !active(s);});checkWav(take,root);
            {std::lock_guard<std::mutex> lock(fake.mutex);require(fake.defaultOpens==1&&fake.closes==1,"default handle lifecycle mismatch");fake.failOpen=true;}
            require(recorder.start(0,1,48000),"async failure fixture did not start");
            const auto failure=waitFor(recorder,[](const Json& s){return !active(s);});
            require(failure.value("state","")=="error"&&failure.value("path","").empty()&&failure.value("frames",uint64_t(0))==0,"failed default probe produced a fake take");
            require(failure.value("message","").find("Fixture default capture unavailable")!=std::string::npos,"default failure lost its driver diagnostic");
            {std::lock_guard<std::mutex> lock(fake.mutex);fake.failOpen=false;fake.named=true;}
            const auto named=waitInputs(recorder,[](const Json& r){return r.size()==2;});
            const int usb=named[1].at("id");
            {std::lock_guard<std::mutex> lock(fake.mutex);fake.named=false;}
            const int before=opens();
            const bool queued=recorder.start(usb,1,48000);
            if(queued)waitFor(recorder,[](const Json& s){return !active(s);});
            require(opens()==before,"disconnected USB fell back to a default open");
            require(Json::parse(recorder.state_json()).value("message","").find("no longer connected")!=std::string::npos,"disconnection diagnostic missing");
            {std::lock_guard<std::mutex> lock(fake.mutex);fake.initialized=false;}
            require(Json::parse(recorder.inputs_json()).empty(),"default probe offered without initialized audio");
            require(!recorder.start(0,1,48000)&&opens()==before,"uninitialized recorder opened capture");
        }
        std::filesystem::remove_all(root);
        std::cout<<"PASS: zero-enumeration explicit default probe, no microphone activation on listing, exact PCM WAV, legitimate-open failure, stale USB never falls back, uninitialized guard. Mock SDL only.\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"FAIL capture policy: "<<e.what()<<"\n";
        if(!root.empty())std::filesystem::remove_all(root);
        return 1;
    }
}
