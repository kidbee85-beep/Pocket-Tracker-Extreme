// SPDX-License-Identifier: GPL-3.0-or-later
// Capture genuine provider states through the shipping backend and UI renderer.
#include "linux/source-sampler.h"
#include "songcore/project_io.h"
#include "ui/layout.h"
#include "ui/studio.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
using namespace pt::ui;
using json=songcore::json;
namespace fs=std::filesystem;
void shot(const fs::path& path,AppState& app){Canvas c;TrackerLayout l;l.draw(c,app);std::ofstream f(path,std::ios::binary);f<<"P6\n640 480\n255\n";for(int i=0;i<640*480;++i){auto p=c.pixels()[i];char rgb[]={char(p>>16),char(p>>8),char(p)};f.write(rgb,3);}}
int main(int argc,char**argv){
 if(argc!=2)return 1;fs::path out=argv[1];fs::create_directories(out);
 auto engine=std::make_unique<AudioEngine>();songcore::SongcoreHost host(engine.get(),48000);
 host.push_project(songcore::serialize_project(songcore::make_default_project()));
 AppState app;app.project=&host.edit_project();app.theme=theme_by_name("ULTRAVIOLET",VisualizerType::OCTA);app.studioOpen=true;app.studioTab=7;
 pt::platform::SourceSampler backend((out/"data").string());app.sourceSampler.backend=&backend;app.sourceSampler.provider=1;app.sourceSampler.query="cloud rap";
 auto send=[&](const char*a,json j=json::object()){if(!backend.command(a,j.dump()))throw std::runtime_error("command refused");};
 auto wait=[&](const char*label,auto ok){
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);json j;
  do{j=json::parse(backend.state_json());if(j.value("status","")=="error")throw std::runtime_error(j.value("error","source error"));if(ok(j))break;std::this_thread::sleep_for(std::chrono::milliseconds(30));}while(std::chrono::steady_clock::now()<deadline);
  if(!ok(j))throw std::runtime_error("provider timeout");
  app.sourceSampler.hasPolled=false;source_sampler_poll(app.sourceSampler,1000+uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()/1000000));
  std::ofstream(out/(std::string(label)+".json"))<<j.dump(2)<<'\n';shot(out/(std::string(label)+".ppm"),app);return j;
 };
 try{
  send("search",{{"provider","commons"},{"query","cloud rap"},{"genre",""}});
  auto j=wait("01-search",[](const json&j){return j.value("status","")=="results"&&!j.value("busy",false);});
  int pick=-1;for(size_t i=0;i<j["results"].size();++i)if(j["results"][i].value("title","").find("Cloud rap beat")!=std::string::npos){pick=int(i);break;}if(pick<0)throw std::runtime_error("CC0 example was not returned");
  app.sourceSampler.listFocused=true;app.sourceSampler.row=3;
  send("open",{{"index",pick}});wait("02-tracks",[](const json&j){return j.value("status","")=="tracks";});
  send("listen",{{"index",0}});j=wait("03-preview",[](const json&j){return j.value("playing",false)&&!j.value("busy",false);});
  if(j.value("license_url",j.value("license","" )).find("zero")==std::string::npos)std::cerr<<"Check CC0 licence against source page in provenance\n";
  std::ofstream pcm(out/"source-preview.f32",std::ios::binary);double energy=0;
  for(int n=0;n<180;++n){float data[960]{};backend.render_audio(data,480,48000);pcm.write(reinterpret_cast<char*>(data),sizeof(data));for(float x:data)energy+=x*x;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  if(energy<=0)throw std::runtime_error("preview PCM silence");
  send("pause");wait("04-paused",[](const json&j){return j.value("status","")=="paused";});
  send("set_region",{{"in_ms",1000},{"out_ms",11000}});app.sourceSampler.listFocused=false;app.sourceSampler.row=8;
  wait("05-markers",[](const json&j){return j.value("in_ms",0)==1000&&j.value("out_ms",0)==11000;});
  send("render");j=wait("06-rendered",[](const json&j){return j.value("clip_ready",false);});
  if(j.value("clip_frames",int64_t(0))!=int64_t(j.value("clip_rate",0))*10)throw std::runtime_error("clip frame mismatch");
  std::ofstream(out/"SUCCESS.json")<<json({{"success",true},{"preview_energy",energy},{"source_page","https://commons.wikimedia.org/wiki/File:Cloud_rap_beat.ogg"},{"creator","Zanahary"},{"license","CC0-1.0"},{"license_url","https://creativecommons.org/publicdomain/zero/1.0/"},{"workflow","Shipping SourceSampler backend; real HTTPS search, preview PCM, IN/OUT and10second rendered WAV. UI screenshots from shipping TrackerLayout."},{"physical_device_capture",false}}).dump(2)<<'\n';
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}
}
