#include "ui/studio.h"
#include "ui/std_filesystem.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pt::ui {
namespace {
int free_capture_slot(const AppState& s){
 if(!s.project)return -1;
 const int count=static_cast<int>(s.project->instruments.size());
 for(int n=0;n<count;++n){const int slot=(std::clamp(s.currentInstrument,0,count-1)+n)%count;
  if(songcore::instrument_is_free(s.project->instruments[slot]))return slot;
 }
 return -1;
}
}
void open_studio_capture(AppState& s){
 s.studioOpen=true;s.studioTab=4;s.studioRow=4;s.studioHelp=false;
 s.studioConfirm=false;s.studioAUsed=false;s.studioBUsed=false;s.studioMessage.clear();
 if(s.capture.targetInstrument<0||!s.project||s.capture.targetInstrument>=int(s.project->instruments.size())||
    !songcore::instrument_is_free(s.project->instruments[s.capture.targetInstrument]))s.capture.targetInstrument=free_capture_slot(s);
 studio_capture_refresh(s);s.capture.nextPoll=0;studio_poll_capture(s,0);
}
void studio_capture_edit(AppState& s,int direction){
 auto& v=s.capture;if(v.active())return;
 if(s.studioRow==0)v.selected=std::clamp(v.selected+direction,0,std::max(0,int(v.inputs.size())-1));
 if(s.studioRow==1)v.channels=std::clamp(v.channels+direction,1,2);
 if(s.studioRow==2)v.rate=direction>0?48000:44100;
 if(s.studioRow==3){if(v.targetInstrument<0)v.targetInstrument=free_capture_slot(s);else v.targetInstrument=std::clamp(v.targetInstrument+direction,0,127);}
}
std::string studio_capture_status(const CaptureView& v){
 if(v.receiving())return "REC - receiving audio";
 if(v.state=="starting"||v.state=="recording")return "Waiting for audio";
 if(v.state=="stopping")return "Saving take...";
 if(v.state=="ready"&&v.frames>0)return "Take ready to edit";
 if(v.state=="permission")return "Allow microphone access";
 if(v.state=="error")return "No audio / input error";
 return v.inputs.empty()?"No input detected":"Ready to record";
}
bool studio_poll_capture(AppState& s,uint64_t now){
 auto& v=s.capture;
 if(!s.captureBackend||now<v.nextPoll||(!v.active()&&!(s.studioOpen&&s.studioTab==4)))return false;
 v.nextPoll=now+100;
 try{
  const auto data=songcore::json::parse(s.captureBackend->state_json());
  if(!data.is_object())return false;
  const bool wasActive=v.active();
  v.state=data.value("state",std::string("idle"));v.message=data.value("message",std::string());
  const auto nextPath=data.value("path",std::string());
  if(!nextPath.empty()&&nextPath!=v.path)v.importedPath.clear();
  v.path=nextPath;v.routedName=data.value("routedName",std::string());
  v.frames=std::max<int64_t>(0,data.value("frames",int64_t(0)));
  v.actualRate=std::max(1,data.value("rate",48000));v.actualChannels=std::clamp(data.value("channels",1),1,2);
  v.requestedId=data.value("requestedId",0);v.routedId=data.value("routedId",0);
  v.phoneDetected=data.value("phoneDetected",false);v.phoneTrusted=data.value("phoneTrusted",false);
  const auto peak=[&](const char* name){const float p=data.value(name,0.f);return std::isfinite(p)?std::clamp(p,0.f,1.f):0.f;};
  v.peakL=v.receiving()?peak("peakL"):0.f;v.peakR=v.receiving()?peak("peakR"):0.f;
  if(wasActive&&!v.active()&&v.frames>0&&!v.path.empty()&&s.studioTab==4)s.studioRow=5;
  if(s.studioOpen&&s.studioTab==4&&!v.active()&&now>=v.nextInputsPoll){v.nextInputsPoll=now+2000;studio_capture_refresh(s);}
 }catch(...){v.message="Input status unavailable";}
 return true;
}
void studio_capture_refresh(AppState& s){
 if(!s.captureBackend){s.capture.inputs.clear();s.capture.inputError="Recording unavailable on this platform";return;}
 try{
  auto data=songcore::json::parse(s.captureBackend->inputs_json());
  if(!data.is_array())throw std::runtime_error("Invalid input list");
  const int old=s.capture.inputs.empty()?0:s.capture.inputs[std::clamp(s.capture.selected,0,int(s.capture.inputs.size())-1)].id;
  std::vector<CaptureInput> inputs;
  for(const auto& entry:data)if(entry.is_object()&&entry.contains("id")&&entry.contains("name"))
   inputs.push_back({entry.at("id").get<int>(),entry.at("name").get<std::string>(),entry.value("kind",std::string("input")),entry.value("detail",std::string()),entry.value("connected",true)});
  s.capture.inputs=std::move(inputs);s.capture.selected=0;s.capture.inputError.clear();
  for(size_t i=0;i<s.capture.inputs.size();++i)if(s.capture.inputs[i].id==old)s.capture.selected=static_cast<int>(i);
 }catch(...){s.capture.inputs.clear();s.capture.inputError="Input list unavailable - refresh and retry";}
}
void studio_capture_action(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs){
 auto& v=s.capture;
 if(!s.captureBackend){s.studioMessage="Recording is unavailable on this platform";return;}
 if(s.studioRow==6){studio_capture_refresh(s);return;}
 if(s.studioRow==4){
  if(v.active())s.captureBackend->stop();
  else{
   studio_capture_refresh(s);
   if(v.inputs.empty()){s.studioMessage=v.inputError.empty()?"No input. USB charging alone is not an audio connection.":v.inputError;return;}
   host.stop_preview();engine.studioRack.punch.store(0);
   const auto& selected=v.inputs[std::clamp(v.selected,0,int(v.inputs.size())-1)];
   bool accepted=false;
   try{accepted=s.captureBackend->start(selected.id,v.channels,v.rate);}catch(...){v.state="error";v.message="Audio input could not start";}
   if(accepted)v.importedPath.clear();
   v.nextPoll=0;studio_poll_capture(s,0);
   if(!accepted&&v.state!="error"&&v.state!="permission"){v.state="error";v.message="Audio input refused to start - refresh and retry";}
   return;
  }
  v.nextPoll=0;studio_poll_capture(s,0);return;
 }
 if(s.studioRow!=5)return;
 if(host.is_playing()){s.studioMessage="Stop playback before importing the take";return;}
 if(v.active()){s.studioMessage="Stop the recording before importing";return;}
 if(v.state!="ready"&&v.state!="error"){s.studioMessage="No completed recording available";return;}
 if(v.path.empty()||v.frames<=0){s.studioMessage="Record a take first";return;}
 if(s.currentScreen==ScreenType::SAMPLE_EDITOR&&s.sampleEditor.isModified){s.studioMessage="Save current sample edits first. B returns; recorded take is kept.";return;}
 if(v.targetInstrument<0)v.targetInstrument=free_capture_slot(s);
 if(v.targetInstrument<0||v.targetInstrument>=int(host.project().instruments.size())||!songcore::instrument_is_free(host.project().instruments[v.targetInstrument])){
  s.studioMessage="Choose an empty target slot - existing instruments are protected";return;
 }
 const int previous=s.currentInstrument;s.currentInstrument=v.targetInstrument;
 if(studio_import_take(s,host,engine,fs,v.path,v.importedPath,"Recorded take","Samples/Recordings")){
  s.studioOpen=false;s.pendingSampleEditor=true;
 }else s.currentInstrument=previous;
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
