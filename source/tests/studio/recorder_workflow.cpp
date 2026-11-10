#include "audio-engine.h"
#include "songcore/project_io.h"
#include "ui/button_mapper.h"
#include "ui/help_text.h"
#include "ui/input_dispatcher.h"
#include "ui/instrument_row_layout.h"
#include "ui/layout.h"
#include "ui/studio.h"
#include "ui/std_filesystem.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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
 std::string path;bool accept=true,broken=false;int starts=0,stops=0,id=-1;
 std::string inputs_json()override{return broken?"not JSON":inputs.dump();}
 std::string state_json()override{return state.dump();}
 bool start(int input,int channels,int rate)override{++starts;id=input;if(accept){state={{"state","starting"},{"frames",0},{"rate",rate},{"channels",channels},{"path",""}};}return accept;}
 void stop()override{++stops;state={{"state","ready"},{"frames",2048},{"rate",48000},{"channels",1},{"path",path}};}
};
struct Input{
 AppState& s;songcore::SongcoreHost& host;AudioEngine& engine;FileSystem& fs;
 InputDispatcher d;MapperState mapper;uint64_t now=1000;
 Input(AppState& a,songcore::SongcoreHost& h,AudioEngine& e,FileSystem& f):s(a),host(h),engine(e),fs(f),d(a,h,f){}
 void event(Button b,ButtonAction action,ButtonMods mods={}){now+=30;d.set_now(now);ButtonEvent e{b,action,mods};if(!studio_button(e,s,host,engine,fs))handle_button(e,d,mapper,now);}
 void tap(Button b){event(b,ButtonAction::PRESSED);event(b,ButtonAction::RELEASED);}
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
  assert(songcore::serialize_project(host.project())==before);in.tap(Button::B);assert(!s.studioOpen);
 }
 host.edit_project().instruments[20].instrumentType=songcore::InstrumentType::SAMPLER;
 s.capture.targetInstrument=20;
 s.instrumentCursorRow=6;shot(out/"instrument-record.ppm",s);in.tap(Button::A);shot(out/"recorder-ready.ppm",s);
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
 // Broken enumeration empties stale rows; no input is a safe, understandable failure.
 backend.broken=true;studio_capture_refresh(s);assert(s.capture.inputs.empty()&&!s.capture.inputError.empty());s.studioRow=4;const int starts=backend.starts;in.tap(Button::A);assert(backend.starts==starts);
 backend.broken=false;backend.inputs=json::array();studio_capture_refresh(s);in.tap(Button::A);assert(backend.starts==starts&&s.studioMessage.find("No input")!=std::string::npos);shot(out/"recorder-no-input.ppm",s);
 backend.inputs=json::array({{{"id",1000000},{"name","iPhone USB (experimental)"},{"kind","iphone-action"},{"connected",false},{"detail","USB helper missing. Reinstall the complete Brick package."}}});studio_capture_refresh(s);shot(out/"recorder-iphone-diagnostics.ppm",s);
 std::cout<<"PASS: visible empty-slot/editor Record, real Start/Stop and meter states, direct take-to-editor, correct slicing slot, unsaved/occupied protections, source errors and original files preserved\n";
}
