// SPDX-License-Identifier: GPL-3.0-or-later
// Reproducible promotional capture of the shipping renderer, engine and controls.
// The provider snapshots are saved by source_capture.cpp using real HTTPS data.
// This is native software capture, not a recording of a physical device camera.
#include "audio-engine.h"
#include "songcore/host.h"
#include "ui/layout.h"
#include "ui/engine_feed.h"
#include "ui/input_dispatcher.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
using namespace pt::ui;
using json=songcore::json;
namespace fs=std::filesystem;
std::string read(const fs::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void ppm(const fs::path&p,const Canvas&c){std::ofstream f(p,std::ios::binary);f<<"P6\n640 480\n255\n";for(int i=0;i<640*480;++i){auto q=c.pixels()[i];char b[]={char(q>>16),char(q>>8),char(q)};f.write(b,3);}}
struct RecordedSource:SourceBackend{
 json state;
 bool command(const std::string&,const std::string&)override{return true;}
 std::string state_json()override{return state.dump();}
};
int main(int argc,char**argv){
 if(argc!=6){std::cerr<<"capture PROJECT.ptp APPDATA SOURCE_SNAPSHOTS OUTPUT FRAMES\n";return 1;}
 const fs::path project=fs::absolute(argv[1]),data=fs::absolute(argv[2]),sources=fs::absolute(argv[3]),out=fs::absolute(argv[4]);
 const int count=std::stoi(argv[5]);constexpr int fps=24,rate=48000;fs::create_directories(out/"shots");fs::create_directories(data);
 auto engine=std::make_unique<AudioEngine>();songcore::SongcoreHost host(engine.get(),rate);
 if(!host.push_project(read(project)))return 2;host.set_app_root(data.string());
 const auto loaded=host.load_media(project.parent_path().string());if(loaded.failed){std::cerr<<"Missing project media\n";return 2;}host.push_params();
 AppState s;s.project=&host.edit_project();s.theme=theme_by_name("ULTRAVIOLET",VisualizerType::OCTA);s.caps=PlatformCaps::sdl(false);
 s.settings.scalingBilinear=true;s.settings.rememberFolder=false;s.currentScreen=ScreenType::SONG;
 StdFileSystem filesystem(data.string());InputDispatcher dispatch(s,host,filesystem);dispatch.set_media_base_dir(project.parent_path().string());
 TrackerLayout layout;EngineFeed feed;Canvas canvas;RecordedSource source;
 const auto sourceDoc=json::parse(read(sources/"06-rendered.json"));
 const auto cloudClip=fs::absolute(sourceDoc.at("clip_path").get<std::string>());
 fs::current_path(data);
 auto provider=[&](const char*name){source.state=json::parse(read(sources/(std::string(name)+".json")));source.state["clip_path"]=cloudClip.string();s.sourceSampler.backend=&source;s.sourceSampler.provider=1;s.sourceSampler.query="cloud rap";s.sourceSampler.hasPolled=false;source_sampler_poll(s.sourceSampler,100000);s.sourceSampler.selectedResult=source.state.value("selected_index",0);s.sourceSampler.selectedTrack=source.state.value("track_index",0);};
 auto load_editor=[&](const fs::path&p,const std::string&title){
  s.studioOpen=false;s.currentInstrument=30;if(!host.load_sample(30,p.string()))throw std::runtime_error("Cannot open editor audio");
  auto&ins=host.edit_project().instruments[30];ins.sampleId=30;ins.sampleFilePath=p.string();ins.instrumentType=songcore::InstrumentType::SAMPLER;ins.name=title;host.push_instrument(30);dispatch.open_imported_sample_editor();
 };
 auto track_view=[&](ScreenType screen){s.studioOpen=false;s.currentScreen=screen;s.studioHelp=false;};
 auto studio=[&](int tab,int row){s.studioOpen=true;s.studioTab=tab;s.studioRow=row;s.studioHelp=false;};
 auto browser=[&](const fs::path&p){s.studioOpen=false;s.currentScreen=ScreenType::FILE_BROWSER;s.browserPurpose=AppState::BrowserPurpose::LOAD_PRESET;s.fileBrowser.fileExtensions={"pti"};s.fileBrowser.sourceBookmarks.clear();navigate_to_folder(s.fileBrowser,filesystem,p.string());};
 const auto originalRack=host.project().studioMaster;
 host.play_song(0);
 std::ofstream raw(out/"app-ui.rgb",std::ios::binary),audio(out/"native-audio.f32",std::ios::binary);
 std::vector<char> rgb(640*480*3);std::vector<float> block(2000*2);
 json events=json::array();int previous=-1;int64_t audioFrames=0;
 const fs::path crate=data/"Samples/Kid_Bunka_Original_Crate_Source.wav";
 auto phase_of=[](double t){
  const double ends[]={3,12,23,30,42,50,58,67,75,85,91,97,104,111,117,123,132,139,145,153,163,174,185,194,200,210};
  for(int i=0;i<26;++i)if(t<ends[i])return i;return 25;
 };
 for(int frame=0;frame<count;++frame){
  const double sec=double(frame)/fps;const long long ms=llround(sec*1000);const int phase=phase_of(sec);
  dispatch.set_now(ms);
  if(phase!=previous){
   engine->studioRack.punch.store(0);s.splashProgress=-1;s.helpOpen=false;s.helpFull=false;
   if(phase==0)s.splashProgress=0;
   if(phase==1)track_view(ScreenType::SONG);
   if(phase==2){track_view(ScreenType::PHRASE);s.cursorColumn=1;}
   if(phase==3){track_view(ScreenType::INSTRUMENT);s.currentInstrument=17;s.instrumentCursorRow=4;s.instrumentCursorColumn=1;}
   if(phase==4){s.currentInstrument=17;open_studio_synth(s,host.project().instruments[17]);s.studioRow=3;}
   if(phase==5){s.studioSynthPage=std::min(1,studio_synth_page_count(s)-1);s.studioRow=3;}
   if(phase==6)browser(fs::path("Presets/Instruments/PocketStudio_MegaPack/Synths"));
   if(phase==7)browser(fs::path("Presets/Instruments/PocketStudio_MegaPack/Drums"));
   if(phase==8){studio(5,0);s.drumLab.activeLanes=8;s.drumLab.steps=32;s.drumLab.seed=92;s.drumLab.genre=0;
    const int ids[]={0,2,4,5,6,7,8,9};for(int i=0;i<8;++i)s.drumLab.lanes[i].instrument=ids[i];drumlab_generate(s.drumLab);}
   if(phase==9){studio(5,3);s.drumLab.lanes[0].locked=true;s.drumLab.seed=9242;drumlab_generate(s.drumLab);}
   if(phase==10){load_editor(crate,"KID BUNKA CRATE");s.sampleEditor.cursorRow=10;s.sampleEditor.cursorCol=0;s.sampleEditor.sliceMethod=SampleEditorModule::SLICE_TRANSIENT;s.sampleEditor.transientMarkers.clear();}
   if(phase==11){s.sampleEditor.zoomLevel=2;s.sampleEditor.cursorRow=9;s.sampleEditor.cursorCol=1;s.sampleEditor.viewCenter=s.sampleEditor.totalFrames/3;}
   if(phase==12){host.stop();studio(7,2);provider("01-search");s.sourceSampler.selectedResult=sourceDoc.value("selected_index",0);s.sourceSampler.row=3;s.sourceSampler.listFocused=true;}
   if(phase==13){studio(7,3);provider("02-tracks");s.sourceSampler.row=4;s.sourceSampler.listFocused=true;}
   if(phase==14){studio(7,6);provider("03-preview");s.sourceSampler.listFocused=false;}
   if(phase==15){studio(7,8);provider("05-markers");s.sourceSampler.listFocused=false;}
   if(phase==16){studio(7,10);provider("06-rendered");s.sourceSampler.listFocused=false;}
   if(phase==17){
    s.currentInstrument=30;std::string imported;
    if(!studio_import_take(s,host,*engine,filesystem,cloudClip.string(),imported,"CLOUD RAP CC0","Samples/Clips"))throw std::runtime_error(s.studioMessage);
    s.studioOpen=false;dispatch.open_imported_sample_editor();s.sampleEditor.sampleName="Cloud_Rap_CC0";s.sampleEditor.sliceMethod=SampleEditorModule::SLICE_TRANSIENT;s.sampleEditor.transientMarkers.clear();s.sampleEditor.cursorRow=10;
   }
   if(phase==18){s.sampleEditor.zoomLevel=3;s.sampleEditor.viewCenter=s.sampleEditor.totalFrames/3;s.sampleEditor.cursorRow=9;s.sampleEditor.cursorCol=1;}
   if(phase==19){host.play_song(14);studio(3,1);s.studioPunch=1;}
   if(phase==20){studio(3,1);s.studioPunch=6;}
   if(phase==21){studio(1,0);s.studioEffectPreset=129;studio_button({Button::A,ButtonAction::RELEASED,{}},s,host,*engine,filesystem);s.studioRow=2;}
   if(phase==22){host.edit_project().studioMaster=originalRack;host.push_params();studio(2,0);s.studioMasterPreset=1;studio_button({Button::A,ButtonAction::RELEASED,{}},s,host,*engine,filesystem);s.studioRow=1;}
   if(phase==23){track_view(ScreenType::MIXER);s.mixerCursorColumn=8;}
   if(phase==24){track_view(ScreenType::SONG);s.cursorColumn=1;}
   if(phase==25)s.splashProgress=.7f;
   events.push_back({{"seconds",sec},{"phase",phase},{"screen",int(s.currentScreen)},{"studio_tab",s.studioOpen?s.studioTab:-1}});
   previous=phase;
  }
  if(phase==0)s.splashProgress=float(sec/3);
  if(phase==14&&sec>=113&&s.sourceSampler.playing){provider("04-paused");s.sourceSampler.row=6;s.sourceSampler.listFocused=false;}
  if(phase==2||phase==24){
   const auto p=host.playheads(0);if(phase==2&&p.phraseId>=0)s.currentPhrase=p.phraseId;
   if(p.songRow>=0)s.cursorRow=p.songRow;
  }
  if((phase==4||phase==5)&&frame%(fps*2)==0)studio_synth_edit(s,host,*engine,3,frame%(fps*4)==0?1:-1,false);
  if(phase==6||phase==7){s.fileBrowser.cursor=std::min<int>(int(s.fileBrowser.items.size())-1,1+int(sec*0.7)%9);}
  if(phase==8||phase==9){s.drumLab.row=phase==8?0:3;s.drumLab.previewDisplayStep=int(sec*92/60*4)%32;}
  if(phase==19||phase==20){
   const double beat=sec*92/60;const bool held=fmod(beat,4.0)>3.4;
   studio_button({Button::A,held?ButtonAction::PRESSED:ButtonAction::RELEASED,{}},s,host,*engine,filesystem);
  }
  const int64_t target=int64_t(frame+1)*rate/fps;
  while(audioFrames<target){int n=int(std::min<int64_t>(512,target-audioFrames));host.poll();engine->processLiveBlock(block.data(),n,2,rate);audio.write(reinterpret_cast<char*>(block.data()),n*2*sizeof(float));audioFrames+=n;}
  s.isPlaying=host.is_playing();s.blinkPhaseMs=int(ms%1000);s.trackMask=host.track_mask();
  for(int t=0;t<8;++t){auto p=host.playheads(t);s.playheads[t]={p.songRow,p.chainId,p.chainRow,p.phraseId,p.phraseStep};}
  feed.poll(*engine,host,s,ms);layout.draw(canvas,s);
  for(int i=0;i<640*480;++i){auto p=canvas.pixels()[i];rgb[i*3]=char(p>>16);rgb[i*3+1]=char(p>>8);rgb[i*3+2]=char(p);}
  raw.write(rgb.data(),rgb.size());
  if(!events.empty()&&frame==int(events.back()["seconds"].get<double>()*fps)+fps/2)ppm(out/"shots"/("phase-"+std::to_string(phase)+".ppm"),canvas);
  if(frame%(fps*15)==0)std::cerr<<"Captured "<<int(sec)<<" / "<<count/fps<<"s\n";
 }
 host.stop();raw.close();audio.close();
 std::ofstream(out/"capture-manifest.json")<<json({{"width",640},{"height",480},{"fps",fps},{"frames",count},{"seconds",double(count)/fps},{"theme","ULTRAVIOLET"},{"visualizer","OCTA"},{"layout","Shipping TrackerLayout and EngineFeed, scripted navigation and real project"},{"provider_snapshots","Actual HTTPS Commons source_capture output, edited for timing"},{"physical_camera_footage",false},{"events",events},{"media_loaded",loaded.loaded},{"media_failed",loaded.failed}}).dump(2)<<'\n';
}
