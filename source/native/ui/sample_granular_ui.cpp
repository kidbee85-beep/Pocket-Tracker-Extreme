// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/sample_granular_ui.h"
#include "ui/sample_granular_presets.h"
#include "ui/studio.h"
#include "ui/std_filesystem.h"
#include "songcore/note_tables.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string_view>
#include <utility>

namespace pt::ui {
namespace {
using Parameters=studio::granular::Parameters;
struct Control {const char* label;float Parameters::*member;float lo,hi,fine,coarse;int display;};
// display: percent, ms, Hz, semitones, pitch spread, source-rate, toggle.
constexpr Control controls[]={
 {"Position",&Parameters::position,0,1,.005f,.05f,0},{"Grain size",&Parameters::sizeMs,2,1000,1,10,1},
 {"Density",&Parameters::densityHz,0,120,.5f,5,2},{"Position spray",&Parameters::sprayMs,0,1000,1,10,1},
 {"Pitch",&Parameters::pitchSemitones,-48,48,1,12,3},{"Pitch spray",&Parameters::pitchSpraySemitones,0,24,.1f,1,4},
 {"Scan speed",&Parameters::scanSpeed,-4,4,.01f,.1f,5},{"Freeze",nullptr,0,1,1,1,6},
 {"Pan spread",&Parameters::panSpread,0,1,.01f,.1f,0},{"Reverse chance",&Parameters::reverseProbability,0,1,.01f,.1f,0},
 {"Window shape",&Parameters::windowShape,0,1,.01f,.1f,0},{"Grain level",&Parameters::level,0,2,.01f,.1f,0},
 {"Attack",&Parameters::attackMs,0,5000,1,25,1},{"Release",&Parameters::releaseMs,1,10000,1,25,1}
};
constexpr int pages[4][4]={{0,1,2,3},{7,6,9,10},{4,5,8,11},{12,13,-1,-1}};
int page(const AppState& s){return std::clamp(s.studioSynthPage,0,5);}
int key(const AppState& s,int control){return page(s)<4&&control>=0&&control<4?pages[page(s)][control]:-1;}
const songcore::Instrument* instrument(const AppState& s){
 return s.project&&s.currentInstrument>=0&&s.currentInstrument<int(s.project->instruments.size())?&s.project->instruments[s.currentInstrument]:nullptr;
}
int filter(const songcore::Instrument& ins){return ins.filterType=="lp"?1:ins.filterType=="hp"?2:ins.filterType=="bp"?3:0;}
bool preserve_pending_sample_edit(AppState& s){
 const bool overEditor=s.currentScreen==ScreenType::SAMPLE_EDITOR||
   (s.currentScreen==ScreenType::FILE_BROWSER&&s.sampleGranular.browserFromEditor);
 if(!overEditor||!s.sampleEditor.isModified)return false;
 s.studioMessage="Save your sample edits first. B returns to the current edit.";return true;
}
void modified(AppState& s){++s.projectVersion;s.sampleGranular.nextPollMs=0;}
void reset_wave_view(SampleGranularUIState& g){
 g.waveformZoom=0;
 g.waveformCenter=.5;
 g.waveformMode=GranularWaveformMode::View;
 g.waveformReady=false;
 g.waveformInstrument=-1;
 g.waveformFirst=0;
 g.waveformEnd=0;
}
void reset_comparison(SampleGranularUIState& g){
 g.compareAvailable=false;
 g.compareLiveB=false;
 g.compareMorph.amount=-1;
}
void wait_for_source(SampleGranularUIState& g,uint32_t oldGeneration){
 g.waitingForSource=true;g.replacedGeneration=oldGeneration;g.snapshot={};g.nextPollMs=0;
 reset_wave_view(g);
 reset_comparison(g);
}
struct WaveRange {
 int64_t first=0,end=0;
 int64_t length() const { return end-first; }
 bool contains(int64_t frame) const { return frame>=first&&frame<end; }
};
WaveRange wave_region(const studio::granular::Snapshot& source){
 const auto first=std::clamp<int64_t>(source.firstFrame,0,std::numeric_limits<int>::max());
 const auto end=std::clamp<int64_t>(source.endFrame,first,std::numeric_limits<int>::max());
 return {first,end};
}
constexpr int kMaxWaveZoom=6;
int64_t wave_width(int64_t length,int zoom){
 const int scale=1<<zoom;
 return std::clamp<int64_t>((length+scale-1)/scale,
                            std::min<int64_t>(length,kSampleGranularWaveBins),length);
}
WaveRange wave_view(const WaveRange& region,int zoom,double center){
 const auto length=region.length(),width=wave_width(length,zoom);
 const auto centered=region.first+std::llround(center*double(length)-double(width)/2.);
 const auto first=std::clamp<int64_t>(centered,region.first,region.end-width);
 return {first,first+width};
}
bool wave_source_ready(const AppState& s,const WaveRange& region){
 const auto& g=s.sampleGranular;
 return !g.waitingForSource&&g.snapshot.sourceValid&&
        g.snapshot.instrumentId==s.currentInstrument&&region.length()>0;
}
int64_t wave_position_frame(const WaveRange& region,float normalized){
 return region.first+std::llround(std::clamp<double>(normalized,0.,1.)*double(region.length()-1));
}
void show_position(SampleGranularUIState& g,const WaveRange& region,int64_t position){
 if(!wave_view(region,g.waveformZoom,g.waveformCenter).contains(position)){
  g.waveformCenter=(double(position-region.first)+.5)/double(region.length());
  g.nextPollMs=0;
 }
}
void edit_wave_view(AppState& s,int direction,bool zoom){
 auto& g=s.sampleGranular;
 const auto region=wave_region(g.snapshot);
 if(!wave_source_ready(s,region)){
  s.studioMessage="Load a sample to zoom its waveform";
  return;
 }
 const auto current=wave_view(region,g.waveformZoom,g.waveformCenter);
 if(zoom){
  int next=g.waveformZoom+direction;
  while(next>0&&next<kMaxWaveZoom&&wave_width(region.length(),next)==current.length())
   next+=direction;
  if(next<0||next>kMaxWaveZoom)return;
  if(direction>0&&wave_width(region.length(),next)==current.length())return;
  if(g.waveformZoom==0)g.waveformCenter=std::clamp<double>(g.parameters.position,0.,1.);
  g.waveformZoom=next;
 }else{
  if(current.length()==region.length()){
   s.studioMessage="A + Up zooms in before panning";
   return;
  }
  const double center=double(current.first-region.first)+double(current.length())*.5;
  const double step=std::max(1.,double(current.length())*.25);
  g.waveformCenter=std::clamp((center+direction*step)/double(region.length()),0.,1.);
 }
 g.nextPollMs=0;
 s.studioMessage.clear();
}
void finish_morph_on_edit(SampleGranularUIState& g){
 if(g.compareMorph.amount<0)return;
 // An edit to the A endpoint stays A. An edit to a blend becomes the working B.
 const bool editingA=g.compareMorph.amount==0;
 g.compareParameters=editingA?g.compareMorph.b:g.compareMorph.a;
 g.compareLiveB=!editingA;
 g.compareMorph.amount=-1;
}
bool publish(AppState& s,songcore::SongcoreHost& host,const Parameters& p,bool morphing=false){
 const auto* ins=instrument(s);if(!ins)return false;
 if(!host.update_sample_granular(s.currentInstrument,ins->granularEnabled,p)){
  s.studioMessage=host.sample_granular_error();return false;
 }
 if(!morphing)finish_morph_on_edit(s.sampleGranular);
 s.sampleGranular.parameters=p;s.sampleGranular.custom=true;s.sampleGranular.presetName="Custom settings";modified(s);
 s.studioMessage=ins->granularEnabled?"LIVE: grain edits reach the playing sample":"Settings kept / enable Granular to hear them";return true;
}
bool apply_factory_preset(AppState& s,songcore::SongcoreHost& host,int index){
 const auto& preset=sample_granular_factory_preset(index);
 if(!publish(s,host,preset.parameters))return false;
 auto& g=s.sampleGranular;
 g.factoryPreset=index;
 g.custom=false;
 g.presetName=preset.name;
 return true;
}
void edit_wave_position(AppState& s,songcore::SongcoreHost& host,int direction,bool coarse){
 auto& g=s.sampleGranular;
 const auto region=wave_region(g.snapshot);
 const auto* ins=instrument(s);
 if(!ins||!wave_source_ready(s,region)){
  s.studioMessage="Load a sample to move Position";
  return;
 }
 if(region.length()==1)return;
 const auto view=wave_view(region,g.waveformZoom,g.waveformCenter);
 const int64_t step=std::max<int64_t>(1,std::llround(double(view.length())/(coarse?4.:32.)));
 const auto current=wave_position_frame(region,ins->granularParams.position);
 const auto target=std::clamp(current+direction*step,region.first,region.end-1);
 if(target==current)return;
 auto next=ins->granularParams;
 next.position=float(double(target-region.first)/double(region.length()-1));
 if(publish(s,host,next)){
  show_position(g,region,target);
  if(ins->granularEnabled)s.studioMessage.clear();
  else s.studioMessage="Position saved / enable Granular to hear it";
 }
}
bool capture_comparison(AppState& s){
 auto& g=s.sampleGranular;
 const auto* ins=instrument(s);
 if(!ins||!g.snapshot.sourceValid||g.waitingForSource){
  s.studioMessage="Load a sample to compare grain settings";
  return false;
 }
 g.compareParameters=ins->granularParams;
 g.compareAvailable=true;
 g.compareLiveB=true;
 g.compareMorph.amount=-1;
 s.studioMessage="A stored. Edit B, then press A/B to compare.";
 return true;
}
Parameters morph_parameters(const Parameters& a,const Parameters& b,int amount){
 if(amount==0)return a;
 if(amount==100)return b;
 const float t=amount*.01f;
 const auto mix=[t](float from,float to){return from+(to-from)*t;};
 Parameters p;
 p.position=mix(a.position,b.position);
 p.sizeMs=mix(a.sizeMs,b.sizeMs);
 p.densityHz=mix(a.densityHz,b.densityHz);
 p.sprayMs=mix(a.sprayMs,b.sprayMs);
 p.pitchSemitones=mix(a.pitchSemitones,b.pitchSemitones);
 p.pitchSpraySemitones=mix(a.pitchSpraySemitones,b.pitchSpraySemitones);
 p.freeze=a.freeze&&b.freeze;
 // A frozen endpoint contributes zero automatic motion to the blend.
 p.scanSpeed=mix(a.freeze?0.f:a.scanSpeed,b.freeze?0.f:b.scanSpeed);
 p.panSpread=mix(a.panSpread,b.panSpread);
 p.reverseProbability=mix(a.reverseProbability,b.reverseProbability);
 p.windowShape=mix(a.windowShape,b.windowShape);
 p.level=mix(a.level,b.level);
 p.attackMs=mix(a.attackMs,b.attackMs);
 p.releaseMs=mix(a.releaseMs,b.releaseMs);
 return studio::granular::Voice::sanitize(p);
}
void edit_morph(AppState& s,songcore::SongcoreHost& host,int direction){
 auto& g=s.sampleGranular;
 if(!g.compareAvailable){
  s.studioMessage="Press A on A/B to store A, then edit B";
  return;
 }
 const auto* ins=instrument(s);
 if(!ins||!wave_source_ready(s,wave_region(g.snapshot))){
  s.studioMessage="Wait for the sample before morphing";
  return;
 }
 auto morph=g.compareMorph;
 if(morph.amount<0){
  morph.a=g.compareLiveB?g.compareParameters:ins->granularParams;
  morph.b=g.compareLiveB?ins->granularParams:g.compareParameters;
  morph.amount=g.compareLiveB?100:0;
 }
 const int next=std::clamp(morph.amount+direction*5,0,100);
 if(next==morph.amount)return;
 const auto parameters=morph_parameters(morph.a,morph.b,next);
 if(!studio::granular::parametersEqual(parameters,ins->granularParams)
     && !publish(s,host,parameters,/*morphing=*/true))return;
 morph.amount=next;
 g.compareMorph=morph;
 if(ins->granularEnabled)s.studioMessage.clear();
 else s.studioMessage="Morph stored / enable Granular to hear it";
}
bool resolve_sample(FileSystem& fs,const std::string& preset,const std::string& reference,std::string& output){
 if(!sample_granular_relative_reference(reference))return false;
 const auto base=fs.parent_path(preset);std::string directory=base;size_t at=0;
 while(at<reference.size()){
  const auto end=reference.find('/',at);const auto name=reference.substr(at,end==std::string::npos?reference.size()-at:end-at);
  const auto entries=fs.list_files(directory);const auto found=std::find_if(entries.begin(),entries.end(),[&](const FileInfo& file){return !file.isRoot&&!file.isAction&&file.name==name;});
  if(found==entries.end()||(end==std::string::npos?found->isDirectory:!found->isDirectory))return false;
  directory=found->path;if(end==std::string::npos)break;at=end+1;
 }
 // Native symlinks must also remain inside the preset directory. Granted Android document
 // trees resolve by actual child entries above, never by concatenating a content URI.
 if(base.find("://")==std::string::npos){
  std::error_code a,b;const auto root=std::filesystem::weakly_canonical(base,a),target=std::filesystem::weakly_canonical(directory,b);
  if(a||b)return false;auto ri=root.begin(),ti=target.begin();
  for(;ri!=root.end();++ri,++ti)if(ti==target.end()||*ri!=*ti)return false;
 }
 output=directory;return fs.file_exists(output)&&!fs.is_directory(output);
}
}
bool sample_granular_studio(const AppState& s){return s.studioTab==0&&s.sampleGranular.open;}
void sample_granular_open(AppState& s,const songcore::Instrument& ins){
 auto& g=s.sampleGranular;g.open=true;g.instrument=s.currentInstrument;g.parameters=ins.granularParams;g.seed=ins.granularSeed;
 g.custom=true;g.presetName="Current settings";g.sourcePath=ins.sampleFilePath?*ins.sampleFilePath:"";
 g.waitingForSource=false;g.waveformGeneration=0;g.snapshot={};g.nextPollMs=0;g.action=0;
 sample_granular_close_preset_picker(s);
 reset_wave_view(g);
 reset_comparison(g);
 s.studioSynthInstrument=s.currentInstrument;s.studioSynthPage=0;s.studioSynthColumn=0;s.studioConfirm=false;s.studioMessage.clear();
}
void sample_granular_select(AppState& s,songcore::SongcoreHost& host,bool activate){
 const auto* ins=instrument(s);if(!ins)return;sample_granular_open(s,*ins);
 s.studioLiveBound=false;s.studioBoundUri.clear();s.studioRow=3;
 if(activate){
  if(host.update_sample_granular(s.currentInstrument,true,s.sampleGranular.parameters)){modified(s);s.studioMessage="Sample Granular active / edits are live";}
  else{s.studioMessage=host.sample_granular_error()+" / choose Load source";s.sampleGranular.action=1;s.studioRow=studio_synth_apply_row(s);}
 }
}
void sample_granular_sync(AppState& s){
 const auto* ins=instrument(s);if(!ins)return;
 if(s.sampleGranular.instrument!=s.currentInstrument){
  const int row=s.studioRow;
  sample_granular_open(s,*ins);
  s.studioRow=std::clamp(row,0,kGranularWaveRow);
 }
}
bool sample_granular_poll(AppState& s,songcore::SongcoreHost& host,uint64_t now){
 auto& g=s.sampleGranular;
 if(!s.studioOpen||!sample_granular_studio(s)||s.studioHelp){
  if(g.publisherFocused){host.sample_granular_snapshot(-1);g.publisherFocused=false;}return false;
 }
 if(now<g.nextPollMs)return false;g.nextPollMs=now+16;sample_granular_sync(s);
 const auto* ins=instrument(s);if(!ins)return false;
 const bool rootPlaying=s.studioAuditionHeld||g.recording.ownsRoot;
 const auto next=host.sample_granular_snapshot(s.currentInstrument,rootPlaying?AudioEngine::PREVIEW_LANE:-1);g.publisherFocused=true;
 if(g.waitingForSource){
  if(!next.sourceValid||next.sourceGeneration==g.replacedGeneration)return false;
  g.waitingForSource=false;
 }
 const auto region=wave_region(next);
 const bool match=next.sourceValid&&next.instrumentId==s.currentInstrument&&region.length()>0&&next.sourceRate>0;
 const std::string_view sourcePath=ins->sampleFilePath?std::string_view(*ins->sampleFilePath):std::string_view{};
 const bool sourceChanged=next.sourceGeneration!=g.snapshot.sourceGeneration||
                          next.sourceValid!=g.snapshot.sourceValid||sourcePath!=g.sourcePath;
 const bool changed=sourceChanged||next.renderedFrames!=g.snapshot.renderedFrames||next.active!=g.snapshot.active||next.activeGrains!=g.snapshot.activeGrains;
 if(sourceChanged){
  reset_wave_view(g);
  reset_comparison(g);
 }else if(!studio::granular::parametersEqual(g.parameters,ins->granularParams)){
  finish_morph_on_edit(g);
 }
 g.snapshot=next;g.parameters=ins->granularParams;g.seed=ins->granularSeed;
 if(sourcePath!=g.sourcePath)g.sourcePath=sourcePath;
 if(!match){g.waveformReady=false;g.waveformInstrument=-1;g.snapshot.active=false;g.snapshot.activeGrains=0;return changed;}
 const auto viewport=wave_view(region,g.waveformZoom,g.waveformCenter);
 if(g.waveformInstrument!=s.currentInstrument||g.waveformGeneration!=next.sourceGeneration||
    g.waveformFirst!=viewport.first||g.waveformEnd!=viewport.end){
  g.waveformReady=false;
  const auto data=host.sample_waveform(ins->sampleId,kSampleGranularWaveBins,
                                      int(viewport.first),int(viewport.end));
  if(data.size()==g.waveform.size()){
   for(size_t i=0;i<data.size();++i)g.waveform[i]=std::isfinite(data[i])?std::clamp(data[i],-1.f,1.f):0.f;
   g.waveformReady=true;
  }
  g.waveformGeneration=next.sourceGeneration;g.waveformInstrument=s.currentInstrument;
  g.waveformFirst=viewport.first;g.waveformEnd=viewport.end;
  return true;
 }
 return changed;
}
void sample_granular_stop_audition(AppState& s,songcore::SongcoreHost& host){
 if(s.studioAuditionHeld){s.studioAuditionHeld=false;host.stop_preview(true);}
}
bool sample_granular_audition_chord(const ButtonEvent& e,AppState& s,songcore::SongcoreHost& host){
 constexpr uint16_t r=1,select=2;auto& g=s.sampleGranular;
 const auto mask=e.button==Button::R_SHIFT?r:e.button==Button::SELECT?select:0;
 if(mask&&e.action==ButtonAction::RELEASED&&(g.auditionReleaseMask&mask)){
  g.auditionReleaseMask&=~mask;sample_granular_stop_audition(s,host);return true;
 }
 if(s.studioOpen&&sample_granular_studio(s)&&s.studioShortcutRelease&&mask&&(e.button==Button::SELECT||e.mods.select)){
  if(e.button==Button::SELECT&&e.action==ButtonAction::RELEASED)s.studioShortcutRelease=false;
  return true;
 }
 if(!s.studioOpen||!sample_granular_studio(s)||s.studioHelp||s.qwerty.isOpen||s.localSampleRange.isOpen)return false;
 const bool chord=(e.button==Button::SELECT&&e.mods.r)||(e.button==Button::R_SHIFT&&e.mods.select);
 if(!chord)return false;
 if(e.action==ButtonAction::PRESSED){
  g.auditionReleaseMask|=r|select;
  if(e.mods.l||e.mods.a||e.mods.b)return true;
  if(g.recording.ownsRoot)return true;
  if(!s.studioAuditionHeld){
   const auto* ins=instrument(s);
   if(!ins||!ins->granularEnabled){s.studioMessage="Enable Granular, then hold R + Select to audition";return true;}
   if(g.waitingForSource){s.studioMessage="Preparing the new sample";return true;}
   const auto source=host.sample_granular_snapshot(s.currentInstrument,AudioEngine::PREVIEW_LANE);g.publisherFocused=true;
   if(!source.sourceValid){s.studioMessage="Load an actual sample before auditioning";return true;}
   host.preview_instrument(s.currentInstrument);s.studioAuditionHeld=true;g.nextPollMs=0;
  }
 }
 return true;
}
int sample_granular_page_count(){return 6;}
const char* sample_granular_page_name(const AppState& s){const char* names[]={"Grain window","Motion / freeze","Pitch / stereo","Grain envelope","Filter / drive","Level / FX sends"};return names[page(s)];}
int sample_granular_control_count(const AppState& s){return page(s)==3?2:4;}
std::string sample_granular_control_label(const AppState& s,int i){
 if(i<0||i>=sample_granular_control_count(s))return {};
 if(page(s)<4)return controls[key(s,i)].label;
 const char* filterNames[]={"Filter type","Cutoff","Resonance","Drive"};
 const char* mixNames[]={"Note level","Note pan","Reverb send","Delay send"};return page(s)==4?filterNames[i]:mixNames[i];
}
int sample_granular_control_level(const AppState& s,int i){
 if(i<0||i>=sample_granular_control_count(s))return 0;
 if(page(s)<4){const auto& c=controls[key(s,i)];if(c.display==6)return s.sampleGranular.parameters.freeze?255:0;
  return int(std::lround(std::clamp((s.sampleGranular.parameters.*(c.member)-c.lo)/(c.hi-c.lo),0.f,1.f)*255));}
 const auto* ins=instrument(s);if(!ins)return 0;
 if(page(s)==4){const int v[]={filter(*ins)*85,ins->filterCut,ins->filterRes,ins->drive};return v[i];}
 const int v[]={ins->volume,ins->pan,ins->reverbSend,ins->delaySend};return v[i];
}
std::string sample_granular_control_value(const AppState& s,int i){
 if(i<0||i>=sample_granular_control_count(s))return {};
 char out[48];
 if(page(s)<4){const auto& c=controls[key(s,i)];const auto& p=s.sampleGranular.parameters;if(c.display==6)return p.freeze?"ON":"OFF";
  const float value=p.*(c.member);
  switch(c.display){case 1:std::snprintf(out,sizeof out,"%.0f ms",value);break;case 2:std::snprintf(out,sizeof out,"%.1f Hz",value);break;
   case 3:std::snprintf(out,sizeof out,"%+.1f st",value);break;case 4:std::snprintf(out,sizeof out,"+/- %.1f st",value);break;
   case 5:std::snprintf(out,sizeof out,"%+.2f x",value);break;default:std::snprintf(out,sizeof out,"%.0f %%",value*100);break;}
  return out;
 }
 const auto* ins=instrument(s);if(!ins)return "--";
 if(page(s)==4&&i==0){const char* names[]={"Off","Low pass","High pass","Band pass"};return names[filter(*ins)];}
 const int v=sample_granular_control_level(s,i);
 if(page(s)==5&&i==1){if(std::abs(v-128)<=1)return "Center";std::snprintf(out,sizeof out,"%d %% %c",std::abs(v-128)*100/128,v<128?'L':'R');}
 else std::snprintf(out,sizeof out,"%d %%",int(std::lround(v*100./255.)));return out;
}
void sample_granular_edit(AppState& s,songcore::SongcoreHost& host,int row,int direction,bool coarse){
 sample_granular_sync(s);s.studioConfirm=false;
 if(row==kGranularWaveRow){
  if(s.sampleGranular.waveformMode==GranularWaveformMode::Position)edit_wave_position(s,host,direction,coarse);
  else edit_wave_view(s,direction,coarse);
  return;
 }
 if(row==studio_synth_apply_row(s)&&s.sampleGranular.action==kGranularCompareAction){
  if(coarse&&direction>0)capture_comparison(s);
  if(!coarse)edit_morph(s,host,direction);
  return;
 }
 if(row==studio_synth_apply_row(s)&&s.sampleGranular.action==kGranularUseTakeAction){
  granular_recording_select_part(s,direction);return;
 }
 if(row==2){
  const int current=s.sampleGranular.factoryPreset;
  const int step=direction*(coarse?kSampleGranularPerCategory:1);
  apply_factory_preset(s,host,(current+step+kSampleGranularFactoryCount)%kSampleGranularFactoryCount);
  return;
 }
 const int i=row-3;if(i<0||i>=sample_granular_control_count(s))return;
 if(page(s)<4){const auto& c=controls[key(s,i)];auto next=s.sampleGranular.parameters;
  if(c.display==6)next.freeze=direction>0;else next.*(c.member)=std::clamp(next.*(c.member)+direction*(coarse?c.coarse:c.fine),c.lo,c.hi);
  publish(s,host,next);return;
 }
 auto& ins=host.edit_project().instruments[s.currentInstrument];const int step=direction*(coarse?16:1);
 if(page(s)==4){if(i==0){const char* types[]={"off","lp","hp","bp"};ins.filterType=types[std::clamp(filter(ins)+direction,0,3)];if(ins.filterType!="off"&&!ins.filterCut)ins.filterCut=255;}
  else{int* values[]={&ins.filterCut,&ins.filterRes,&ins.drive};*values[i-1]=std::clamp(*values[i-1]+step,0,255);}}
 else{int* values[]={&ins.volume,&ins.pan,&ins.reverbSend,&ins.delaySend};*values[i]=std::clamp(*values[i]+step,0,255);}
 host.push_instrument(s.currentInstrument,page(s)==4||i>=2);modified(s);
 s.studioMessage=page(s)==5&&i<2?"Level / pan applies to the next note. Grain level / spread edits live.":"LIVE: instrument filter and sends updated";
}
void sample_granular_navigate(AppState& s,Button direction){
 const int count=sample_granular_control_count(s),apply=studio_synth_apply_row(s);
 const int row=s.studioRow;
 const bool control=row>=3&&row<3+count;
 const bool left=direction==Button::DPAD_LEFT,right=direction==Button::DPAD_RIGHT;
 const int column=std::clamp(control?row-3:s.studioSynthColumn,0,count-1);
 s.studioSynthColumn=column;
 if(left||right){
  const int step=right?1:-1;
  if(row<=1)s.studioRow=right?1:0;
  else if(row==apply){
   auto& action=s.sampleGranular.action;
   action=(action+kGranularWorkflowActionCount+step)%kGranularWorkflowActionCount;
  }else if(control){
   s.studioSynthColumn=std::clamp(column+step,0,count-1);
   s.studioRow=3+s.studioSynthColumn;
  }
 }else if(direction==Button::DPAD_UP){
  if(row==2)s.studioRow=1;
  else if(row==kGranularWaveRow)s.studioRow=2;
  else if(control)s.studioRow=kGranularWaveRow;
  else if(row==apply)s.studioRow=3+column;
 }else if(direction==Button::DPAD_DOWN){
  if(row<=1)s.studioRow=2;
  else if(row==2)s.studioRow=kGranularWaveRow;
  else if(row==kGranularWaveRow)s.studioRow=3+column;
  else if(control)s.studioRow=apply;
 }
 auto& g=s.sampleGranular;
 if(row!=kGranularWaveRow&&s.studioRow==kGranularWaveRow&&g.waveformMode==GranularWaveformMode::Position){
  const auto region=wave_region(g.snapshot);
  if(wave_source_ready(s,region))show_position(g,region,wave_position_frame(region,g.parameters.position));
 }
 s.studioConfirm=false;s.studioMessage.clear();
}
bool sample_granular_wave_action(AppState& s){
 if(!sample_granular_studio(s)||s.studioRow!=kGranularWaveRow)return false;
 sample_granular_sync(s);
 auto& g=s.sampleGranular;
 const auto region=wave_region(g.snapshot);
 if(!wave_source_ready(s,region)){
  s.studioMessage="Load a sample to move Position";
  return true;
 }
 if(g.waveformMode==GranularWaveformMode::View){
  g.waveformMode=GranularWaveformMode::Position;
  show_position(g,region,wave_position_frame(region,g.parameters.position));
 }else{
  g.waveformMode=GranularWaveformMode::View;
 }
 s.studioMessage.clear();
 return true;
}
bool sample_granular_preset_picker_open(const AppState& s){
 return s.studioOpen&&sample_granular_studio(s)&&s.sampleGranular.presetPicker.project;
}
bool sample_granular_open_preset_picker(AppState& s){
 if(!s.studioOpen||!sample_granular_studio(s)||s.studioRow!=2||!instrument(s))return false;
 sample_granular_sync(s);
 auto& g=s.sampleGranular;
 const int index=std::clamp(g.factoryPreset,0,kSampleGranularFactoryCount-1);
 g.presetPicker={s.project,s.currentInstrument,index/kSampleGranularPerCategory,
                 index%kSampleGranularPerCategory};
 s.studioConfirm=false;
 s.studioMessage.clear();
 return true;
}
void sample_granular_close_preset_picker(AppState& s){
 s.sampleGranular.presetPicker={};
}
int sample_granular_picker_count(const AppState& s){
 return s.sampleGranular.presetPicker.bank==kSampleGranularFavoritesBank
     ? static_cast<int>(s.settings.sampleGranularFavorites.size()) : kSampleGranularPerCategory;
}
int sample_granular_picker_index(const AppState& s,int item){
 if(item<0||item>=sample_granular_picker_count(s))return -1;
 const int bank=s.sampleGranular.presetPicker.bank;
 return bank==kSampleGranularFavoritesBank?s.settings.sampleGranularFavorites[item]
                                        :bank*kSampleGranularPerCategory+item;
}
bool sample_granular_preset_is_favorite(const AppState& s,int index){
 const auto& favorites=s.settings.sampleGranularFavorites;
 return std::find(favorites.begin(),favorites.end(),index)!=favorites.end();
}
namespace {
void choose_picker_bank(GranularPresetPicker& picker,int bank){
 if(picker.bank==bank)return;
 if(bank==kSampleGranularFavoritesBank){
  picker.returnBank=picker.bank;
  picker.returnItem=picker.item;
  picker.item=0;
 }else if(picker.bank==kSampleGranularFavoritesBank){
  picker.item=picker.returnItem;
 }
 picker.bank=bank;
}
void toggle_picker_favorite(AppState& s){
 auto& picker=s.sampleGranular.presetPicker;
 if(picker.banksFocused)return;
 const int index=sample_granular_picker_index(s,picker.item);
 if(index<0)return;
 auto& favorites=s.settings.sampleGranularFavorites;
 const auto found=std::find(favorites.begin(),favorites.end(),index);
 if(found!=favorites.end()){
  favorites.erase(found);
  picker.item=std::clamp(picker.item,0,std::max(0,sample_granular_picker_count(s)-1));
  s.studioMessage="Removed from Favorites";
 }else if(favorites.size()<kSampleGranularPerCategory){
  favorites.push_back(index);
  s.studioMessage="Added to Favorites";
 }else{
  s.studioMessage="Favorites full / remove one of the 16 first";
 }
}
GranularPresetShortcut picker_shortcut(Button button){
 if(button==Button::DPAD_UP)return GranularPresetShortcut::Favorites;
 if(button==Button::DPAD_DOWN)return GranularPresetShortcut::Mark;
 return GranularPresetShortcut::None;
}
}
bool sample_granular_preset_picker_button(const ButtonEvent& e,AppState& s,songcore::SongcoreHost& host){
 if(!sample_granular_preset_picker_open(s))return false;
 auto& picker=s.sampleGranular.presetPicker;
 if(picker.project!=s.project||picker.instrument!=s.currentInstrument||!instrument(s)){
  sample_granular_close_preset_picker(s);
  return true;
 }
 if(e.cancelled||e.button==Button::START||e.button==Button::B){
  picker.chooseArmed=false;
  picker.shortcut=GranularPresetShortcut::None;
  if(!e.cancelled&&e.button==Button::B&&e.action==ButtonAction::RELEASED)
   sample_granular_close_preset_picker(s);
  return e.cancelled||e.button==Button::B;
 }
 const bool pressed=e.action==ButtonAction::PRESSED;
 const bool plain=!e.mods.b&&!e.mods.l&&!e.mods.r&&!e.mods.select;
 if(e.button==Button::A){
  picker.shortcut=GranularPresetShortcut::None;
  if(pressed)picker.chooseArmed=plain;
  else if(std::exchange(picker.chooseArmed,false)&&plain){
   if(picker.banksFocused)picker.banksFocused=false;
   else{
    const int index=sample_granular_picker_index(s,picker.item);
    if(index>=0&&apply_factory_preset(s,host,index))sample_granular_close_preset_picker(s);
   }
  }
  return true;
 }
 const auto shortcut=picker_shortcut(e.button);
 if(!pressed){
  if(shortcut!=GranularPresetShortcut::None&&picker.shortcut==shortcut){
   picker.shortcut=GranularPresetShortcut::None;
   if(plain&&e.mods.a){
    if(shortcut==GranularPresetShortcut::Mark)toggle_picker_favorite(s);
    else{
     const int bank=picker.bank==kSampleGranularFavoritesBank?picker.returnBank:kSampleGranularFavoritesBank;
     choose_picker_bank(picker,bank);
     picker.banksFocused=false;
     s.studioMessage.clear();
    }
   }
  }
  return true;
 }
 // Repeating a held direction keeps the shortcut armed until that direction is released.
 if(picker.chooseArmed&&plain&&e.mods.a)picker.shortcut=shortcut;
 else if(picker.shortcut!=shortcut)picker.shortcut=GranularPresetShortcut::None;
 picker.chooseArmed=false;
 if(e.mods.a||e.mods.b||e.mods.select)return true;
 s.studioMessage.clear();
 const bool left=e.button==Button::DPAD_LEFT,right=e.button==Button::DPAD_RIGHT;
 if(e.button==Button::L_SHIFT||e.button==Button::R_SHIFT||(picker.banksFocused&&(left||right))){
  const int step=(right||e.button==Button::R_SHIFT)?1:-1;
  choose_picker_bank(picker,std::clamp(picker.bank+step,0,kSampleGranularFavoritesBank));
  return true;
 }
 if(picker.banksFocused){
  if(e.button==Button::DPAD_DOWN)picker.banksFocused=false;
  return true;
 }
 constexpr int columns=kSampleGranularPickerColumns;
 const int column=picker.item%columns;
 if(left&&column>0)--picker.item;
 if(right&&column<columns-1&&picker.item+1<sample_granular_picker_count(s))++picker.item;
 if(e.button==Button::DPAD_UP){
  if(picker.item>=columns)picker.item-=columns;
  else picker.banksFocused=true;
 }
 if(e.button==Button::DPAD_DOWN&&picker.item+columns<sample_granular_picker_count(s))picker.item+=columns;
 return true;
}
bool sample_granular_load_source(AppState& s,songcore::SongcoreHost& host,const std::string& path){
 if(preserve_pending_sample_edit(s))return false;
 auto& g=s.sampleGranular;const auto old=host.sample_granular_snapshot(s.currentInstrument);g.publisherFocused=true;
 if(!host.load_sample_granular_patch(s.currentInstrument,path,g.parameters,g.seed)){
  s.studioMessage=host.sample_granular_error();return false;}
 wait_for_source(g,old.sourceGeneration);g.sourcePath=path;modified(s);s.studioMessage="Sample loaded / Granular is live";return true;
}
bool sample_granular_load_preset(AppState& s,songcore::SongcoreHost& host,FileSystem& fs,const std::string& path,bool* sampleReplaced){
 if(sampleReplaced)*sampleReplaced=false;
 for(const auto& file:fs.list_files(fs.parent_path(path)))if(file.path==path&&file.size>64*1024){s.studioMessage="Preset is too large / current sound kept";return false;}
 std::string text;if(!fs.read_file(path,text)||text.size()>64*1024){s.studioMessage="Cannot read preset / current sound kept";return false;}
 SampleGranularPresetFile preset;std::string error;
 if(!parse_sample_granular_preset(text,preset,error)){s.studioMessage=error+" / current sound kept";return false;}
 auto& g=s.sampleGranular;
 if(preset.hasSample){if(preserve_pending_sample_edit(s))return false;
  std::string source;if(!resolve_sample(fs,path,preset.sampleReference,source)){s.studioMessage="Preset sample missing or outside its folder / sound kept";return false;}
  const auto old=host.sample_granular_snapshot(s.currentInstrument);g.publisherFocused=true;
  if(!host.load_sample_granular_patch(s.currentInstrument,source,preset.parameters,preset.seed)){s.studioMessage=host.sample_granular_error();return false;}
  wait_for_source(g,old.sourceGeneration);g.sourcePath=source;g.seed=preset.seed;
 }
 else{
  const auto* ins=instrument(s);if(!ins||!host.update_sample_granular(s.currentInstrument,ins->granularEnabled,preset.parameters)){s.studioMessage=host.sample_granular_error();return false;}
  finish_morph_on_edit(g);
 }
 g.parameters=preset.parameters;g.presetName=preset.name.empty()?path_stem(path):preset.name;g.custom=true;g.nextPollMs=0;
 if(sampleReplaced)*sampleReplaced=preset.hasSample;
 modified(s);s.studioMessage=preset.hasSample?"Sample + Granular patch loaded":"Grain settings loaded / sample and instrument preserved";return true;
}
bool sample_granular_action(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs){
 auto& g=s.sampleGranular;const auto* ins=instrument(s);if(!ins)return false;
 if(g.action==kGranularCompareAction){
  if(!g.compareAvailable)return capture_comparison(s);
  if(!g.snapshot.sourceValid||g.waitingForSource){
   s.studioMessage="Wait for the sample before comparing grain settings";
   return false;
  }
  const bool morphing=g.compareMorph.amount>=0;
  const bool nextB=morphing?g.compareMorph.amount==0:!g.compareLiveB;
  const auto previous=morphing?(nextB?g.compareMorph.a:g.compareMorph.b):ins->granularParams;
  const auto next=morphing?(nextB?g.compareMorph.b:g.compareMorph.a):g.compareParameters;
  if(!publish(s,host,next,/*morphing=*/true))return false;
  g.compareMorph.amount=-1;
  g.compareParameters=previous;
  g.compareLiveB=nextB;
  s.studioMessage=g.compareLiveB?"Comparing grain settings B":"Comparing grain settings A";
  return true;
 }
 if(g.action==kGranularRecordAction)return sample_granular_recording_toggle(s,host,engine,fs);
 if(g.action==kGranularUseTakeAction){
  if(preserve_pending_sample_edit(s))return false;
  if(host.is_playing()){s.studioMessage="Stop playback before opening the saved take";return false;}
  return sample_granular_request_take(s);
 }
 if((g.action==1||g.action==2)&&preserve_pending_sample_edit(s))return false;
 if(g.action==0){
  if(!host.update_sample_granular(s.currentInstrument,!ins->granularEnabled,g.parameters)){s.studioMessage=host.sample_granular_error();return false;}
  modified(s);s.studioMessage=ins->granularEnabled?"Granular active / sample preserved":"Granular bypassed / sample preserved";return true;
 }
 if(g.action==1){sample_granular_stop_audition(s,host);g.request=SampleGranularRequest::LOAD_SOURCE;return true;}
 if(g.action==2){if(!g.snapshot.sourceValid){s.studioMessage="Load an actual sample before opening Crop";return false;}
  sample_granular_stop_audition(s,host);g.request=SampleGranularRequest::CROP_SOURCE;return true;}
 if(g.action==4){sample_granular_stop_audition(s,host);g.request=SampleGranularRequest::LOAD_PRESET;return true;}
 if(g.action!=3)return false;
 const auto folder=fs.make_app_folder("Presets/SampleGranular/User");if(folder.empty()){s.studioMessage="Choose a writable home for the preset";return false;}
 SampleGranularPresetFile preset;preset.parameters=g.parameters;preset.seed=g.seed;preset.name="User Grain "+std::to_string(g.userSlot+1);
 char name[40];int slot=g.userSlot;
 for(;slot<g.userSlot+4096;++slot){std::snprintf(name,sizeof name,"/User-Grain-%04d.ptgrain",slot+1);if(!fs.file_exists(folder+name))break;}
 if(slot>=g.userSlot+4096){s.studioMessage="No unused preset filename / existing files kept";return false;}
 preset.name="User Grain "+std::to_string(slot+1);
 if(!fs.write_file(folder+name,serialize_sample_granular_preset(preset))){s.studioMessage="Save failed / existing sample preserved";return false;}
 s.studioMessage="Saved "+std::string(name+1)+" / sample not copied";g.userSlot=slot+1;return true;
}
} // namespace pt::ui
