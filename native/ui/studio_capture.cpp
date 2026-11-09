#include "ui/studio.h"
#include "ui/std_filesystem.h"
#include <algorithm>
#include <cmath>

namespace pt::ui {
bool studio_poll_capture(AppState& s,uint64_t now){
 auto& v=s.capture;
 if(!s.captureBackend||now<v.nextPoll||(!v.active()&&!(s.studioOpen&&s.studioTab==4)))return false;
 v.nextPoll=now+100;
 try{
  const auto data=songcore::json::parse(s.captureBackend->state_json());
  if(!data.is_object())return false;
  v.state=data.value("state",std::string("idle"));v.message=data.value("message",std::string());
  const auto nextPath=data.value("path",std::string());
  if(!nextPath.empty()&&nextPath!=v.path)v.importedPath.clear();
  v.path=nextPath;v.routedName=data.value("routedName",std::string());
  v.frames=std::max<int64_t>(0,data.value("frames",int64_t(0)));
  v.actualRate=std::max(1,data.value("rate",48000));v.actualChannels=std::clamp(data.value("channels",1),1,2);
  v.requestedId=data.value("requestedId",0);v.routedId=data.value("routedId",0);
  const auto peak=[&](const char* name){const float p=data.value(name,0.f);return std::isfinite(p)?std::clamp(p,0.f,1.f):0.f;};
  v.peakL=peak("peakL");v.peakR=peak("peakR");
  if(v.inputs.empty())studio_capture_refresh(s);
 }catch(...){v.message="Input status unavailable";}
 return true;
}
void studio_capture_refresh(AppState& s){
 if(!s.captureBackend)return;
 try{
  auto data=songcore::json::parse(s.captureBackend->inputs_json());
  if(!data.is_array())return;
  const int old=s.capture.inputs.empty()?0:s.capture.inputs[std::clamp(s.capture.selected,0,int(s.capture.inputs.size())-1)].id;
  std::vector<CaptureInput> inputs;
  for(const auto& entry:data)if(entry.is_object()&&entry.contains("id")&&entry.contains("name"))
   inputs.push_back({entry.at("id").get<int>(),entry.at("name").get<std::string>(),entry.value("kind",std::string("input"))});
  s.capture.inputs=std::move(inputs);s.capture.selected=0;
  for(size_t i=0;i<s.capture.inputs.size();++i)if(s.capture.inputs[i].id==old)s.capture.selected=static_cast<int>(i);
 }catch(...){s.capture.message="Input list unavailable";}
}
void studio_capture_action(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs){
 auto& v=s.capture;
 if(!s.captureBackend){s.studioMessage="Recording is unavailable on this platform";return;}
 if(s.studioRow==6){studio_capture_refresh(s);return;}
 if(s.studioRow==4){
  if(v.active())s.captureBackend->stop();
  else{
   studio_capture_refresh(s);
   if(v.inputs.empty()){s.studioMessage="No audio input detected";return;}
   host.stop_preview();engine.studioRack.punch.store(0);
   v.importedPath.clear();
   const auto& selected=v.inputs[std::clamp(v.selected,0,int(v.inputs.size())-1)];
   s.captureBackend->start(selected.id,v.channels,v.rate);
  }
  v.nextPoll=0;studio_poll_capture(s,0);return;
 }
 if(s.studioRow!=5)return;
 if(host.is_playing()){s.studioMessage="Stop playback before importing the take";return;}
 if(v.active()){s.studioMessage="Stop the recording before importing";return;}
 if(v.state!="ready"&&v.state!="error"){s.studioMessage="No completed recording available";return;}
 if(v.path.empty()||v.frames<=0){s.studioMessage="Record a take first";return;}
 studio_import_take(s,host,engine,fs,v.path,v.importedPath,"Recorded take","Samples/Recordings");
}
bool studio_import_take(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs,
                        const std::string& sourcePath,std::string& importedPath,
                        const std::string& name,const std::string& folder){
 if(host.is_playing()){s.studioMessage="Stop playback before importing the take";return false;}
 if(sourcePath.empty()||!fs.file_exists(sourcePath)){s.studioMessage="Completed audio file unavailable";return false;}
 host.stop_preview();engine.studioRack.punch.store(0);
 std::string target=importedPath;
 if(target.empty()||!fs.file_exists(target)){
  const auto dir=fs.make_app_folder(folder);
  if(dir.empty()){s.studioMessage="Choose an app folder before importing";return false;}
  const auto stem=path_sanitize(path_stem(sourcePath),false);
  target=dir+"/"+stem+".wav";
  for(int suffix=1;fs.file_exists(target)&&suffix<1000;++suffix)target=dir+"/"+stem+"-"+std::to_string(suffix)+".wav";
  if(!fs.copy_file(sourcePath,target)){s.studioMessage="Could not save take - original kept";return false;}
  importedPath=target;
 }
 const auto credits=sourcePath+".source.json";
 if(fs.file_exists(credits)&&!fs.file_exists(target+".source.json")&&!fs.copy_file(credits,target+".source.json")){
  // A refused/short write can leave a partial new sidecar. Do not let a retry
  // mistake that file for a successfully preserved source record.
  fs.delete_path(target+".source.json");
  s.studioMessage="Clip saved; source details could not be copied - retry import";return false;
 }
 if(!host.load_sample(s.currentInstrument,target)){s.studioMessage="Take saved; instrument load failed";return false;}
 host.set_instrument_type(s.currentInstrument,songcore::InstrumentType::SAMPLER);
 auto& ins=host.edit_project().instruments[s.currentInstrument];
 ins=songcore::Instrument(s.currentInstrument);
 ins.sampleId=s.currentInstrument;ins.sampleFilePath=target;
 ins.name=Canvas::clip_text(name.empty()?"Source clip":name,32);ins.volume=170;
 host.push_instrument(s.currentInstrument);++s.projectVersion;
 s.studioMessage="Take loaded into slot "+std::to_string(s.currentInstrument)+" - open EDIT";
 return true;
}
} // namespace pt::ui
