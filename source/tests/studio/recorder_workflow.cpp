#include "audio-engine.h"
#include "songcore/project_io.h"
#include "ui/button_mapper.h"
#include "ui/help_text.h"
#include "ui/input_dispatcher.h"
#include "ui/instrument_row_layout.h"
#include "ui/layout.h"
#include "ui/studio.h"
#include "ui/std_filesystem.h"
#include "ui/live_resample.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <chrono>
#include <cmath>
#include <thread>
using namespace pt::ui;
using songcore::json;
namespace {
void wave(const std::filesystem::path& file){
 std::ofstream f(file,std::ios::binary);auto u16=[&](int n){f.put(char(n));f.put(char(n>>8));};
 auto u32=[&](int n){u16(n);u16(n>>16);};
 f.write("RIFF",4);u32(36+4096);f.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(48000);u32(96000);u16(2);u16(16);f.write("data",4);u32(4096);
 for(int i=0;i<2048;++i)u16((i%128-64)*190);assert(f.good());
}
std::string bytes(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
struct Fake final:CaptureBackend{
 json inputs=json::array({{{"id",4},{"name","Built-in microphone"},{"kind","mic"}},{{"id",19},{"name","USB class compliant interface"},{"kind","usb"}},{{"id",1000000},{"name","iPhone USB (experimental)"},{"kind","iphone-action"},{"connected",false},{"detail","USB helper unavailable; reinstall the complete package."}}});
 json state={{"state","idle"},{"frames",0},{"path",""},{"rate",48000},{"channels",1},{"peakL",.8},{"peakR",.6}};
 std::string path;bool accept=true,broken=false,stuckStopping=false;int starts=0,stops=0,id=-1,inputPolls=0,statePolls=0;
 std::string inputs_json()override{++inputPolls;return broken?"not JSON":inputs.dump();}
 std::string state_json()override{++statePolls;return state.dump();}
 bool start(int input,int channels,int rate)override{++starts;id=input;if(accept){state={{"state","starting"},{"frames",0},{"rate",rate},{"channels",channels},{"path",""}};}return accept;}
 void stop()override{++stops;state={{"state",stuckStopping?"stopping":"ready"},{"frames",2048},{"rate",48000},{"channels",1},{"path",path}};}
};
struct Input{
 AppState& s;songcore::SongcoreHost& host;AudioEngine& engine;FileSystem& fs;
 InputDispatcher d;MapperState mapper;uint64_t now=1000;
 Input(AppState& a,songcore::SongcoreHost& h,AudioEngine& e,FileSystem& f):s(a),host(h),engine(e),fs(f),d(a,h,f){}
 void event(Button b,ButtonAction action,ButtonMods mods={}){now+=30;d.set_now(now);ButtonEvent e{b,action,mods};if(!studio_button(e,s,host,engine,fs))handle_button(e,d,mapper,now);}
 void tap(Button b){event(b,ButtonAction::PRESSED);event(b,ButtonAction::RELEASED);}
 void choose(int id){studio_capture_refresh(s);s.studioRow=0;while(s.capture.selected>0)tap(Button::DPAD_LEFT);for(size_t n=0;n<s.capture.inputs.size()&&s.capture.inputs[s.capture.selected].id!=id;++n)tap(Button::DPAD_RIGHT);assert(s.capture.inputs[s.capture.selected].id==id);}
 void poll(){s.capture.nextPoll=0;assert(studio_poll_capture(s,now+=200));}
 void handoff(){assert(s.pendingSampleEditor);s.pendingSampleEditor=false;d.open_imported_sample_editor();}
};
void shot(const std::filesystem::path& path,AppState& s){Canvas c;TrackerLayout layout;layout.draw(c,s);std::ofstream f(path,std::ios::binary);f<<"P6\n640 480\n255\n";for(int i=0;i<640*480;++i){const auto p=c.pixels()[i];const char v[]={char(p>>16),char(p>>8),char(p)};f.write(v,3);}}
}
int main(int argc,char** argv){
 assert(argc==2);const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
 const auto oldFile=out/"original.wav",take=out/"recording.wav";wave(oldFile);wave(take);
 auto engine=std::make_unique<AudioEngine>();songcore::SongcoreHost host(engine.get(),48000);
 assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
 StdFileSystem fs((out/"app-data").string());Fake backend;backend.path=take.string();
 AppState s;s.project=&host.edit_project();s.currentScreen=ScreenType::INSTRUMENT;s.currentInstrument=20;s.captureBackend=&backend;s.theme=theme_studio();
 Input in(s,host,*engine,fs);
 // An empty sampler can record without first finding/loading a sample.
 for(auto type:{songcore::InstrumentType::SAMPLER,songcore::InstrumentType::SOUNDFONT,songcore::InstrumentType::EXTERNAL}){
  host.edit_project().instruments[20].instrumentType=type;s.studioOpen=false;s.instrumentCursorRow=type==songcore::InstrumentType::SOUNDFONT?7:6;s.instrumentCursorColumn=1;
  assert(instrument_cell_at(type,s.instrumentCursorRow,1)==InstrumentCell::RECORD);
  assert(detail::instrument_topic(type,s.instrumentCursorRow,1)==HelpTopic::RECORD_INPUT);
  const auto before=songcore::serialize_project(host.project());in.tap(Button::A);
  assert(s.studioOpen&&s.studioTab==4&&s.studioRow==4&&!s.studioHelp);
  assert(s.capture.internal_source()&&backend.inputPolls==0&&backend.statePolls==0);
  assert(songcore::serialize_project(host.project())==before);in.tap(Button::B);assert(!s.studioOpen);
 }
 host.edit_project().instruments[20].instrumentType=songcore::InstrumentType::SAMPLER;
 s.capture.targetInstrument=20;
 s.instrumentCursorRow=6;shot(out/"instrument-record.ppm",s);in.tap(Button::A);shot(out/"recorder-internal-ready.ppm",s);in.choose(4);s.studioRow=4;shot(out/"recorder-ready.ppm",s);
 // Simple spatial navigation; source changes never change the current instrument.
 in.tap(Button::DPAD_UP);assert(s.studioRow==3);const int beforeSlot=s.currentInstrument;
 in.tap(Button::DPAD_RIGHT);assert(s.currentInstrument==beforeSlot&&s.capture.targetInstrument==21);
 s.studioRow=0;in.tap(Button::DPAD_RIGHT);assert(s.capture.inputs[s.capture.selected].id==19);
 assert(s.studioRow==0);in.tap(Button::DPAD_DOWN);assert(s.studioRow==1);
 // Permission/start failures are explicit, not fake REC or fake meter activity.
 backend.accept=false;s.studioRow=4;in.tap(Button::A);
 assert(backend.starts==1&&s.capture.state=="error"&&!s.capture.active());
 assert(s.capture.message.find("refused")!=std::string::npos&&!s.capture.receiving());
 backend.accept=true;in.tap(Button::A);assert(backend.starts==2&&backend.id==19&&s.capture.state=="starting");
 backend.state["state"]="recording";backend.state["frames"]=0;backend.state["peakL"]=1.;in.poll();
 assert(!s.capture.receiving()&&s.capture.peakL==0&&studio_capture_status(s.capture)=="Waiting for audio");shot(out/"recorder-waiting.ppm",s);
 backend.state["frames"]=2048;backend.state["peakL"]=.75;in.poll();assert(s.capture.receiving()&&s.capture.peakL==.75f);shot(out/"recorder-receiving.ppm",s);
 in.tap(Button::A);assert(backend.stops==1&&s.capture.state=="ready"&&s.studioRow==5&&s.capture.peakL==0);
 in.tap(Button::A);assert(!s.studioOpen&&s.pendingSampleEditor&&s.currentInstrument==21);in.handoff();
 assert(s.currentScreen==ScreenType::SAMPLE_EDITOR&&s.previousScreen==ScreenType::INSTRUMENT);
 assert(s.sampleEditor.instrumentId==21&&s.sampleEditor.totalFrames==2048);
 assert(host.project().instruments[21].sampleFilePath==s.capture.importedPath&&bytes(s.capture.importedPath)==bytes(take));
 // Every new take is distinct and the user's old source/buffer remains intact.
 assert(host.load_sample(20,oldFile.string()));const auto oldDoc=host.project().instruments[20];float oldData[2048];engine->getSampleData(20,oldData);
 s.sampleEditor.cursorRow=12;s.sampleEditor.cursorCol=0;s.sampleEditor.selectionStart=512;s.sampleEditor.selectionEnd=1536;
 in.d.on_button_a();assert(s.sampleEditor.marker_count()==1&&s.sampleEditor.marker_position(0)==512);
 s.sampleEditor.cursorRow=15;s.sampleEditor.cursorCol=1;in.d.on_button_a();assert(s.sampleEditor.isModified);
 float unchanged[2048];engine->getSampleData(20,unchanged);for(int i=0;i<2048;++i)assert(oldData[i]==unchanged[i]);
 assert(host.project().instruments[20].sampleFilePath==oldDoc.sampleFilePath&&bytes(oldFile)==bytes(take));
 const auto edit=s.sampleEditor;const auto editorDoc=songcore::serialize_project(host.project());
 s.sampleEditor.cursorRow=19;s.sampleEditor.cursorCol=4;shot(out/"sample-editor-record.ppm",s);in.tap(Button::A);
 assert(s.studioOpen&&s.studioRow==4&&s.currentScreen==ScreenType::SAMPLE_EDITOR);
 s.studioRow=5;in.tap(Button::A);assert(s.studioOpen&&!s.pendingSampleEditor&&s.sampleEditor.isModified);
 assert(s.studioMessage.find("Save current sample")!=std::string::npos);
 in.tap(Button::B);assert(!s.studioOpen&&s.sampleEditor.selectionStart==edit.selectionStart&&s.sampleEditor.marker_count()==edit.marker_count()&&s.sampleEditor.isModified);
 assert(songcore::serialize_project(host.project())==editorDoc);
 // The editor RECORD action also exists with slicing off; existing indices stay put.
 s.sampleEditor.sliceMethod=SampleEditorModule::SLICE_OFF;s.sampleEditor.cursorCol=3;in.tap(Button::A);assert(s.studioOpen);
 // Occupied targets never overwrite a sample, even after unsaved edits are saved.
 s.sampleEditor.isModified=false;s.capture.targetInstrument=20;s.studioRow=5;in.tap(Button::A);
 assert(s.studioOpen&&s.studioMessage.find("empty target")!=std::string::npos&&host.project().instruments[20].sampleFilePath==oldDoc.sampleFilePath);
 // Broken enumeration retains the selected identity but refuses all starts.
 const int beforeFailure=s.capture.inputs[s.capture.selected].id;
 backend.broken=true;studio_capture_refresh(s);assert(!s.capture.inputs.empty()&&!s.capture.inputError.empty()&&s.capture.inputs[s.capture.selected].id==beforeFailure);s.studioRow=4;const int starts=backend.starts;in.tap(Button::A);assert(backend.starts==starts);
 backend.broken=false;backend.inputs=json::array();studio_capture_refresh(s);in.tap(Button::A);assert(backend.starts==starts&&s.studioMessage.find("disconnected")!=std::string::npos&&s.capture.inputs[s.capture.selected].id==beforeFailure);shot(out/"recorder-no-input.ppm",s);
 backend.inputs=json::array({{{"id",1000000},{"name","iPhone USB (experimental)"},{"kind","iphone-action"},{"connected",false},{"detail","USB helper missing. Reinstall the complete Brick package."}}});studio_capture_refresh(s);s.studioRow=0;in.tap(Button::DPAD_LEFT);assert(s.capture.inputs[s.capture.selected].id==1000000);shot(out/"recorder-iphone-diagnostics.ppm",s);

 // A selected USB input can vanish between the last screen refresh and Record.
 // Drive the actual UI actions: neither an ordinary removal nor a broken list
 // may start the first/default microphone behind the user's back.
 {
  const auto projectBefore=songcore::serialize_project(host.project());
  const auto oldBytes=bytes(oldFile),takeBytes=bytes(take);
  Fake safeBackend;safeBackend.path=take.string();
  const auto devices=json::array({{{"id",0},{"name","System default"},{"kind","default"}},{{"id",4},{"name","Built-in microphone"},{"kind","mic"}},{{"id",19},{"name","USB interface"},{"kind","usb"}},{{"id",1000000},{"name","iPhone discovery"},{"kind","iphone-action"},{"connected",false}}});
  safeBackend.inputs=devices;
  AppState safe;safe.project=&host.edit_project();safe.currentInstrument=50;safe.captureBackend=&safeBackend;safe.theme=theme_studio();
  Input buttons(safe,host,*engine,fs);open_studio_capture(safe);
  buttons.choose(19);
  assert(safe.capture.inputs[safe.capture.selected].id==19);
  safeBackend.state={{"state","ready"},{"path",take.string()},{"frames",2048},{"rate",48000},{"channels",1}};buttons.poll();
  safeBackend.inputs.erase(2);safe.studioRow=4;buttons.tap(Button::A);
  assert(safeBackend.starts==0&&safe.capture.inputs[safe.capture.selected].id==19&&!safe.capture.inputs[safe.capture.selected].connected);
  assert(safe.capture.inputs[safe.capture.selected].kind=="missing"&&safe.studioMessage.find("disconnected")!=std::string::npos);
  assert(safe.capture.path==take.string()&&safe.capture.frames==2048);
  shot(out/"recorder-usb-disconnected.ppm",safe);
  safe.capture.state="idle";assert(studio_capture_status(safe.capture)=="Selected input disconnected");
  safe.capture.state="ready";

  // Enumeration errors must retain the missing identity through retries.
  safeBackend.broken=true;buttons.tap(Button::A);
  assert(safeBackend.starts==0&&!safe.capture.inputError.empty()&&safe.capture.inputs[safe.capture.selected].id==19);
  assert(safe.capture.path==take.string()&&safe.capture.frames==2048);
  safeBackend.broken=false;safeBackend.inputs=devices;studio_capture_refresh(safe);
  assert(safe.capture.inputError.empty()&&safe.capture.inputs[safe.capture.selected].id==19&&safe.capture.inputs[safe.capture.selected].connected);
  assert(safe.capture.path==take.string()&&safe.capture.frames==2048);
  std::swap(safeBackend.inputs[0],safeBackend.inputs[2]);studio_capture_refresh(safe);
  assert(safe.capture.selected==1&&safe.capture.inputs[1].id==19);

  // A genuinely disconnected row supplied by the backend is also blocked.
  safeBackend.inputs[0]["connected"]=false;safe.capture.state="idle";safe.studioRow=4;buttons.tap(Button::A);
  assert(safeBackend.starts==0&&studio_capture_status(safe.capture)=="Selected input disconnected");
  safeBackend.inputs=devices;safeBackend.inputs.erase(2);studio_capture_refresh(safe);
  safe.studioRow=0;buttons.tap(Button::DPAD_LEFT);buttons.tap(Button::DPAD_LEFT);buttons.tap(Button::DPAD_LEFT);
  assert(safe.capture.inputs[safe.capture.selected].id==0);
  safe.studioRow=4;buttons.tap(Button::A);assert(safeBackend.starts==1&&safeBackend.id==0);buttons.tap(Button::A);

  // A vanished explicit phone must not turn into manual discovery either.
  safeBackend.inputs=json::array({{{"id",1000001},{"name","Selected phone"},{"kind","iphone"},{"connected",true}}});studio_capture_refresh(safe);
  safe.studioRow=0;buttons.tap(Button::DPAD_LEFT);assert(safe.capture.inputs[safe.capture.selected].id==1000001);
  safeBackend.inputs=json::array({devices[3]});safe.studioRow=4;buttons.tap(Button::A);
  assert(safeBackend.starts==1&&safe.capture.inputs[safe.capture.selected].id==1000001&&!safe.capture.inputs[safe.capture.selected].connected);
  // Manual discovery still works once explicitly selected with the same keys.
  safe.studioRow=0;buttons.tap(Button::DPAD_LEFT);assert(safe.capture.inputs[safe.capture.selected].id==1000000);
  safe.studioRow=4;buttons.tap(Button::A);assert(safeBackend.starts==2&&safeBackend.id==1000000);buttons.tap(Button::A);
  assert(songcore::serialize_project(host.project())==projectBefore&&bytes(oldFile)==oldBytes&&bytes(take)==takeBytes);
 }
 // The visible recorder can resample the actual post-FX/master mix without
 // touching an unavailable external provider. Its previous external take is separate.
 {
  auto masterEngine=std::make_unique<AudioEngine>();masterEngine->setDeviceSampleRate(48000);
  songcore::SongcoreHost masterHost(masterEngine.get(),48000);auto project=songcore::make_default_project();
  project.tracks[0].chainRefs={0};project.chains[0].phraseRefs[0]=0;
  project.phrases[0].steps[0].note=songcore::Note::C4();project.phrases[0].steps[0].instrument=0;
  project.instruments[0].sampleFilePath=studio::custom_uri(103,50,80,100);project.instruments[0].loopMode="fwd";
  assert(masterHost.push_project(songcore::serialize_project(project)));assert(masterHost.load_media("").failed==0);masterHost.push_params();
  StdFileSystem masterFs((out/"internal-data").string());Fake unavailable;unavailable.broken=true;
  AppState mix;mix.project=&masterHost.edit_project();mix.currentInstrument=22;mix.captureBackend=&unavailable;mix.theme=theme_studio();
  mix.capture.path=take.string();mix.capture.frames=2048;mix.capture.state="ready";mix.capture.importedPath="external-previous.wav";
  Input buttons(mix,masterHost,*masterEngine,masterFs);open_studio_capture(mix);
  assert(mix.capture.internal_source()&&unavailable.inputPolls==0&&unavailable.statePolls==0);
  assert(!studio_poll_capture(mix,2000)&&unavailable.inputPolls==0&&unavailable.statePolls==0);
  mix.studioRow=1;buttons.tap(Button::DPAD_RIGHT);assert(mix.resampleView.lengthIndex==1);
  buttons.tap(Button::DPAD_LEFT);assert(mix.resampleView.lengthIndex==0);
  mix.studioRow=3;buttons.tap(Button::DPAD_RIGHT);assert(mix.capture.targetInstrument==23);
  shot(out/"recorder-master-ready.ppm",mix);
  const auto beforeProject=songcore::serialize_project(masterHost.project());
  masterHost.play_song(0);masterHost.poll();mix.isPlaying=true;
  mix.studioRow=4;buttons.tap(Button::A);assert(mix.resampleView.active&&masterHost.is_playing());
  std::vector<float> heard;float output[512];
  for(int block=0;block<8;++block){
   if(block==3)masterEngine->studioRack.punch=4;
   if(block==6)masterEngine->studioRack.punch=0;
   masterEngine->processLiveBlock(output,256,2,48000);heard.insert(heard.end(),output,output+512);
  }
  live_resample_poll(mix,*masterEngine,masterFs);assert(mix.resampleView.frames==2048);
  shot(out/"recorder-master-receiving.ppm",mix);
  buttons.tap(Button::A);
  const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(mix.resampleView.saving&&std::chrono::steady_clock::now()<until){live_resample_poll(mix,*masterEngine,masterFs);std::this_thread::yield();}
  assert(!mix.resampleView.active&&!mix.resampleView.saving&&!mix.resampleView.failed);
  const auto readyPath=mix.resampleView.path;const auto recorded=bytes(readyPath);assert(recorded.size()==44+heard.size()*2);
  double energy=0;for(size_t n=0;n<heard.size();++n){
   const uint16_t raw=static_cast<unsigned char>(recorded[44+n*2])|(static_cast<unsigned char>(recorded[45+n*2])<<8);
   const auto expected=static_cast<int16_t>(std::clamp(int(std::lround(std::clamp(heard[n],-1.f,1.f)*32768.f)),-32768,32767));
   assert(static_cast<int16_t>(raw)==expected);energy+=heard[n]*heard[n];
  }
  assert(energy>.001&&songcore::serialize_project(masterHost.project())==beforeProject);
  // An attempt stopped before the first audio callback retains the ready take.
  buttons.tap(Button::A);assert(mix.resampleView.active);buttons.tap(Button::A);
  assert(!mix.resampleView.active&&mix.resampleView.frames==2048&&mix.resampleView.path==readyPath&&bytes(readyPath)==recorded);
  mix.studioRow=5;buttons.tap(Button::A);assert(!mix.pendingSampleEditor&&mix.studioMessage.find("Stop playback")!=std::string::npos);
  masterHost.stop();mix.isPlaying=false;mix.currentScreen=ScreenType::SAMPLE_EDITOR;mix.sampleEditor.isModified=true;
  buttons.tap(Button::A);assert(!mix.pendingSampleEditor&&mix.studioMessage.find("Save current sample")!=std::string::npos);
  mix.sampleEditor.isModified=false;mix.capture.targetInstrument=0;buttons.tap(Button::A);
  assert(!mix.pendingSampleEditor&&mix.studioMessage.find("empty target")!=std::string::npos);
  mix.capture.targetInstrument=22;mix.studioRow=3;buttons.tap(Button::DPAD_RIGHT);assert(mix.capture.targetInstrument==23&&mix.studioMessage.empty());
  mix.studioRow=5;shot(out/"recorder-master-take-ready.ppm",mix);buttons.tap(Button::A);buttons.handoff();
  assert(mix.currentScreen==ScreenType::SAMPLE_EDITOR&&mix.currentInstrument==23&&mix.sampleEditor.totalFrames==2048);
  assert(masterHost.project().instruments[23].sampleFilePath==mix.resampleView.importedPath&&bytes(mix.resampleView.importedPath)==recorded);
  assert(mix.capture.path==take.string()&&mix.capture.frames==2048&&mix.capture.importedPath=="external-previous.wav");
  assert(unavailable.inputPolls==0&&unavailable.statePolls==0&&unavailable.starts==0&&unavailable.stops==0);
  assert(bytes(take)==bytes(oldFile));
  // Resampling also works on targets without an external capture backend.
  AppState noInput;noInput.project=&masterHost.edit_project();open_studio_capture(noInput);
  assert(noInput.capture.internal_source());noInput.studioRow=4;
  studio_capture_action(noInput,masterHost,*masterEngine,masterFs);assert(noInput.resampleView.active);
  studio_capture_action(noInput,masterHost,*masterEngine,masterFs);assert(!noInput.resampleView.active);
  // An external backend that never finishes stopping does not own the master
  // recorder. Left on Source is an explicit escape; its old state/take stay intact.
  Fake stuck;stuck.path=take.string();stuck.stuckStopping=true;
  AppState stalled;stalled.project=&masterHost.edit_project();stalled.captureBackend=&stuck;
  Input escape(stalled,masterHost,*masterEngine,masterFs);open_studio_capture(stalled);escape.choose(19);
  stalled.capture.state="stopping";stalled.capture.path=take.string();stalled.capture.frames=2048;
  stalled.studioRow=0;escape.tap(Button::DPAD_LEFT);
  assert(stalled.capture.internal_source()&&stalled.capture.state=="stopping"&&stuck.stops==1);
  const int pollsBefore=stuck.inputPolls+stuck.statePolls;
  stalled.studioRow=4;escape.tap(Button::A);assert(stalled.resampleView.active);
  masterEngine->processLiveBlock(output,256,2,48000);live_resample_poll(stalled,*masterEngine,masterFs);
  escape.tap(Button::A);
  const auto stopUntil=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(stalled.resampleView.saving&&std::chrono::steady_clock::now()<stopUntil){live_resample_poll(stalled,*masterEngine,masterFs);std::this_thread::yield();}
  assert(!stalled.resampleView.failed&&!stalled.resampleView.saving&&stalled.resampleView.frames==256);
  assert(!studio_poll_capture(stalled,100000)&&stuck.inputPolls+stuck.statePolls==pollsBefore&&stuck.starts==0&&stuck.stops==1);
  assert(stalled.capture.state=="stopping"&&stalled.capture.path==take.string()&&stalled.capture.frames==2048&&bytes(take)==bytes(oldFile));
 }
 std::cout<<"PASS: internal master recorder captures exact post-FX PCM without external-provider calls, visible length/Start/Stop/Use, zero-frame recovery, selected-slot import, external take and original files preserved; external recording hotplug guards remain valid\n";
}
