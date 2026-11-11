#include "audio-engine.h"
#include "ui/live_resample.h"
#include "ui/studio.h"
#include "ui/std_filesystem.h"
#include "songcore/project_io.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <thread>

namespace { thread_local bool guard=false;std::atomic<int> allocations{0}; }
void* operator new(std::size_t n){if(guard)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
using namespace pt::ui;
namespace {
int16_t q(float x){return std::isfinite(x)?static_cast<int16_t>(std::clamp(int(std::lround(std::clamp(x,-1.f,1.f)*32768.f)),-32768,32767)):0;}
std::string bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
void wait_save(AppState& s,AudioEngine& e,FileSystem& fs){
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(s.resampleView.saving&&std::chrono::steady_clock::now()<until){live_resample_poll(s,e,fs);std::this_thread::yield();}
    assert(!s.resampleView.saving&&!s.resampleView.failed);
}
void core(){
    for(int rate:{8000,44100,48000,96000,192000}){
        LiveResampler r;assert(r.start(rate,257));float x[256*2];
        for(int i=0;i<512;++i)x[i]=std::sin(float(i)*.17f)*.7f;
        guard=true;r.append(x,256,2,rate);r.append(x,256,2,rate);guard=false;
        assert(!r.recording()&&r.frames()==257);r.stop();
        for(int i=0;i<514;++i)assert(r.pcm()[i]==q(x[i%512]));
        assert(r.start(rate,10));float mono[]={-2,-1,0,.5f,1,2,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),.1f,-.1f};
        guard=true;r.append(mono,10,1,rate);guard=false;r.stop();
        for(int i=0;i<10;++i)assert(r.pcm()[2*i]==q(mono[i])&&r.pcm()[2*i+1]==q(mono[i]));
        assert(r.start(rate,4));r.append(mono,2,1,rate==48000?44100:48000);
        assert(!r.recording()&&r.rate_changed()&&r.frames()==0);
    }
    assert(allocations==0);
    LiveResampler r;std::atomic<bool> run{true};float audio[128]{};
    std::thread callback([&]{while(run.load()){guard=true;r.append(audio,64,2,48000);guard=false;}});
    for(int pass=0;pass<200;++pass){assert(r.start(48000,4096));std::this_thread::yield();r.stop();
        const auto snap=r.pcm();for(int i=0;i<4;++i)std::this_thread::yield();assert(snap==r.pcm());}
    run=false;callback.join();assert(allocations==0);
}
struct Fixture {
    std::unique_ptr<AudioEngine> engine=std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(),48000};AppState state;StdFileSystem fs;
    explicit Fixture(const std::filesystem::path& root):fs(root.string()){
        engine->setDeviceSampleRate(48000);auto p=songcore::make_default_project();
        p.tracks[0].chainRefs={0};p.chains[0].phraseRefs[0]=0;
        p.phrases[0].steps[0].note=songcore::Note::C4();p.phrases[0].steps[0].instrument=0;
        p.instruments[0].sampleFilePath=studio::custom_uri(103,50,80,100);p.instruments[0].loopMode="fwd";
        assert(host.push_project(songcore::serialize_project(p)));assert(host.load_media("").failed==0);host.push_params();
        state.project=&host.edit_project();host.play_song(0);host.poll();
    }
};
void engine_capture(const std::filesystem::path& root){
    Fixture f(root);const std::string project=songcore::serialize_project(f.host.project());
    // New global shortcut consumes transport, and capture works outside Studio.
    ButtonMods mods;mods.l=mods.r=true;
    assert(studio_button({Button::START,ButtonAction::PRESSED,mods},f.state,f.host,*f.engine,f.fs));
    assert(f.state.resampleView.active&&f.host.is_playing()&&!f.state.studioOpen);
    std::vector<float> heard;heard.reserve(8*256*2);float output[512];
    for(int b=0;b<8;++b){if(b==3)f.engine->studioRack.punch=4;if(b==6)f.engine->studioRack.punch=0;
        guard=true;f.engine->processLiveBlock(output,256,2,48000);guard=false;
        heard.insert(heard.end(),output,output+512);}
    assert(allocations==0&&f.engine->liveResampler.frames()==8*256);
    f.engine->liveResampler.stop();const auto& pcm=f.engine->liveResampler.pcm();
    double energy=0;for(size_t i=0;i<heard.size();++i){assert(pcm[i]==q(heard[i]));energy+=heard[i]*heard[i];}assert(energy>.001);
    assert(studio_button({Button::START,ButtonAction::PRESSED,mods},f.state,f.host,*f.engine,f.fs));
    assert(f.host.is_playing());wait_save(f.state,*f.engine,f.fs);
    const auto wav=bytes(f.state.resampleView.path);assert(wav.size()==44+heard.size()*2);
    for(size_t i=0;i<heard.size();++i){const uint16_t raw=static_cast<unsigned char>(wav[44+i*2])|(static_cast<unsigned char>(wav[45+i*2])<<8);assert(static_cast<int16_t>(raw)==q(heard[i]));}
    assert(songcore::serialize_project(f.host.project())==project);
    // A running song, edited sample, and occupied instruments are never replaced.
    live_resample_use(f.state,f.host,*f.engine,f.fs);assert(f.state.currentInstrument==0);
    f.host.stop();f.state.currentScreen=ScreenType::SAMPLE_EDITOR;f.state.sampleEditor.isModified=true;
    live_resample_use(f.state,f.host,*f.engine,f.fs);assert(f.state.currentInstrument==0);
    f.state.sampleEditor.isModified=false;live_resample_use(f.state,f.host,*f.engine,f.fs);
    assert(f.state.currentInstrument==1&&f.state.pendingSampleEditor);
    assert(f.host.project().instruments[0].sampleFilePath==studio::custom_uri(103,50,80,100));
    AppState relaunched;live_resample_poll(relaunched,*f.engine,f.fs);
    assert(relaunched.resampleView.frames==2048&&relaunched.resampleView.path==f.state.resampleView.path);
    // Known insufficient physical-memory budget refuses before touching a new PCM allocation,
    // preserving the previous ready take and all project data (old Android overcommit risk).
    const auto readyPath=f.state.resampleView.path;const auto readyFrames=f.state.resampleView.frames;
    const auto readyBytes=bytes(readyPath);const char* oldLimit=std::getenv("PT_MEMORY_LIMIT_MB");
    const bool hadLimit=oldLimit!=nullptr;const std::string previousLimit=oldLimit?oldLimit:"";
#if defined(_WIN32)
    _putenv_s("PT_MEMORY_LIMIT_MB","1");
#else
    setenv("PT_MEMORY_LIMIT_MB","1",1);
#endif
    live_resample_toggle(f.state,f.host,*f.engine,f.fs);
    assert(!f.state.resampleView.active&&f.state.resampleView.path==readyPath&&f.state.resampleView.frames==readyFrames);
    assert(bytes(readyPath)==readyBytes&&f.state.resampleView.message.find("RAM")!=std::string::npos);
#if defined(_WIN32)
    _putenv_s("PT_MEMORY_LIMIT_MB",hadLimit?previousLimit.c_str():"");
#else
    if(hadLimit)setenv("PT_MEMORY_LIMIT_MB",previousLimit.c_str(),1);else unsetenv("PT_MEMORY_LIMIT_MB");
#endif
    // Fixed musical lengths are exact, start immediately, and stop without UI timing.
    f.state.resampleView.lengthIndex=1;f.host.edit_project().tempo=120;
    live_resample_toggle(f.state,f.host,*f.engine,f.fs);assert(f.engine->liveResampler.limit()==96000);
    for(int i=0;i<376;++i)f.engine->processLiveBlock(output,256,2,48000);
    live_resample_poll(f.state,*f.engine,f.fs);wait_save(f.state,*f.engine,f.fs);
    assert(f.state.resampleView.frames==96000);
    const auto instruments=f.host.project().instruments;
    for(auto& instrument:f.host.edit_project().instruments)instrument.sampleFilePath=studio::custom_uri(103,50,80,100);
    const auto full=songcore::serialize_project(f.host.project());
    live_resample_use(f.state,f.host,*f.engine,f.fs);
    assert(songcore::serialize_project(f.host.project())==full);
    f.host.edit_project().instruments=instruments;
    // A failed publication keeps the exact take in memory. Retry uses a fresh
    // filename and must leave a colliding external partial file untouched.
    f.state.resampleView.lengthIndex=0;live_resample_toggle(f.state,f.host,*f.engine,f.fs);
    f.engine->processLiveBlock(output,256,2,48000);
    const auto collision=f.state.resampleView.path+".part";{std::ofstream file(collision);file<<"USER FILE";}
    live_resample_stop(f.state,*f.engine,f.fs);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(f.state.resampleView.saving&&std::chrono::steady_clock::now()<until){live_resample_poll(f.state,*f.engine,f.fs);std::this_thread::yield();}
    assert(f.state.resampleView.failed&&f.state.resampleView.frames==256&&bytes(collision)=="USER FILE");
    live_resample_toggle(f.state,f.host,*f.engine,f.fs);wait_save(f.state,*f.engine,f.fs);
    assert(bytes(collision)=="USER FILE"&&f.state.resampleView.path+".part"!=collision);
    // Offline rendering ends the live take and contributes no exported frames.
    assert(f.engine->liveResampler.start(48000,1024));f.engine->processLiveBlock(output,256,2,48000);
    f.engine->setOfflineRendering(true);f.engine->renderOffline(256,output,48000);
    assert(!f.engine->liveResampler.recording()&&f.engine->liveResampler.frames()==256);
    f.engine->setOfflineRendering(false);
    // Metronome clicks are excluded, even while the monitor output contains them.
    f.host.stop();for(int i=0;i<100;++i)f.engine->processLiveBlock(output,256,2,48000);
    f.engine->setMetronome(true,.5f);f.engine->startMetronome(f.engine->getCurrentFrame(),24000);
    assert(f.engine->liveResampler.start(48000,256));f.engine->processLiveBlock(output,256,2,48000);f.engine->liveResampler.stop();
    bool click=false;for(int i=0;i<512;++i){click|=std::abs(output[i])>.001f;assert(std::abs(f.engine->liveResampler.pcm()[i])<=1);}assert(click);
}

void synth_capture(const std::filesystem::path& root) {
    for (int model = studio::kOctaveOrbitModel; model < studio::model_count(); ++model) {
        Fixture f(root / ("synth-" + std::to_string(model)));
        f.host.stop();
        float output[512]{};
        for (int block = 0; block < 128; ++block) f.engine->processLiveBlock(output,256,2,48000);
        f.state.currentInstrument=4;
        open_studio_synth(f.state, f.host.project().instruments[4]);
        f.state.studioModel=model;
        f.state.studioVariation=0;
        f.state.resampleView.lengthIndex=1;
        const auto untouched=songcore::serialize_project(f.host.project());
        ButtonMods shoulders;shoulders.l=shoulders.r=true;
        auto event=[&](Button button, ButtonAction action, ButtonMods mods={}) {
            return studio_button({button,action,mods},f.state,f.host,*f.engine,f.fs);
        };
        assert(event(Button::START,ButtonAction::PRESSED,shoulders));
        assert(f.state.resampleView.active&&f.state.resampleView.ownsSynthRoot&&f.state.studioAuditionHeld);
        assert(event(Button::L_SHIFT,ButtonAction::RELEASED));
        double energy=0;
        guard=true;
        for(int block=0;block<280;++block){
            f.engine->processLiveBlock(output,256,2,48000);
            if(block>35)for(float value:output)energy+=double(value)*value;
        }
        guard=false;
        assert(allocations==0&&energy>0.1&&f.state.studioAuditionHeld);
        assert(event(Button::START,ButtonAction::PRESSED,shoulders));
        assert(!f.state.resampleView.active&&!f.state.resampleView.ownsSynthRoot&&!f.state.studioAuditionHeld);
        wait_save(f.state,*f.engine,f.fs);
        assert(songcore::serialize_project(f.host.project())==untouched);
        assert(bytes(f.state.resampleView.path).size()==44+280*256*4);
        ButtonMods back;back.b=true;
        assert(event(Button::B,ButtonAction::PRESSED,back));
        assert(event(Button::DPAD_UP,ButtonAction::PRESSED,back));
        assert(f.state.pendingSampleEditor&&!f.state.studioOpen&&f.state.currentInstrument==1);
        assert(event(Button::DPAD_UP,ButtonAction::PRESSED,back));  // A held arrow cannot escape into the editor.
        assert(event(Button::DPAD_UP,ButtonAction::RELEASED,back));
        assert(event(Button::B,ButtonAction::RELEASED));
        assert(f.state.studioInputSuppress==0);
        const auto& take=f.host.project().instruments[1];
        assert(take.sampleFilePath&&*take.sampleFilePath==f.state.resampleView.path);
        assert(f.host.project().instruments[0].sampleFilePath==studio::custom_uri(103,50,80,100));
        f.host.edit_project().phrases[0].steps[0].instrument=1;
        f.host.play_song(0);
        f.host.poll();
        energy=0;
        for(int block=0;block<64;++block){
            f.engine->processLiveBlock(output,256,2,48000);
            for(float value:output)energy+=double(value)*value;
        }
        assert(energy>.01);
    }
    std::puts("PASS Octave Orbit and three Gravity instruments: root capture, WAV, safe import and chain playback");
}
}
int main(int argc,char** argv){assert(argc==2);const auto root=std::filesystem::path(argv[1]);std::filesystem::create_directories(root);core();engine_capture(root);synth_capture(root);std::cout<<"PASS: post-Punch/master PCM, no callback allocation, bounded/rate-safe stereo capture, control/audio handoff, async WAV, transport/data preservation, free-slot import, relaunch and bar length.\n";}
