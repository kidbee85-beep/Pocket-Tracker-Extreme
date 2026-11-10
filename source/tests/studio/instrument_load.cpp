#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/input_dispatcher.h"
#include "ui/instrument_row_layout.h"
#include "ui/layout.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"
#include "studio/synth.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
using namespace pt::ui;
namespace {
void word(std::ofstream& f,uint16_t v){char b[]={char(v),char(v>>8)};f.write(b,2);}
void dword(std::ofstream& f,uint32_t v){char b[]={char(v),char(v>>8),char(v>>16),char(v>>24)};f.write(b,4);}
void wav(const std::filesystem::path& p){std::ofstream f(p,std::ios::binary);f.write("RIFF",4);dword(f,36+8192);f.write("WAVEfmt ",8);dword(f,16);word(f,1);word(f,1);dword(f,48000);dword(f,96000);word(f,2);word(f,16);f.write("data",4);dword(f,8192);for(int i=0;i<4096;++i)word(f,int16_t(std::sin(i*.04)*12000));}
double render(AudioEngine& e,songcore::SongcoreHost& h,int n){double sum=0;float pcm[1024]{};for(int b=0;b<n;++b){h.poll();e.processLiveBlock(pcm,512,2,48000);for(float v:pcm){assert(std::isfinite(v));sum+=v*v;}}return sum;}
void shot(const std::filesystem::path& p,AppState& s){Canvas c;TrackerLayout layout;layout.draw(c,s);std::ofstream f(p,std::ios::binary);f<<"P6\n640 480\n255\n";for(int i=0;i<640*480;++i){auto v=c.pixels()[i];char rgb[]={char(v>>16),char(v>>8),char(v)};f.write(rgb,3);}}
void choose(InputDispatcher& d,AppState& s,const std::string& path){int index=-1;for(size_t i=0;i<s.fileBrowser.items.size();++i)if(s.fileBrowser.items[i].path==path){index=int(i);break;}assert(index>=0);s.fileBrowser.cursor=index;d.on_button_a();}
}
int main(int argc,char**argv){
 assert(argc==2);const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
 StdFileSystem fs((out/"app-data").string());auto e=std::make_unique<AudioEngine>();e->setDeviceSampleRate(48000);
 songcore::SongcoreHost host(e.get(),48000);assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
 host.set_app_root(fs.parent_path(fs.samples_directory()));
 AppState s;s.project=&host.edit_project();s.currentScreen=ScreenType::INSTRUMENT;s.currentInstrument=3;
 InputDispatcher d(s,host,fs);const auto open=[&]{s.studioOpen=false;s.currentScreen=ScreenType::INSTRUMENT;s.instrumentCursorRow=0;s.instrumentCursorColumn=2;d.on_button_a();assert(s.currentScreen==ScreenType::FILE_BROWSER&&s.browserPurpose==AppState::BrowserPurpose::LOAD_SOURCE);};
 const auto bookmark=[&](const char* id){choose(d,s,std::string(SOURCE_BOOKMARK_PREFIX)+id);};
 const auto category=fs.instruments_directory()+"/Synths/Keys";std::filesystem::create_directories(category);
 const auto audio=fs.samples_directory()+"/recorded.wav";wav(audio);
 const auto sf=fs.soundfonts_directory()+"/tone.sf2";
 std::filesystem::copy_file(std::filesystem::path(__FILE__).parent_path()/"fixtures/tone.sf2",sf,std::filesystem::copy_options::overwrite_existing);
 songcore::InstrumentPreset preset;preset.instrument=songcore::Instrument(99);preset.instrument.name="Categorized Lead";
 preset.instrument.sampleFilePath=studio::custom_uri(102,120,68,184);preset.instrument.loopMode="fwd";
 preset.instrument.volume=201;preset.instrument.pan=80;preset.instrument.studioSynthParams[0]=44;preset.instrument.studioSynthParams[8]=200;
 preset.instrument.modSlots[0].type=songcore::ModType::ADSR;preset.instrument.modSlots[0].dest=songcore::ModDest::VOLUME;
 preset.instrument.modSlots[0].attack=0;preset.instrument.modSlots[0].sustain=255;
 preset.tableRows=std::vector<songcore::TableRow>(16);(*preset.tableRows)[0].transpose=12;
 const auto pti=category+"/Lead.pti";assert(fs.write_file(pti,songcore::serialize_instrument_preset(preset)));
 open();assert(s.fileBrowser.sourceBookmarks.size()==4);
 for(const auto& ext:{"wav","pti","sf2","sf3"})assert(std::find(s.fileBrowser.fileExtensions.begin(),s.fileBrowser.fileExtensions.end(),ext)!=s.fileBrowser.fileExtensions.end());
 // Virtual source entries are navigation, never rename/delete/clipboard targets.
 for(size_t i=0;i<s.fileBrowser.items.size();++i)if(s.fileBrowser.items[i].path==std::string(SOURCE_BOOKMARK_PREFIX)+"synth")s.fileBrowser.cursor=int(i);
 assert(s.fileBrowser.current()->is_pseudo()&&s.fileBrowser.delete_targets().empty());
 d.on_select_b();assert(s.fileBrowser.mode==BrowserMode::NORMAL);d.on_select_a();assert(!s.qwerty.isOpen);
 shot(out/"instrument-unified-load.ppm",s);
 bookmark("presets");assert(s.fileBrowser.currentDirectory==fs.instruments_directory());
 choose(d,s,fs.instruments_directory()+"/Synths");choose(d,s,category);
 const auto remembered=s.settings.lastSampleFolder;choose(d,s,pti);
 assert(s.currentScreen==ScreenType::INSTRUMENT);
 const auto& loaded=host.project().instruments[3];assert(loaded.id==3&&loaded.sampleId==3);
 assert(loaded.name=="Categorized Lead"&&loaded.volume==201&&loaded.pan==80&&loaded.studioSynthParams==preset.instrument.studioSynthParams);
 assert(loaded.sampleFilePath==preset.instrument.sampleFilePath&&loaded.tableId==3&&host.project().tables[3].rows[0].transpose==12);
 assert(e->studioCodes[3].load()==studio::uri_code(loaded.sampleFilePath->c_str())&&s.settings.lastSampleFolder==remembered);
 host.preview_instrument(3);assert(render(*e,host,20)>1e-7);host.stop_preview(true);render(*e,host,10);
 // The same LOAD menu reaches the complete synth editor without another Project menu.
 const auto document=songcore::serialize_project(host.project());open();bookmark("synth");
 assert(s.studioOpen&&s.currentScreen==ScreenType::INSTRUMENT&&s.studioModel==102&&studio_synth_live(s));
 assert(songcore::serialize_project(host.project())==document);
 studio_button({Button::B,ButtonAction::RELEASED,{}},s,host,*e,fs);assert(!s.studioOpen);
 // File type picks the loader, independently of the previous instrument type.
 open();bookmark("soundfonts");choose(d,s,sf);
 assert(s.currentScreen==ScreenType::INSTRUMENT&&host.project().instruments[3].instrumentType==songcore::InstrumentType::SOUNDFONT);
 assert(host.project().instruments[3].soundfontPath==sf&&host.instrument_media_ready(3));
 open();bookmark("samples");choose(d,s,audio);
 assert(host.project().instruments[3].instrumentType==songcore::InstrumentType::SAMPLER&&host.project().instruments[3].sampleFilePath==audio);
 assert(!host.project().instruments[3].soundfontPath&&e->getSampleLength(3)==4096);
 host.set_instrument_type(3,songcore::InstrumentType::EXTERNAL);open();
 assert(instrument_cell_at(songcore::InstrumentType::EXTERNAL,0,2)==InstrumentCell::SOURCE_LOAD);
 bookmark("midi");assert(s.currentScreen==ScreenType::INSTRUMENT&&s.instrumentCursorRow==2&&!s.studioOpen);
 // Folder overrides remain authoritative, including genuine .pti files in a custom Presets folder.
 const auto override=fs.make_app_folder("Custom-Presets");assert(fs.write_file(override+"/Lead.pti",songcore::serialize_instrument_preset(preset)));
 s.folderConfig.instruments=override;open();bookmark("presets");assert(s.fileBrowser.currentDirectory==override);choose(d,s,override+"/Lead.pti");
 assert(host.project().instruments[3].sampleFilePath==preset.instrument.sampleFilePath);
 s.folderConfig.instruments.reset();
 // Malformed/wrong-schema presets and missing/corrupt sources preserve the old source, table,
 // parameters and active playback. They must not become blank default instruments.
 const auto empty=category+"/Empty.pti";assert(fs.write_file(empty,"{}"));
 const auto bad=category+"/Bad.pti";assert(fs.write_file(bad,"not JSON"));
 auto missing=preset;missing.instrument.name="DO NOT REPLACE";missing.instrument.sampleFilePath=fs.samples_directory()+"/absent.wav";
 const auto absent=category+"/Missing.pti";assert(fs.write_file(absent,songcore::serialize_instrument_preset(missing)));
 const auto invalidAudio=fs.samples_directory()+"/bad.wav";assert(fs.write_file(invalidAudio,"bad wave"));
 missing.instrument.sampleFilePath=invalidAudio;const auto corrupt=category+"/Corrupt.pti";assert(fs.write_file(corrupt,songcore::serialize_instrument_preset(missing)));
 host.edit_project().tracks[0].chainRefs={0};host.edit_project().chains[0].phraseRefs[0]=0;
 host.edit_project().phrases[0].steps[0].note=songcore::Note::C4();host.edit_project().phrases[0].steps[0].instrument=3;
 host.edit_project().tables[3].rows[0].volume=77;
 for(const auto& file:{empty,bad,absent,corrupt}){
  const auto before=songcore::serialize_project(host.project());const auto code=e->studioCodes[3].load();
  open();navigate_to_folder(s.fileBrowser,fs,category);host.play_song(0);assert(host.is_playing());
  choose(d,s,file);assert(s.currentScreen==ScreenType::FILE_BROWSER&&!s.fileBrowser.statusSuccess&&host.is_playing());
  assert(songcore::serialize_project(host.project())==before&&e->studioCodes[3].load()==code);
  assert(render(*e,host,8)>1e-7);host.stop();render(*e,host,10);
 }
 // Wrapped params-only presets and recognized legacy flat presets remain legitimate loads.
 const auto params=category+"/Parameters.pti";assert(fs.write_file(params,"{\"instrument\":{\"name\":\"Settings only\",\"volume\":123}}"));
 open();navigate_to_folder(s.fileBrowser,fs,category);choose(d,s,params);assert(host.project().instruments[3].name=="Settings only"&&host.project().instruments[3].volume==123);
 const auto flat=category+"/Legacy.pti";assert(fs.write_file(flat,"{\"name\":\"Legacy\",\"volume\":140}"));
 open();navigate_to_folder(s.fileBrowser,fs,category);choose(d,s,flat);assert(host.project().instruments[3].name=="Legacy"&&host.project().instruments[3].volume==140);
 // Dedicated .pti browsers keep their old purpose and do not acquire source navigation actions.
 s.currentScreen=ScreenType::INSTRUMENT;d.open_file_browser(AppState::BrowserPurpose::LOAD_PRESET,fs.instruments_directory(),{"pti"});assert(s.fileBrowser.sourceBookmarks.empty());
 std::puts("PASS: unified Instrument LOAD, protected source bookmarks, actual categorized .pti metadata/params, Synth access, SF/WAV dispatch, folder override and transactional invalid-preset preservation");
}
