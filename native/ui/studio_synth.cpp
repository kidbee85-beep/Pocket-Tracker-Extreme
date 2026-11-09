#include "ui/studio.h"
#include "studio/synth_params.h"
#include "songcore/note_tables.h"
#include "songcore/engine_setup.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pt::ui {
namespace {
int extra_pages(const AppState& s){return (studio::synth_param_count(s.studioModel)+2)/3;}
int kind(const AppState& s){const int n=extra_pages(s);return s.studioSynthPage==0?0:s.studioSynthPage<=n?1:s.studioSynthPage-n+1;}
int extra_index(const AppState& s,int i){return (s.studioSynthPage-1)*3+i;}
std::string patch_uri(const AppState& s){return s.studioSynthCustom?studio::custom_uri(s.studioModel,s.studioHarmonics,s.studioTimbre,s.studioMorph):studio::uri(studio::preset_code(s.studioModel,s.studioVariation));}
void macros(AppState& s){auto p=studio::preset_macros(studio::preset_code(s.studioModel,s.studioVariation));s.studioHarmonics=p[0];s.studioTimbre=p[1];s.studioMorph=p[2];s.studioSynthCustom=false;}
const songcore::ModSlot* envelope(const songcore::Instrument& ins){
 for(const auto& m:ins.modSlots)if(m.dest==songcore::ModDest::VOLUME&&(m.type==songcore::ModType::ADSR||m.type==songcore::ModType::TRIG||m.type==songcore::ModType::AHD||m.type==songcore::ModType::DRUM))return &m;
 return nullptr;
}
songcore::ModSlot* edit_envelope(songcore::Instrument& ins){
 if(auto* m=const_cast<songcore::ModSlot*>(envelope(ins))){m->type=songcore::ModType::ADSR;return m;}
 for(auto& m:ins.modSlots)if(m.type==songcore::ModType::NONE){m=songcore::ModSlot{};m.type=songcore::ModType::ADSR;m.dest=songcore::ModDest::VOLUME;return &m;}
 return nullptr;
}
std::string percent(int v){return std::to_string(static_cast<int>(std::lround(v*100./255)))+" %";}
int filter_index(const songcore::Instrument& ins){return ins.filterType=="lp"?1:ins.filterType=="hp"?2:ins.filterType=="bp"?3:0;}
void publish(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,bool common,const songcore::Instrument* oldDraft=nullptr){
 if(studio_synth_live(s)){
  const auto before=host.project().instruments[s.currentInstrument];
  const int oldCode=studio::uri_code(before.sampleFilePath->c_str());
  const auto oldAutoName=(oldCode&(1<<30))?std::string(studio::model_name(studio::preset_model(oldCode)))+" / CUSTOM":studio::preset_name(oldCode);
  const bool autoName=before.name==oldAutoName||songcore::instrument_has_default_name(before);
  const auto uri=patch_uri(s);
  if(!host.update_studio_synth(s.currentInstrument,uri,s.studioDraft.studioSynthParams)){
   s.studioMessage="Synth source unavailable - load the instrument again";return;
  }
  s.studioBoundUri=uri;
  auto& actual=host.edit_project().instruments[s.currentInstrument];
  if(common){actual.volume=s.studioDraft.volume;actual.pan=s.studioDraft.pan;actual.detune=s.studioDraft.detune;
   actual.filterType=s.studioDraft.filterType;actual.filterCut=s.studioDraft.filterCut;actual.filterRes=s.studioDraft.filterRes;
   actual.drive=s.studioDraft.drive;actual.modSlots=s.studioDraft.modSlots;host.push_instrument(s.currentInstrument);host.update_studio_voice(before,actual);}
  if(autoName)actual.name=s.studioSynthCustom?std::string(studio::model_name(s.studioModel))+" / CUSTOM":studio::preset_name(studio::preset_code(s.studioModel,s.studioVariation));
  s.studioDraft.name=actual.name;s.studioDraft.sampleFilePath=uri;++s.projectVersion;
  s.studioMessage="Live: changes are heard immediately";
 }else s.studioMessage="Draft sound - Load once to use it in the sequence";
 if(s.studioAuditionHeld){
  if(studio::synth_needs_strike(s.studioModel)){
   if(studio_synth_live(s))host.preview_instrument(s.currentInstrument);
   else host.preview_studio_synth(s.studioDraft,patch_uri(s),true);
  }else if(!studio_synth_live(s)){
   host.preview_studio_synth(s.studioDraft,patch_uri(s),false);
   if(common){engine.refreshSoundingInstrument(songcore::PREVIEW_SAMPLE_SLOT);if(oldDraft)host.update_studio_voice(*oldDraft,s.studioDraft,songcore::PREVIEW_SAMPLE_SLOT);}
  }
 }
}
}
bool studio_synth_live(const AppState& s){
 if(!s.studioLiveBound||!s.project||s.currentInstrument!=s.studioSynthInstrument||s.currentInstrument<0||s.currentInstrument>=int(s.project->instruments.size()))return false;
 const auto& ins=s.project->instruments[s.currentInstrument];return ins.instrumentType==songcore::InstrumentType::SAMPLER&&ins.sampleFilePath&&*ins.sampleFilePath==s.studioBoundUri;
}
int studio_synth_page_count(const AppState& s){return 1+extra_pages(s)+3;}
std::string studio_synth_page_name(const AppState& s){
 switch(kind(s)){case 0:return "Oscillator";case 1:return "Engine "+std::to_string(s.studioSynthPage)+" / "+std::to_string(extra_pages(s));case 2:return "Filter / Drive";case 3:return "Amplifier / Tune";default:return "ADSR envelope";}
}
int studio_synth_control_count(const AppState& s){switch(kind(s)){case 0:return studio::macro_count(s.studioModel);case 1:return std::clamp(studio::synth_param_count(s.studioModel)-extra_index(s,0),0,3);case 3:return 3;default:return 4;}}
int studio_synth_apply_row(const AppState& s){return std::max(6,3+studio_synth_control_count(s));}
std::string studio_synth_control_label(const AppState& s,int i){
 if(i<0||i>=studio_synth_control_count(s))return {};
 switch(kind(s)){case 0:return studio::macro_name(s.studioModel,i);case 1:return studio::synth_param_name(s.studioModel,extra_index(s,i));case 2:{const char* n[]={"Filter type","Cutoff","Resonance","Drive"};return n[i];}case 3:{const char* n[]={"Level","Pan","Fine tune"};return n[i];}default:{const char* n[]={"Attack","Decay","Sustain","Release"};return n[i];}}
}
int studio_synth_control_level(const AppState& s,int i){
 if(i<0||i>=studio_synth_control_count(s))return 0;const auto& p=s.studioDraft;
 switch(kind(s)){case 0:{const int v[]={s.studioHarmonics,s.studioTimbre,s.studioMorph};return v[i];}
 case 1:{const int x=extra_index(s,i),v=p.studioSynthParams[x];return v<0?studio::synth_param_default(s.studioModel,x):v;}
 case 2:{const int v[]={filter_index(p)*85,p.filterCut,p.filterRes,p.drive};return v[i];}
 case 3:{const int v[]={p.volume,p.pan,p.detune};return v[i];}
 default:{const auto* m=envelope(p);if(!m)return 0;const int v[]={m->attack,m->decay,m->sustain,m->release};return v[i];}}
}
std::string studio_synth_control_value(const AppState& s,int i){
 if(i<0||i>=studio_synth_control_count(s))return {};
 const int v=studio_synth_control_level(s,i);
 if(kind(s)==1)return studio::synth_param_value(s.studioModel,extra_index(s,i),s.studioDraft.studioSynthParams[extra_index(s,i)]);
 if(kind(s)==2&&i==0){const char* n[]={"Off","Low pass","High pass","Band pass"};return n[filter_index(s.studioDraft)];}
 if(kind(s)==3&&i==1){if(std::abs(v-128)<=1)return "Center";return std::to_string(std::abs(v-128)*100/128)+(v<128?" % L":" % R");}
 if(kind(s)==3&&i==2){char out[32];std::snprintf(out,sizeof(out),"%+.1f cents",1200*std::log2(songcore::detune_multiplier(v)));return out;}
 if(kind(s)==4){if(!envelope(s.studioDraft))return std::any_of(s.studioDraft.modSlots.begin(),s.studioDraft.modSlots.end(),[](const auto& m){return m.type==songcore::ModType::NONE;})?"Off":"No free slot";if(i!=2)return std::to_string(v)+" ticks";}
 return percent(v);
}
std::string studio_synth_note_name(const AppState& s){return songcore::note_name(s.studioDraft.root);}
void open_studio_synth(AppState& s,const songcore::Instrument& ins){
 s.studioOpen=true;s.studioTab=0;s.studioRow=3;s.studioConfirm=false;s.studioMessage.clear();
 s.studioHelp=false;s.studioAUsed=false;s.studioBUsed=false;s.studioSynthPage=0;s.studioAuditionHeld=false;
 s.studioSynthInstrument=s.currentInstrument;s.studioShortcutRelease=false;
 const int code=ins.sampleFilePath?studio::uri_code(ins.sampleFilePath->c_str()):-1;
 s.studioLiveBound=code>=0&&ins.instrumentType==songcore::InstrumentType::SAMPLER;s.studioBoundUri=s.studioLiveBound?*ins.sampleFilePath:"";
 if(s.studioLiveBound){s.studioDraft=ins;
  if(code&(1<<30)){s.studioModel=code&127;s.studioHarmonics=((code>>7)&127)*2;s.studioTimbre=((code>>14)&127)*2;s.studioMorph=((code>>21)&127)*2;s.studioSynthCustom=true;}
  else{s.studioModel=studio::preset_model(code);s.studioVariation=studio::preset_variation(code);macros(s);}
 }else{
  s.studioDraft=songcore::Instrument(s.currentInstrument);s.studioDraft.volume=170;s.studioDraft.filterCut=255;s.studioDraft.loopMode="fwd";
  auto& m=s.studioDraft.modSlots[0];m.type=songcore::ModType::ADSR;m.dest=songcore::ModDest::VOLUME;m.attack=3;m.decay=60;m.sustain=180;m.release=45;macros(s);
 }
}
void studio_synth_sync_target(AppState& s){
 if(!s.project||s.currentInstrument<0||s.currentInstrument>=int(s.project->instruments.size()))return;
 if(s.studioSynthInstrument!=s.currentInstrument){
  const int row=s.studioRow;const bool a=s.studioAUsed,b=s.studioBUsed,shortcut=s.studioShortcutRelease,help=s.studioHelp;
  open_studio_synth(s,s.project->instruments[s.currentInstrument]);s.studioRow=std::clamp(row,0,studio_synth_apply_row(s));
  s.studioAUsed=a;s.studioBUsed=b;s.studioShortcutRelease=shortcut;s.studioHelp=help;
 }
}
void studio_synth_navigate(AppState& s,Button button){
 const int count=studio_synth_control_count(s),apply=studio_synth_apply_row(s);
 s.studioSynthColumn=std::clamp(s.studioSynthColumn,0,std::max(0,count-1));
 const bool left=button==Button::DPAD_LEFT,right=button==Button::DPAD_RIGHT;
 if(left||right){
  if(s.studioRow<=1)s.studioRow=right?1:0;
  else if(s.studioRow>=3&&s.studioRow<3+count){s.studioSynthColumn=std::clamp(s.studioRow-3+(right?1:-1),0,count-1);s.studioRow=3+s.studioSynthColumn;}
 }else if(button==Button::DPAD_UP){
  if(s.studioRow==2)s.studioRow=1;
  else if(s.studioRow>=3&&s.studioRow<3+count){s.studioSynthColumn=s.studioRow-3;s.studioRow=2;}
  else if(s.studioRow==apply)s.studioRow=3+s.studioSynthColumn;
 }else if(button==Button::DPAD_DOWN){
  if(s.studioRow<=1)s.studioRow=2;
  else if(s.studioRow==2)s.studioRow=3+s.studioSynthColumn;
  else if(s.studioRow>=3&&s.studioRow<3+count){s.studioSynthColumn=s.studioRow-3;s.studioRow=apply;}
 }
 s.studioConfirm=false;s.studioMessage.clear();
}
void studio_synth_stop_audition(AppState& s,songcore::SongcoreHost& host){if(s.studioAuditionHeld){s.studioAuditionHeld=false;host.stop_preview(true);}}
void studio_synth_audition(AppState& s,songcore::SongcoreHost& host,bool on){
 if(!on){studio_synth_stop_audition(s,host);return;}if(s.studioAuditionHeld)return;studio_synth_sync_target(s);
 if(studio_synth_live(s)){host.preview_instrument(s.currentInstrument);s.studioAuditionHeld=true;}
 else{s.studioAuditionHeld=host.preview_studio_synth(s.studioDraft,patch_uri(s),true);if(!s.studioAuditionHeld)s.studioMessage="Audition unavailable";}
}
void studio_synth_edit(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,int row,int direction,bool coarse){
 studio_synth_sync_target(s);s.studioConfirm=false;
 if(row==0){const bool held=s.studioAuditionHeld;studio_synth_stop_audition(s,host);s.currentInstrument=std::clamp(s.currentInstrument+direction,0,127);studio_synth_sync_target(s);if(held)studio_synth_audition(s,host,true);return;}
 if(row==1){const int m=std::clamp(s.studioModel+direction,0,studio::model_count()-1);if(m==s.studioModel)return;s.studioModel=m;s.studioDraft.studioSynthParams.fill(-1);macros(s);s.studioSynthPage=std::min(s.studioSynthPage,studio_synth_page_count(s)-1);publish(s,host,engine,false);return;}
 if(row==2){s.studioVariation=(s.studioVariation+direction+studio::variation_count())%studio::variation_count();s.studioDraft.studioSynthParams.fill(-1);macros(s);publish(s,host,engine,false);return;}
 const int i=row-3;if(i<0||i>=studio_synth_control_count(s))return;int* value=nullptr;auto& p=s.studioDraft;const int k=kind(s);
 const auto previousDraft=p;
 if(k==0){int* v[]={&s.studioHarmonics,&s.studioTimbre,&s.studioMorph};value=v[i];s.studioSynthCustom=true;}
 else if(k==1){const int x=extra_index(s,i);p.studioSynthParams[x]=studio::synth_param_adjust(s.studioModel,x,p.studioSynthParams[x],direction,coarse);publish(s,host,engine,false);return;}
 else if(k==2){if(i==0){const char* n[]={"off","lp","hp","bp"};p.filterType=n[std::clamp(filter_index(p)+direction,0,3)];if(p.filterType!="off"&&!p.filterCut)p.filterCut=255;}else{int* v[]={&p.filterCut,&p.filterRes,&p.drive};value=v[i-1];}}
 else if(k==3){int* v[]={&p.volume,&p.pan,&p.detune};value=v[i];}
 else{auto* m=edit_envelope(p);if(!m){s.studioMessage="All modulation slots are used - free one in MODS for ADSR";return;}int* v[]={&m->attack,&m->decay,&m->sustain,&m->release};value=v[i];}
 if(value)*value=std::clamp(*value+direction*(coarse?16:1),0,255);publish(s,host,engine,k>=2,&previousDraft);
}
bool studio_synth_apply(AppState& s,songcore::SongcoreHost& host,AudioEngine&){
 studio_synth_sync_target(s);if(studio_synth_live(s)){s.studioMessage="Live editing is active - no reload needed";return true;}
 auto& old=host.edit_project().instruments[s.currentInstrument];
 if((old.sampleFilePath||old.soundfontPath||old.instrumentType!=songcore::InstrumentType::SAMPLER)&&!s.studioConfirm){s.studioConfirm=true;return false;}
 const bool held=s.studioAuditionHeld;studio_synth_stop_audition(s,host);const auto uri=patch_uri(s);
 if(!host.load_sample(s.currentInstrument,uri)){s.studioMessage="Synth load failed - instrument kept";return false;}
 const int sampleId=old.sampleId;old=s.studioDraft;old.id=s.currentInstrument;old.sampleId=sampleId;old.sampleFilePath=uri;old.soundfontPath.reset();old.instrumentType=songcore::InstrumentType::SAMPLER;
 old.name=s.studioSynthCustom?std::string(studio::model_name(s.studioModel))+" / CUSTOM":studio::preset_name(studio::preset_code(s.studioModel,s.studioVariation));
 host.push_instrument(s.currentInstrument);s.studioDraft=old;s.studioBoundUri=uri;s.studioLiveBound=true;s.studioConfirm=false;++s.projectVersion;
 s.studioMessage="Synth loaded - model, preset and controls now edit live";
 if(held)studio_synth_audition(s,host,true);else if(!host.is_playing())host.preview_instrument(s.currentInstrument);return true;
}
} // namespace pt::ui
