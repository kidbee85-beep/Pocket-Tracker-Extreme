// A pack check against the shipping instrument loader and audio path, plus listening previews.
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "studio/synth.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using songcore::json;
constexpr int rate=48000,blockFrames=512;
constexpr float minimumPeak=.005f,maximumPeak=1.25f;
constexpr double minimumRms=1e-4;
struct Measurement { double energy=0;float peak=0;size_t samples=0; };
struct Audition {std::vector<float> pcm;json clips=json::array();};
std::string read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("read failed: "+p.string());return {std::istreambuf_iterator<char>(f),{}};}
void require(bool ok,const std::string& what){if(!ok)throw std::runtime_error(what);}
void word(std::ofstream& f,uint16_t x){const char b[]={char(x),char(x>>8)};f.write(b,2);}
void dword(std::ofstream& f,uint32_t x){const char b[]={char(x),char(x>>8),char(x>>16),char(x>>24)};f.write(b,4);}
std::string safe(const std::string& s){std::string out;for(unsigned char c:s)out+=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')?char(c):'-';return out;}
float wav(const std::filesystem::path& p,const std::vector<float>& pcm){
 float peak=0;for(float x:pcm)peak=std::max(peak,std::fabs(x));
 const float gain=peak>.92f?.92f/peak:1.f; // attenuation only: maintain the preset's authored dynamics.
 std::ofstream f(p,std::ios::binary);require(bool(f),"WAV write failed");
 f.write("RIFF",4);dword(f,36+uint32_t(pcm.size()*2));f.write("WAVEfmt ",8);dword(f,16);
 word(f,1);word(f,2);dword(f,rate);dword(f,rate*4);word(f,4);word(f,16);f.write("data",4);dword(f,uint32_t(pcm.size()*2));
 for(float x:pcm)word(f,static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(x*gain,-1.f,1.f)*32767.f))));
 require(bool(f),"WAV data write failed");return gain;
}
void reset(AudioEngine& e){
 e.stopAll();e.clearScheduledNotes();
 for(auto& voice:e.studioVoices)voice.reset();
 e.resetEffectState();
}
Measurement render(AudioEngine& e,int frames,std::vector<float>* pcm){
 float audio[blockFrames*2]{};Measurement m;
 while(frames>0){const int n=std::min(frames,blockFrames);e.processLiveBlock(audio,n,2,rate);
  for(int i=0;i<n*2;++i){require(std::isfinite(audio[i]),"non-finite output");m.peak=std::max(m.peak,std::fabs(audio[i]));m.energy+=double(audio[i])*audio[i];++m.samples;}
  if(pcm)pcm->insert(pcm->end(),audio,audio+n*2);frames-=n;
 }
 return m;
}
}
int main(int argc,char** argv){
 try{
  require(argc==3,"usage: ptpresetpack-check manifest.json outputdir");
  const auto manifest=json::parse(read(argv[1]));const auto& entries=manifest.at("presets");require(entries.is_array()&&!entries.empty(),"empty manifest");
  const std::filesystem::path out(argv[2]);std::filesystem::create_directories(out/"Auditions");
  std::map<std::string,std::vector<size_t>> byCategory;
  for(size_t i=0;i<entries.size();++i)byCategory[entries[i].at("category").get<std::string>()].push_back(i);
  std::set<size_t> auditionIndexes;
  for(auto& [category,ix]:byCategory){for(size_t p:{size_t(0),ix.size()/3,(ix.size()*2)/3,ix.size()-1})auditionIndexes.insert(ix[p]);}
  auto engine=std::make_unique<AudioEngine>();engine->setDeviceSampleRate(rate);
  songcore::SongcoreHost host(engine.get(),rate);
  require(host.push_project(songcore::serialize_project(songcore::make_default_project())),"new project failed");host.push_params();
  std::map<std::string,Audition> auditions;json results=json::array(),failures=json::array();size_t passed=0;
  for(size_t index=0;index<entries.size();++index){
   const auto& entry=entries[index];const std::string path=entry.at("path"),name=entry.at("name"),category=entry.at("category");
   try{
    const auto source=json::parse(read(path));const auto preset=songcore::parse_instrument_preset(source);
    require(preset.version==1,"unsupported preset version");
    require(preset.instrument.name==name,"manifest name differs from instrument");
    require(preset.instrument.sampleFilePath.has_value(),"no source URI");
    const auto uri=*preset.instrument.sampleFilePath;
    const int code=studio::uri_code(uri.c_str());
    require(code>=0,"unrecognized Mutable URI");
    require(preset.instrument.modSlots.size()==4,"expected four modulation slots");
    const auto encoded=songcore::serialize_instrument_preset(preset);
    const auto decoded=songcore::parse_instrument_preset(json::parse(encoded));
    require(songcore::serialize_instrument_preset(decoded)==encoded,"instrument roundtrip changed authored values");
    reset(*engine);
    require(host.load_instrument_preset(0,path),"shipping loader refused preset");
    require(host.project().instruments[0].sampleFilePath==uri,"loader changed source URI");
    require(host.project().instruments[0].name==name,"loader changed name");
    if(index%64==0){
     require(host.load_instrument_preset(7,path),"nonzero slot load failed");
     require(host.project().instruments[7].id==7&&host.project().instruments[7].sampleId==7,"preset was not remapped to slot7");
     require(host.project().instruments[7].sampleFilePath==uri&&host.project().instruments[0].name==name,"slot7 load changed source or slot0");
    }
    const bool selected=auditionIndexes.count(index)!=0;
    const bool drum=entry.value("group",std::string())=="Drums"||path.find("/Drums/")!=std::string::npos;
    const int strikes=studio::preset_model(code)==18?8:drum?3:1;
    std::vector<float> clip;json strikeMeasurements=json::array();
    float worstPeak=maximumPeak,highestPeak=0;double worstRms=1.0,firstRms=0;float firstPeak=0;
    // Reset local voices and all effect histories, but deliberately preserve the global
    // Mutable RNG sequence: sparse drum voices must also work on subsequent strikes.
    for(int strike=0;strike<strikes;++strike){
     reset(*engine);
     const bool capture=selected&&strike==0;
     const int frames=capture?rate:rate*3/4;
     host.preview_instrument(0);
     const auto m=render(*engine,frames,capture?&clip:nullptr);
     host.stop_preview();const auto release=render(*engine,rate/4,capture?&clip:nullptr);
     const double rms=std::sqrt(m.energy/std::max(size_t(1),m.samples));
     if(strike==0){firstPeak=m.peak;firstRms=rms;}
     worstPeak=std::min(worstPeak,m.peak);worstRms=std::min(worstRms,rms);
     highestPeak=std::max({highestPeak,m.peak,release.peak});
     strikeMeasurements.push_back({{"strike",strike+1},{"hold_seconds",double(frames)/rate},{"peak",m.peak},{"rms",rms},{"release_peak",release.peak}});
    }
    // Keep measurements even when the practical audibility check fails, so a weak
    // recipe cannot disappear from the report merely because it was rejected.
    results.push_back({{"path",path},{"name",name},{"category",category},{"model",studio::preset_model(code)},{"peak",firstPeak},{"rms",firstRms},{"worst_peak",worstPeak},{"worst_rms",worstRms},{"highest_peak",highestPeak},{"strike_count",strikes},{"strikes",strikeMeasurements},{"root_midi",songcore::note_to_midi(preset.instrument.root)}});
    require(highestPeak<=maximumPeak,"unexpected master output peak");
    require(worstPeak>=minimumPeak,"rendered peak below practical audibility threshold on at least one strike");
    require(worstRms>=minimumRms,"rendered RMS below practical audibility threshold on at least one strike");
    ++passed;
    if(selected){
     auto& audition=auditions[category];const double start=double(audition.pcm.size())/(rate*2);
     audition.pcm.insert(audition.pcm.end(),clip.begin(),clip.end());
     audition.clips.push_back({{"name",name},{"path",path},{"start_seconds",start},{"duration_seconds",double(clip.size())/(rate*2)},{"recommended_note",entry.value("recommended_note",std::string())}});
     audition.pcm.insert(audition.pcm.end(),rate/4*2,0.f);
    }
   }catch(const std::exception& e){failures.push_back({{"index",index},{"path",path},{"name",name},{"error",e.what()}});std::cerr<<"FAIL "<<path<<": "<<e.what()<<'\n';}
   if((index+1)%128==0)std::cout<<"Checked "<<index+1<<" / "<<entries.size()<<std::endl;
  }
  json auditionManifest=json::array();
  for(auto& [category,a]:auditions){
   const auto filename=safe(category)+".wav";const float gain=wav(out/"Auditions"/filename,a.pcm);
   auditionManifest.push_back({{"category",category},{"file",filename},{"gain",gain},{"clips",a.clips}});
  }
  json report={{"presets",entries.size()},{"categories",byCategory.size()},{"passed",passed},{"failed",failures.size()},{"failures",failures},{"measurements",results},{"sample_rate",rate},{"auditions",auditionManifest},{"thresholds",{{"minimum_peak",minimumPeak},{"minimum_rms",minimumRms},{"maximum_peak",maximumPeak}}},{"method","native .pti parse/roundtrip, shipping host loader; reset local voices/effect histories per strike; preserve global RNG progression; 8 strikes for Plaits particle model18, 3 for other drums, 1 for other synths; finite output and minimum peak/RMS required on every strike; slot7 remap every64th entry; audition WAV captures first strike only"}};
  std::ofstream(out/"validation.json")<<report.dump(2)<<'\n';
  std::ofstream(out/"Auditions"/"index.json")<<auditionManifest.dump(2)<<'\n';
  if(!failures.empty()){std::cerr<<failures.size()<<" preset checks failed\n";return 2;}
  std::cout<<"PASS: "<<entries.size()<<" presets loaded/rendered, "<<byCategory.size()<<" categories, "<<auditions.size()<<" category auditions\n";
 }catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}
}
