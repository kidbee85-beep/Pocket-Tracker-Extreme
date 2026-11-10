// Actual preview PCM onsets against applied native scheduler events; no copied beat implementation.
#include "ui/drumlab.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "audio-engine.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
using namespace songcore;
using namespace pt::ui;
namespace {
struct Notes : IMidiConsumer {
    std::vector<int64_t> frames;
    void consume(const Event& e) override { if(e.type == EV_NOTE_ON) frames.push_back(e.frame); }
    void on_play(const std::string&,const std::string&,int64_t,int,int) override {}
    void on_stop() override {}
};
void u16(std::ofstream& f,unsigned v){const char b[]={char(v),char(v>>8)};f.write(b,2);}
void u32(std::ofstream& f,unsigned v){const char b[]={char(v),char(v>>8),char(v>>16),char(v>>24)};f.write(b,4);}
void impulse(const std::filesystem::path& path){
    std::ofstream f(path,std::ios::binary); f.write("RIFF",4);u32(f,36+128);f.write("WAVEfmt ",8);
    u32(f,16);u16(f,1);u16(f,1);u32(f,48000);u32(f,96000);u16(f,2);u16(f,16);f.write("data",4);u32(f,128);
    for(int i=0;i<64;++i)u16(f,i<48?22000:0);
}
Project project(const std::filesystem::path& sample,int swing,bool custom){
    auto p=make_default_project();normalize_and_migrate(p);p.tempo=128;p.swing=swing;p.masterVolume=255;
    p.reverbWet=0;p.delayWet=0;p.studioMaster[0]=0;
    for(int lane=0;lane<5;++lane){auto& ins=p.instruments[lane];ins.sampleFilePath=sample.string();
        ins.root=Note::C4();ins.volume=210;ins.pan=128;ins.reverbSend=0;ins.delaySend=0;ins.loopMode="off";
        for(auto& slot:ins.modSlots)slot.type=ModType::NONE;p.tracks[lane].volume=255;}
    if(custom)p.grooves[0].steps={16,8,-1};
    return p;
}
DrumLabState grid(){
    DrumLabState s;s.steps=32;s.activeLanes=5;s.swing=31;s.humanize=24;s.seed=12345;
    assert(drumlab_generate(s));
    for(auto& lane:s.grid)lane={};
    // Deliberately retain nonzero LAT including odd rows, so uniform-tic preview bugs are audible.
    for(int i=0;i<s.steps;++i)s.grid[0][i]={100,static_cast<uint8_t>(i%4)};
    return s;
}
std::vector<int64_t> onsets(const std::vector<float>& audio){
    std::vector<int64_t> result;float previous=0;
    for(size_t i=0;i<audio.size();i+=2){const float value=audio[i];
        if(value-previous>.0002f&&(result.empty()||static_cast<int64_t>(i/2)-result.back()>512))result.push_back(i/2);
        previous=value;}
    return result;
}
void check(const std::filesystem::path& source,int rate,int ratio,bool custom,int changedRatio=-1){
    auto engine=std::make_unique<AudioEngine>();engine->setDeviceSampleRate(rate);
    SongcoreHost host(engine.get(),rate);assert(host.push_project(serialize_project(project(source,ratio,custom))));
    assert(host.load_media("").failed==0);host.push_params();auto s=grid();
    const auto groovesBefore=json::parse(serialize_project(host.project()))["grooves"];
    assert(drumlab_apply(s,host));assert(json::parse(serialize_project(host.project()))["grooves"]==groovesBefore);
    const auto& first=host.project().phrases[host.project().chains[host.project().tracks[0].chainRefs[s.appliedSongRow]].phraseRefs[0]];
    assert(first.steps[0].fx2Type==FX_GRV && first.steps[0].fx2Value==(custom?1:0));
    Notes expected;host.router().add_consumer(&expected);host.schedule_song_range(s.appliedSongRow,s.appliedSongRow,nullptr);
    host.stop();assert(expected.frames.size()==32);host.router().remove_consumer(&expected);
    const auto f=frames_per_step(host.project().tempo,rate);
    assert(drumlab_preview(s,host,*engine));assert(s.previewNextStep==1);
    const int64_t start=s.previewStartFrame;
    if(changedRatio>=0){
        host.edit_project().swing=changedRatio;
        for(int i=0;i<32;++i){
            const int pairRatio=i<2?ratio:changedRatio;
            const int64_t length=swing_step_duration(f,pairRatio,(i&1)!=0);
            expected.frames[i]=static_cast<int64_t>(i)*f+((i&1)?f*(pairRatio-50)/50:0)+(i%4)*(length/12);
        }
    }
    const auto document=serialize_project(host.project());
    const int64_t finish=start+32*f;std::vector<float> pcm;float block[256*2];bool checkedDisplay=false;
    while(engine->getCurrentFrame()<finish){
        drumlab_tick(s,host,*engine);engine->processLiveBlock(block,256,2,rate);pcm.insert(pcm.end(),block,block+512);
        const auto now=engine->getCurrentFrame();
        if(!checkedDisplay&&ratio==75&&changedRatio<0&&now>start+f+256&&now<start+f+f/2-256){
            drumlab_tick(s,host,*engine);assert(s.previewDisplayStep==0);checkedDisplay=true;
        }
    }
    const auto actual=onsets(pcm);
    if(actual.size()<expected.frames.size())std::cerr<<"Too few transients: "<<actual.size()<<" ratio "<<ratio<<" rate "<<rate<<'\n';
    assert(actual.size()>=expected.frames.size());
    const int64_t latency=actual[0]-start-expected.frames[0];assert(latency>=0&&latency<128);
    for(size_t i=0;i<expected.frames.size();++i){
        const auto error=actual[i]-start-expected.frames[i]-latency;
        if(std::abs(error)>1)std::cerr<<"Mismatch rate="<<rate<<" swing="<<ratio<<" step="<<i<<" actual="<<actual[i]-start<<" expected="<<expected.frames[i]<<" error="<<error<<'\n';
        assert(std::abs(error)<=1);
    }
    assert(serialize_project(host.project())==document);
    if(ratio==75&&changedRatio<0)assert(checkedDisplay);
    drumlab_stop(s,host,*engine);assert(!s.previewRunning&&s.previewDisplayStep==-1);
    // No stale swing latch/stamps on a fresh Preview.
    assert(drumlab_preview(s,host,*engine));assert(s.previewNextStep==1);
    assert(s.previewSwingRatio==host.project().swing);drumlab_stop(s,host,*engine);
}
void no_straight_slot(const std::filesystem::path& source){
    auto p=project(source,67,true);for(auto& g:p.grooves)g.steps={16,8,-1};
    auto engine=std::make_unique<AudioEngine>();SongcoreHost host(engine.get(),48000);
    assert(host.push_project(serialize_project(p)));assert(host.load_media("").failed==0);auto s=grid();
    const auto before=serialize_project(host.project());assert(!drumlab_preview(s,host,*engine));
    assert(!s.previewRunning&&serialize_project(host.project())==before);
}
void modal_catchup(const std::filesystem::path& source,int ratio){
    auto engine=std::make_unique<AudioEngine>();engine->setDeviceSampleRate(48000);
    SongcoreHost host(engine.get(),48000);assert(host.push_project(serialize_project(project(source,ratio,false))));
    assert(host.load_media("").failed==0);auto s=grid();assert(drumlab_preview(s,host,*engine));
    const auto start=s.previewStartFrame;const auto f=s.previewFramesPerStep;
    float block[512*2];for(int i=0;i<400;++i)engine->processLiveBlock(block,512,2,48000); // UI was busy.
    const auto now=engine->getCurrentFrame();drumlab_tick(s,host,*engine);
    const auto pair=(now-start)/(2*f);int64_t step=pair*2,frame=start+pair*2*f;
    while(frame<now){frame+=swing_step_duration(f,ratio,(step&1)!=0);++step;}
    assert(s.previewStepStartFrames[static_cast<size_t>(step%s.steps)]==frame);
    assert(s.previewNextFrame>now+6000); // Refilled only the normal preview horizon.
    for(auto stamp:s.previewStepStartFrames)if(stamp>=0)assert(stamp>=now-2*f&&stamp<=now+6000);
    if(ratio==50)assert(frame==start+((now-start+f-1)/f)*f); // Exact original ceil behavior.
    drumlab_stop(s,host,*engine);
}
}
int main(int argc,char**argv){
    assert(argc==2);const std::filesystem::path out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
    const auto sample=out/"impulse.wav";impulse(sample);
    for(int rate:{44100,48000})for(int ratio:{50,57,67,75})for(bool custom:{false,true})check(sample,rate,ratio,custom);
    check(sample,48000,67,true,50);check(sample,48000,50,false,75);no_straight_slot(sample);
    modal_catchup(sample,50);modal_catchup(sample,67);modal_catchup(sample,75);
    std::cout<<"PASS: actual Drumlab preview PCM matches applied native note frames/LAT across 50/57/67/75,44.1/48k,custom groove0,live edits,display and restart; no straight slot fails safely\n";
}
