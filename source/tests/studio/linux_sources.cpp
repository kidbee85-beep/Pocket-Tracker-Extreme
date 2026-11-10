#include "linux/source-sampler.h"
#include "songcore/project_io.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
using json=songcore::json;
using namespace std::chrono_literals;
static json snapshot(pt::platform::SourceSampler& s){return json::parse(s.state_json());}
template<class F> static json wait(pt::platform::SourceSampler& s,F predicate,int seconds=12){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
    while(std::chrono::steady_clock::now()<deadline){auto j=snapshot(s);if(predicate(j))return j;std::this_thread::sleep_for(5ms);}
    std::cerr<<"Timeout: "<<s.state_json()<<'\n';std::abort();
}
static void send(pt::platform::SourceSampler& s,const std::string& a,const json& args=json::object()){assert(s.command(a,args.dump()));}
static void ready(pt::platform::SourceSampler& s){wait(s,[](const json& j){return !j.value("busy",false)&&j.value("status","")=="ready";});}
static std::vector<unsigned char> bytes(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static uint32_t word(const std::vector<unsigned char>& b,size_t i){return uint32_t(b[i])|(uint32_t(b[i+1])<<8)|(uint32_t(b[i+2])<<16)|(uint32_t(b[i+3])<<24);}
int main(int argc,char** argv){
    assert(argc==3);const std::string root=argv[1],base=std::getenv("PT_SOURCE_TEST_BASE");pt::platform::SourceSampler s(root);
    assert(!s.command("invalid","{}"));assert(!s.command("play","{"));assert(!s.command("play","[]"));
    send(s,"query_dialog");bool requested=false;for(int i=0;i<100&&!requested;++i){requested=s.take_query_request();std::this_thread::sleep_for(5ms);}assert(requested);assert(!s.take_query_request());
    send(s,"search",{{"provider","archive"},{"genre","jazz"},{"query","fixture"}});
    auto j=wait(s,[](const json& j){return j.value("status","")=="results"&&!j.value("busy",false);});assert(j["results"].size()==2);assert(j["results"][0]["description"]=="Real fixture metadata");
    send(s,"open",{{"index",0}});j=wait(s,[](const json& j){return j.value("status","")=="tracks";});assert(j["tracks"].size()==2);
    send(s,"listen",{{"index",0}});j=wait(s,[](const json& j){return j.value("playing",false)&&!j.value("busy",false);});assert(j["duration_ms"]==3000);
    double energy=0;std::vector<float> output(480*2);
    for(int n=0;n<100;++n){std::fill(output.begin(),output.end(),0.f);s.render_audio(output.data(),480,48000);for(float f:output)energy+=f*f;std::this_thread::sleep_for(1ms);}assert(energy>100);j=wait(s,[](const json& j){return j.value("position_ms",int64_t(0))>=980;});assert(j["position_ms"]<=1010);
    send(s,"pause");wait(s,[](const json& j){return j.value("status","")=="paused"&&!j.value("playing",true);});
    std::fill(output.begin(),output.end(),.123f);s.render_audio(output.data(),480,48000);assert(output[0]==.123f);
    send(s,"set_region",{{"in_ms",250},{"out_ms",650}});wait(s,[](const json& j){return j.value("in_ms",0)==250&&j.value("out_ms",0)==650;});
    send(s,"sort",{{"order","title"}});j=wait(s,[](const json& j){return j.value("sort_order","")=="title";});assert(j["in_ms"]==250&&j["out_ms"]==650);assert(j["tracks"][0]["title"]=="A tone");
    send(s,"render");j=wait(s,[](const json& j){return j.value("clip_ready",false);});assert(j["clip_frames"]==17640);assert(j["clip_rate"]==44100);assert(j["clip_channels"]==2);
    const auto clip=bytes(j["clip_path"]),original=bytes(argv[2]);assert(clip.size()==44+17640*4);assert(word(clip,40)==17640*4);assert(std::equal(clip.begin()+44,clip.end(),original.begin()+44+11025*4));assert(std::filesystem::exists(j["clip_path"].get<std::string>()+".source.json"));
    send(s,"seek",{{"position_ms",2000}});wait(s,[](const json& j){return j.value("position_ms",0)==2000;});send(s,"play");wait(s,[](const json& j){return j.value("playing",false);});
    for(int n=0;n<10;++n){s.render_audio(output.data(),480,48000);std::this_thread::sleep_for(2ms);}send(s,"record",{{"enabled",true}});wait(s,[](const json& j){return j.value("recording",false);});
    for(int n=0;n<30;++n){s.render_audio(output.data(),480,48000);std::this_thread::sleep_for(2ms);}send(s,"record",{{"enabled",false}});j=wait(s,[](const json& j){return j.value("clip_ready",false);});assert(j["clip_frames"]>=12800&&j["clip_frames"]<=13671);
    send(s,"search",{{"provider","commons"},{"genre","jazz"},{"query","fail-query"}});wait(s,[](const json& j){return j.value("status","")=="error";});
    send(s,"sort",{{"order","title"}});j=wait(s,[](const json& j){return j.value("status","")=="ready";});assert(j["results"].empty()&&j["tracks"].empty());
    send(s,"search",{{"provider","archive"},{"genre","jazz"},{"query","fixture"}});wait(s,[](const json& j){return j.value("status","")=="results";});
    send(s,"open",{{"index",0}});wait(s,[](const json& j){return j.value("status","")=="tracks";});
    send(s,"open",{{"index",1}});wait(s,[](const json& j){return j.value("status","")=="error";});
    send(s,"sort",{{"order","title"}});j=wait(s,[](const json& j){return j.value("status","")=="ready";});assert(j["tracks"].empty());
    send(s,"search",{{"provider","commons"},{"genre","jazz"},{"query","fixture"}});j=wait(s,[](const json& j){return j.value("status","")=="results";});assert(j["results"].size()==1);assert(j["results"][0]["creator"]=="Fixture Artist");
    send(s,"open",{{"index",0}});wait(s,[](const json& j){return j.value("status","")=="tracks";});send(s,"listen",{{"index",0}});wait(s,[](const json& j){return j.value("playing",false);});
    send(s,"seek",{{"position_ms",2990}});wait(s,[](const json& j){return j.value("position_ms",0)==2990;});
    for(int i=0;i<5;++i){s.render_audio(output.data(),480,48000);std::this_thread::sleep_for(5ms);}wait(s,[](const json& j){return !j.value("playing",true)&&j.value("position_ms",0)==3000;});
    send(s,"listen",{{"index",0}});j=wait(s,[](const json& j){return j.value("playing",false)&&j.value("position_ms",0)==0;});
    send(s,"stop");wait(s,[](const json& j){return j.value("status","")=="stopped";});
    send(s,"test_load",{{"url",base+"/redirect-bad"},{"ext","wav"}});j=wait(s,[](const json& j){return j.value("status","")=="error";});assert(j.value("error","").find("unsupported address")!=std::string::npos);
    send(s,"test_load",{{"url",base+"/broken.wav"},{"ext","wav"}});j=wait(s,[](const json& j){return j.value("status","")=="error"&&j.value("error","").find("WAV")!=std::string::npos;});assert(!j["clip_ready"].get<bool>());
    send(s,"test_load",{{"url",base+"/slow.wav"},{"ext","wav"}});wait(s,[](const json& j){return j.value("busy",false);});auto start=std::chrono::steady_clock::now();send(s,"stop");wait(s,[](const json& j){return !j.value("busy",true)&&j.value("status","")=="stopped";},4);assert(std::chrono::steady_clock::now()-start<3s);
    if(const char* list=std::getenv("PT_SOURCE_CODEC_LIST")){
        std::string formats=list;size_t start=0;while(start<formats.size()){
            const auto end=formats.find(',',start);const auto ext=formats.substr(start,end-start);const auto title="Codec "+ext;
            send(s,"test_load",{{"url",base+"/codec/fixture."+ext},{"ext",ext=="m4a-frag"?"m4a":ext},{"title",title}});j=wait(s,[&](const json& j){return j.value("title","")==title&&!j.value("busy",false);});assert(j["duration_ms"]>=2900&&j["duration_ms"]<=3100);
            send(s,"play");wait(s,[](const json& j){return j.value("playing",false);});double codecEnergy=0;
            for(int n=0;n<30;++n){std::fill(output.begin(),output.end(),0.f);s.render_audio(output.data(),480,48000);for(float f:output)codecEnergy+=f*f;std::this_thread::sleep_for(1ms);}assert(codecEnergy>50);
            send(s,"pause");wait(s,[](const json& j){return j.value("status","")=="paused";});send(s,"set_region",{{"in_ms",250},{"out_ms",650}});wait(s,[](const json& j){return j.value("in_ms",0)==250&&j.value("out_ms",0)==650;});send(s,"render");j=wait(s,[](const json& j){return j.value("clip_ready",false);});const auto rate=j["clip_rate"].get<int>();assert(j["clip_frames"]==650LL*rate/1000-250LL*rate/1000);assert(rate==(ext=="opus"?48000:44100));std::cout<<"PASS codec "<<ext<<" rate="<<rate<<" clip_frames="<<j["clip_frames"]<<" preview_energy="<<codecEnergy<<'\n';
            if(end==std::string::npos)break;start=end+1;
        }
    }
    send(s,"search",{{"provider","youtube"},{"query","fixture; touch /tmp/PTE-INJECTION-SHOULD-NOT-EXIST"},{"genre","jazz"}});j=wait(s,[](const json& j){return j.value("status","")=="results";});assert(j["results"].size()==1);
    send(s,"open",{{"index",0}});wait(s,[](const json& j){return j.value("status","")=="tracks";});send(s,"listen",{{"index",0}});j=wait(s,[](const json& j){return j.value("playing",false);});assert(j["duration_ms"]==3000);assert(!std::filesystem::exists("/tmp/PTE-INJECTION-SHOULD-NOT-EXIST"));
    send(s,"stop");wait(s,[](const json& j){return j.value("status","")=="stopped";});
    send(s,"search",{{"provider","youtube"},{"query","too-large"}});wait(s,[](const json& j){return j.value("status","")=="results";});send(s,"open",{{"index",0}});wait(s,[](const json& j){return j.value("status","")=="tracks";});send(s,"listen",{{"index",0}});j=wait(s,[](const json& j){return j.value("status","")=="error";});assert(j.value("error","").find("bounded")!=std::string::npos||j.value("error","").find("96 MiB")!=std::string::npos);assert(!std::filesystem::exists(std::filesystem::path(root)/"Cache/Digging/source.media"));
    const auto heartbeat=std::filesystem::path(root).parent_path()/"helper-heartbeat";std::filesystem::remove(heartbeat);
    send(s,"search",{{"provider","youtube"},{"query","slow-helper"}});wait(s,[](const json& j){return j.value("busy",false);});
    for(int i=0;i<200&&!std::filesystem::exists(heartbeat);++i)std::this_thread::sleep_for(5ms);assert(std::filesystem::exists(heartbeat));
    send(s,"stop");wait(s,[](const json& j){return j.value("status","")=="stopped"&&!j.value("busy",true);});std::this_thread::sleep_for(100ms);const auto count=std::filesystem::file_size(heartbeat);std::this_thread::sleep_for(100ms);assert(std::filesystem::file_size(heartbeat)==count);
    std::cout<<"PASS Linux source providers, metadata, streaming preview/seek, exact PCM16 17640-frame render, recording, sorting/marker retention, failed-provider/album stale-list prevention, EOF replay, safe helper argv, redirect rejection, corrupt WAV, bounded download size/cancellation and helper-process-group cleanup\n";
}
