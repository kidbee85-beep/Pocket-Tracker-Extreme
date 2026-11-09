#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/studio.h"
#include "ui/layout.h"
#include "ui/std_filesystem.h"
#include <cassert>
#include <fstream>
#include <cmath>
#include <cstdio>
#include <memory>
#include <filesystem>
using namespace pt::ui;
void shot(const char* path,AppState& s){Canvas c;TrackerLayout layout;layout.draw(c,s);std::ofstream f(path,std::ios::binary);f<<"P6\n640 480\n255\n";for(int i=0;i<640*480;++i){auto p=c.pixels()[i];char rgb[]={static_cast<char>(p>>16),static_cast<char>(p>>8),static_cast<char>(p)};f.write(rgb,3);}}
int main(int argc,char** argv){assert(argc==2);std::filesystem::create_directories(argv[1]);
 auto engine=std::make_unique<AudioEngine>();songcore::SongcoreHost host(engine.get(),48000);
 assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
 AppState s;s.project=&host.edit_project();s.studioOpen=true;s.studioRow=6;
 StdFileSystem fs(argv[1]);
 ButtonEvent a{Button::A,ButtonAction::RELEASED,{}};
 assert(studio_button(a,s,host,*engine,fs));assert(host.project().instruments[0].sampleFilePath==studio::uri(0));
 assert(host.project().instruments[0].loopMode=="fwd");
 float audio[1024]{};double energy=0;
 for(int b=0;b<80;++b){engine->processLiveBlock(audio,512,2,48000);for(float x:audio){assert(std::isfinite(x));energy+=x*x;}}
 assert(energy>1e-5);host.stop_preview();
 // Synth macros are editable, stored in the instrument URI and restored when reopening its editor.
 s.studioRow=3;studio_button({Button::DPAD_RIGHT,ButtonAction::PRESSED,{true}},s,host,*engine,fs);
 assert(s.studioSynthCustom&&s.studioHarmonics==90);
 studio_button(a,s,host,*engine,fs);
 assert(studio_synth_live(s));
 assert(host.project().instruments[0].sampleFilePath==studio::custom_uri(0,90,107,128));
 // The already loaded synth publishes edits immediately; the old Load button is no longer a
 // repeated commit step and cannot arm another replacement confirmation.
 s.studioRow=studio_synth_apply_row(s);studio_button(a,s,host,*engine,fs);assert(!s.studioConfirm);
 assert(host.project().instruments[0].sampleFilePath==studio::custom_uri(0,90,107,128));
 open_studio_synth(s,host.project().instruments[0]);
 assert(s.studioSynthCustom&&s.studioHarmonics==90&&s.studioTimbre==106&&s.studioMorph==128);
 s.studioRow=2;studio_button({Button::DPAD_RIGHT,ButtonAction::PRESSED,{true}},s,host,*engine,fs);
 assert(!s.studioSynthCustom&&s.studioHarmonics==184);
 studio_button(a,s,host,*engine,fs);
 // Return to factory preset 0 for the restart/render checks below.
 studio_synth_edit(s,host,*engine,2,-1,false);
 assert(s.studioVariation==0&&host.project().instruments[0].sampleFilePath==studio::uri(0));host.stop_preview();
 // A mastering preset must retain the selected Mutable effect and its mix/macros.
 auto& rack=host.edit_project().studioMaster;rack[studio::FX]=7;rack[studio::FX_MIX]=99;rack[studio::COLOR]=123;
 s.studioTab=2;s.studioRow=0;s.studioMasterPreset=2;studio_button(a,s,host,*engine,fs);
 assert(rack[studio::FX]==7&&rack[studio::FX_MIX]==99&&rack[studio::COLOR]==123);
 auto saved=songcore::serialize_project(host.project());auto reloaded=songcore::parse_project(songcore::json::parse(saved));
 assert(reloaded.studioMaster==host.project().studioMaster);assert(reloaded.instruments[0].sampleFilePath==studio::uri(0));
 s.studioRow=12;studio_button(a,s,host,*engine,fs);host.edit_project().studioMaster[studio::DRIVE]=0;s.studioRow=13;studio_button(a,s,host,*engine,fs);assert(host.project().studioMaster[studio::DRIVE]==65);
 // Sixteen independent user slots retain separate data; the first uses the legacy filename.
 s.studioMasterUserSlot=15;s.studioRow=12;
 host.edit_project().studioMaster[studio::DRIVE]=43;studio_button(a,s,host,*engine,fs);
 host.edit_project().studioMaster[studio::DRIVE]=0;s.studioRow=13;studio_button(a,s,host,*engine,fs);assert(host.project().studioMaster[studio::DRIVE]==43);
 s.studioMasterUserSlot=0;studio_button(a,s,host,*engine,fs);assert(host.project().studioMaster[studio::DRIVE]==65);
 s.studioTab=1;s.studioFxUserSlot=15;s.studioRow=8;host.edit_project().studioMaster[studio::COLOR]=87;studio_button(a,s,host,*engine,fs);
 host.edit_project().studioMaster[studio::COLOR]=0;s.studioRow=9;studio_button(a,s,host,*engine,fs);assert(host.project().studioMaster[studio::COLOR]==87);
 s.studioTab=2;s.studioRow=13;
 // Extended preset IDs survive instrument reopening and project serialization.
 auto extended=studio::uri(studio::preset_code(10,11));host.edit_project().instruments[1].sampleFilePath=extended;
 s.currentInstrument=1;open_studio_synth(s,host.project().instruments[1]);assert(s.studioModel==10&&s.studioVariation==11&&!s.studioSynthCustom);
 auto extProject=songcore::parse_project(songcore::json::parse(songcore::serialize_project(host.project())));assert(extProject.instruments[1].sampleFilePath==extended);
 s.studioTab=2;s.studioRow=13;
 // Wrong preset shapes/types/ranges are rejected as a whole, with a visible message.
 const auto beforeInvalid=host.project().studioMaster;
 const auto customPath=fs.make_app_folder("Studio")+"/Custom-Master.json";
 songcore::json malformed;malformed["version"]=1;malformed["master"]=beforeInvalid;malformed["master"][studio::WIDTH]="wide";
 assert(fs.write_file(customPath,malformed.dump()));studio_button(a,s,host,*engine,fs);
 assert(host.project().studioMaster==beforeInvalid&&s.studioMessage=="INVALID PRESET - SETTINGS KEPT");
 malformed["master"]=beforeInvalid;malformed["master"][studio::DRIVE]=300;
 assert(fs.write_file(customPath,malformed.dump()));studio_button(a,s,host,*engine,fs);
 assert(host.project().studioMaster==beforeInvalid);
 s.currentInstrument=0;assert(host.push_project(saved));assert(host.load_media("").failed==0);host.push_params();host.preview_instrument(0);energy=0;
 for(int b=0;b<60;++b){engine->processLiveBlock(audio,512,2,48000);for(float x:audio){assert(std::isfinite(x));energy+=x*x;}}assert(energy>1e-5);host.stop_preview();
 open_studio_synth(s,host.project().instruments[0]);
 drumlab_generate(s.drumLab);
 // Deliberately labelled preview data: these screenshots exercise layout, not a live service.
 auto& source=s.sourceSampler;source.provider=2;source.providerAvailable={{true,true,true}};source.row=7;
 source.title="Preview fixture / Jazz guitar";source.creator="Owned test fixture";
 source.results={{"fixture-playlist","Preview fixture playlist","QA","playlist","https://example.invalid/fixture",""}};
 source.tracks={{"fixture-track","Jazz guitar preview fixture","QA","stream","https://example.invalid/fixture","https://creativecommons.org/publicdomain/zero/1.0/",240000}};
 source.selectedResult=source.selectedTrack=0;source.durationMs=240000;source.positionMs=72000;
 source.inMs=70000;source.outMs=82000;source.backendStatus="ready";
 source.status="Preview fixture - no live stream used";source.sourceUrl="https://example.invalid/fixture";
 source.licenseUrl="https://creativecommons.org/publicdomain/zero/1.0/";
 for(int tab=0;tab<8;++tab){s.studioTab=tab;s.studioRow=tab==0?3:tab==1?2:tab==2?3:0;shot((std::string(argv[1])+"/studio-"+std::to_string(tab)+".ppm").c_str(),s);}
 for(float progress:{.1f,.4f,.8f}){s.splashProgress=progress;shot((std::string(argv[1])+"/splash-"+std::to_string(int(progress*100))+".ppm").c_str(),s);}
 s.splashProgress=-1;
 s.studioOpen=false;shot((std::string(argv[1])+"/tracker.ppm").c_str(),s);
 // Render the same public layout used by the Android shell, including long library names.
 for(auto screen:{ScreenType::INSTRUMENT,ScreenType::MIXER,ScreenType::SETTINGS,ScreenType::PROJECT}){
  s.currentScreen=screen;shot((std::string(argv[1])+"/screen-"+std::to_string(static_cast<int>(screen))+".ppm").c_str(),s);
 }
 s.currentScreen=ScreenType::FILE_BROWSER;
 auto& browser=s.fileBrowser;browser.currentDirectory="PocketStudio/Presets/Instruments/PocketStudio_MegaPack/Synths/04_Keys";
 browser.fileExtensions={"pti"};
 browser.items={
  {BrowserItem::Kind::PARENT,"..",".."},
  {BrowserItem::Kind::FOLDER,"FM_Tines","[FM Tines]"},
  {BrowserItem::Kind::FILE,"a.pti","Velvet Wave Keys Motion Open","pti","1KB","08-11-26"},
  {BrowserItem::Kind::FILE,"b.pti","Sub Foundation Glass Open","pti","1KB","08-11-26"}};
 browser.cursor=2;shot((std::string(argv[1])+"/browser-presets.ppm").c_str(),s);
 browser.fileExtensions={"wav","aiff"};browser.currentDirectory="PocketStudio/Samples/Drums";
 browser.items[2]={BrowserItem::Kind::FILE,"break.wav","Amen Break 172 Stereo","wav","4MB","08-11-26"};
 browser.items[3]={BrowserItem::Kind::FILE,"snare.wav","Snare Studio Room","wav","48KB","08-11-26"};
 shot((std::string(argv[1])+"/browser-samples.ppm").c_str(),s);
 browser.mode=BrowserMode::DELETE;shot((std::string(argv[1])+"/browser-confirm.ppm").c_str(),s);
 browser.mode=BrowserMode::NORMAL;browser.items.clear();browser.cursor=0;
 shot((std::string(argv[1])+"/browser-empty.ppm").c_str(),s);
 s.currentScreen=ScreenType::SONG;
 // No punch survives release, page changes, closing or stopping.
 s.studioOpen=true;s.studioTab=3;s.studioRow=1;s.studioPunch=4;
 studio_button({Button::A,ButtonAction::PRESSED,{}},s,host,*engine,fs);assert(engine->studioRack.punch.load()==4);
 studio_button(a,s,host,*engine,fs);assert(engine->studioRack.punch.load()==0);
 studio_button({Button::A,ButtonAction::PRESSED,{}},s,host,*engine,fs);
 studio_button({Button::SELECT,ButtonAction::PRESSED,{}},s,host,*engine,fs);assert(engine->studioRack.punch.load()==0);
 studio_button({Button::SELECT,ButtonAction::RELEASED,{}},s,host,*engine,fs);assert(s.studioHelp);
 studio_button(a,s,host,*engine,fs);assert(!s.studioAUsed&&engine->studioRack.punch.load()==0);
 studio_button({Button::B,ButtonAction::RELEASED,{}},s,host,*engine,fs);assert(!s.studioHelp&&s.studioOpen);
 studio_button({Button::A,ButtonAction::PRESSED,{}},s,host,*engine,fs);engine->stopAllRamped();assert(engine->studioRack.punch.load()==0);
 // A failed source load must not clear or change an external/SoundFont instrument.
 songcore::SongcoreHost noEngine(nullptr,48000);assert(noEngine.push_project(saved));
 noEngine.edit_project().instruments[0].instrumentType=songcore::InstrumentType::EXTERNAL;
 noEngine.edit_project().instruments[0].name="EXTERNAL KEEP";
 AppState failed;failed.project=&noEngine.edit_project();failed.studioOpen=true;failed.studioRow=6;
 studio_button(a,failed,noEngine,*engine,fs);assert(failed.studioConfirm);
 studio_button(a,failed,noEngine,*engine,fs);
 assert(noEngine.project().instruments[0].instrumentType==songcore::InstrumentType::EXTERNAL);
 assert(noEngine.project().instruments[0].name=="EXTERNAL KEEP");
 engine->setTempo(93);assert(engine->tempo()==93&&engine->studioRack.tempo.load()==93);
 engine->studioRack.punch.store(1);engine->resetEffectState();assert(engine->studioRack.punch.load()==0);
 Voice pitchVoice;pitchVoice.playbackRate=1;pitchVoice.baseFrequency=220;
 pitchVoice.params.base[PARAM_PITCH]=12;pitchVoice.loopMode=LOOP_MODE_OSCILLATOR;
 pitchVoice.actualLoopStart=0;pitchVoice.actualLoopEnd=64;
 assert(std::fabs(engine->getModulatedPlaybackRate(pitchVoice,false)-2.f)<1e-6f);
 assert(std::fabs(engine->getModulatedPlaybackRate(pitchVoice,true)-2.f)>1e-3f);
 puts("PASS: live synth, editable macros, save/reload, preset validation, rendered screens, punch release/help/stop, failed-load preservation");
}
