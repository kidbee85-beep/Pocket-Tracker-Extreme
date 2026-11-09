#include "linux/source-sampler.h"
#include "songcore/project_io.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using json=songcore::json;
using namespace std::chrono_literals;
static json state(pt::platform::SourceSampler& s){return json::parse(s.state_json());}
template<class F> json wait(pt::platform::SourceSampler& s,F expected,int seconds=230){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
    while(std::chrono::steady_clock::now()<deadline){auto j=state(s);if(j.value("status","")=="error")throw std::runtime_error(j.value("error","Source error"));if(expected(j))return j;std::this_thread::sleep_for(15ms);}throw std::runtime_error("Online source test timed out");
}
int main(int argc,char** argv){
    if(argc<4)return 1;const std::string provider=argv[2];json result={{"provider",provider},{"query",argv[3]}};
    try{
        pt::platform::SourceSampler source(argv[1]);auto send=[&](const char* action,const json& args=json::object()){if(!source.command(action,args.dump()))throw std::runtime_error("Source command refused");};
        send("search",{{"provider",provider},{"query",argv[3]},{"genre",argc>4?argv[4]:""}});
        auto j=wait(source,[](const json& j){return j.value("status","")=="results"&&!j.value("busy",false);});result["results"]=j["results"].size();if(j["results"].empty())throw std::runtime_error("No search results");
        int selected=0;if(provider=="commons")for(size_t i=0;i<j["results"].size();++i)if(j["results"][i].value("title","").find("Livery")!=std::string::npos){selected=int(i);break;}
        send("open",{{"index",selected}});j=wait(source,[](const json& j){return j.value("status","")=="tracks";});result["tracks"]=j["tracks"].size();if(j["tracks"].empty())throw std::runtime_error("No playable tracks");
        send("listen",{{"index",0}});j=wait(source,[](const json& j){return j.value("playing",false)&&!j.value("busy",false);});result["source_url"]=j["source_url"];result["title"]=j["title"];result["duration_ms"]=j["duration_ms"];
        send("seek",{{"position_ms",1000}});wait(source,[](const json& j){return j.value("position_ms",0)==1000;});
        std::vector<float> output(480*2);double energy=0;for(int i=0;i<50;++i){std::fill(output.begin(),output.end(),0.f);source.render_audio(output.data(),480,48000);for(float sample:output)energy+=sample*sample;std::this_thread::sleep_for(2ms);}result["preview_energy"]=energy;if(energy<=0)throw std::runtime_error("Preview did not produce audible PCM");
        send("pause");j=wait(source,[](const json& j){return j.value("status","")=="paused";});result["preview_position_ms"]=j["position_ms"];
        send("set_region",{{"in_ms",1000},{"out_ms",2000}});wait(source,[](const json& j){return j.value("in_ms",0)==1000&&j.value("out_ms",0)==2000;});
        send("render");j=wait(source,[](const json& j){return j.value("clip_ready",false);});result["clip_frames"]=j["clip_frames"];result["clip_rate"]=j["clip_rate"];result["clip_path"]=j["clip_path"];if(j["clip_frames"]!=j["clip_rate"])throw std::runtime_error("Rendered region has the wrong frame count");result["success"]=true;
    }catch(const std::exception& e){result["success"]=false;result["error"]=e.what();std::cout<<result.dump(2)<<'\n';return 2;}
    std::cout<<result.dump(2)<<'\n';return 0;
}
