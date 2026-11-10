#include "studio.h"
#include "ui/studio_help.h"
#include "ui/demo_font_style.h"
#include "studio/synth.h"
#include "studio/rack.h"
#include "studio/punch.h"
#include "songcore/project_io.h"
#include "ui/helpers.h"
#include "ui/live_resample.h"
#include "ui/studio_preset_io.h"
#include "ui/modules/theme_editor.h"
#include <algorithm>
#include <cstdio>
namespace pt::ui {
namespace {
const char* controls[]={"ACTIVE","MUTABLE EFFECT","WET / DRY","COLOR","TEXTURE","FEEDBACK","SATURATION","COMP THRESHOLD","COMP RATIO","ATTACK","RELEASE","STEREO WIDTH","OUTPUT GAIN","PEAK CEILING","HIGH PASS"};
int masterKey(int row){static const int keys[]={studio::ENABLE,studio::DRIVE,studio::THRESHOLD,studio::RATIO,studio::ATTACK,studio::RELEASE,studio::WIDTH,studio::OUTPUT,studio::CEILING,studio::HIGH_PASS};return keys[std::clamp(row-1,0,9)];}
int rows(int tab){return tab==0?7:tab==1?11:tab==2?15:tab==4?7:tab==5?20:tab==6?3:tab==7?source_sampler_row_count():2;}
void changed(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine){++s.projectVersion;auto& p=host.edit_project();for(int i=0;i<studio::COUNT;++i)engine.setStudioControl(i,p.studioMaster[i]);engine.studioRack.tempo.store(p.tempo);}
// FX and mastering have independent parameter groups. Loading one must not erase the other.
void apply_preset(bool effect,const std::array<int,studio::COUNT>& source,songcore::Project& project){
 apply_studio_preset(effect,source,project);
}
bool load_custom_preset(bool effect,const std::string& data,songcore::Project& project){
 std::array<int,studio::COUNT> checked{};
 if(!parse_studio_preset(effect,data,checked,false))return false;
 apply_preset(effect,checked,project);return true;
}
std::string display_control(int key,int v){
 char text[48];
 switch(key){
 case studio::ENABLE:return v?"ON":"OFF";
 case studio::THRESHOLD:std::snprintf(text,sizeof(text),"%.1f dB",-48+48*v/255.f);break;
 case studio::RATIO:std::snprintf(text,sizeof(text),"%.1f : 1",1+19*v/255.f);break;
 case studio::ATTACK:std::snprintf(text,sizeof(text),"%.1f ms",1+99*v/255.f);break;
 case studio::RELEASE:std::snprintf(text,sizeof(text),"%.0f ms",20+980*v/255.f);break;
 case studio::WIDTH:std::snprintf(text,sizeof(text),"%.0f %%",100*v/128.f);break;
 case studio::OUTPUT:std::snprintf(text,sizeof(text),"%+.1f dB",20*(v-128)/128.f);break;
 case studio::CEILING:std::snprintf(text,sizeof(text),"%.1f dBFS",-12+12*v/255.f);break;
 case studio::HIGH_PASS:if(!v)return "OFF";std::snprintf(text,sizeof(text),"%.0f Hz",10+190*v/255.f);break;
 default:std::snprintf(text,sizeof(text),"%.0f %%",100*v/255.f);break;
 }return text;
}
std::string value(const AppState& s,int row){const auto& p=*s.project;
 if(s.studioTab==0){if(row==0)return std::to_string(s.currentInstrument);if(row==1)return studio::model_name(s.studioModel);if(row==2)return s.studioSynthCustom?"CUSTOM MACROS":studio::preset_name(studio::preset_code(s.studioModel,s.studioVariation));if(row>=3&&row<=5){const int values[]={s.studioHarmonics,s.studioTimbre,s.studioMorph};return display_control(-1,values[row-3]);}return s.studioConfirm?"A AGAIN TO REPLACE":"LOAD INTO INSTRUMENT";}
 if(s.studioTab==1){if(row==10)return "BROWSE CATEGORIES";if(row==0)return studio::effect_preset_name(s.studioEffectPreset);if(row==7)return std::to_string(s.studioFxUserSlot+1)+" / 16";if(row>=8)return row==8?"SAVE CUSTOM":"LOAD CUSTOM";int key=row==1?studio::FX:row==2?studio::FX_MIX:row==3?studio::COLOR:row==4?studio::TEXTURE:row==5?studio::FEEDBACK:studio::ENABLE;return key==studio::FX?studio::effect_name(p.studioMaster[key]):display_control(key,p.studioMaster[key]);}
 if(s.studioTab==2){if(row==14)return "BROWSE CATEGORIES";if(row==0)return studio::master_preset_name(s.studioMasterPreset);if(row==11)return std::to_string(s.studioMasterUserSlot+1)+" / 16";if(row>=12)return row==12?"SAVE CUSTOM":"LOAD CUSTOM";return display_control(masterKey(row),p.studioMaster[masterKey(row)]);}
 return row==0?studio::punch_name(s.studioPunch):"HOLD A TO PERFORM";
}
std::string label(const AppState& s,int row){
 if(s.studioTab==0){const char* x[]={"TARGET SLOT","SYNTH MODEL","PRESET","HARMONICS","TIMBRE","MORPH","APPLY"};return x[row];}
 if(s.studioTab==1){const char* x[]={"FACTORY PRESET","MODULE","MIX","COLOR","TEXTURE","FEEDBACK","ACTIVE","USER SLOT","SAVE CUSTOM","LOAD CUSTOM","FX LIBRARY"};return x[row];}
 if(s.studioTab==2)return row==14?"MASTER LIBRARY":row==0?"FACTORY PRESET":row==11?"USER SLOT":row>=12?"MASTER PRESET":controls[masterKey(row)];
 return row==0?"PUNCH EFFECT":"PERFORMANCE";
}
}
std::string studio_value(const AppState& s,int row){return value(s,row);}
std::string studio_label(const AppState& s,int row){return label(s,row);}
int studio_rows(int tab){return rows(tab);}
bool studio_button(const ButtonEvent& e,AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs){
 if(s.localSampleRange.isOpen)return false;
 if(e.button==Button::START&&e.mods.l&&e.mods.r){
  if(e.action==ButtonAction::PRESSED)live_resample_toggle(s,host,engine,fs);
  return true;
 }
 if(s.qwerty.isOpen)return false;
 if(s.studioShortcutRelease&&e.button==Button::SELECT&&e.action==ButtonAction::RELEASED){s.studioShortcutRelease=false;return true;}
 if(studio_help_button(e,s,host,engine))return true;
 if(!s.studioOpen)return false;
 // An external stop/panic may have released DSP without a physical A-up event.
 if(s.studioTab==3&&s.studioAUsed&&engine.studioRack.punch.load()==0)s.studioAUsed=false;
 if((e.button==Button::SELECT&&e.mods.r)||(e.button==Button::R_SHIFT&&e.mods.select)){
  if(e.action==ButtonAction::PRESSED){studio_synth_stop_audition(s,host);drumlab_stop(s.drumLab,host,engine);source_sampler_stop(s.sourceSampler);if(s.captureBackend&&s.capture.active())s.captureBackend->stop();s.studioShortcutRelease=true;s.studioOpen=false;s.studioHelp=false;s.studioAUsed=false;engine.studioRack.punch.store(0);host.stop_preview(true);}return true;
 }
 // The direct Punch chord also closes/switches a Studio page without a hidden action.
 if(!e.mods.r&&!e.mods.a&&!e.mods.b&&((e.button==Button::SELECT&&e.mods.l)||(e.button==Button::L_SHIFT&&e.mods.select))){
  if(e.action==ButtonAction::PRESSED){
   studio_synth_stop_audition(s,host);drumlab_stop(s.drumLab,host,engine);source_sampler_stop(s.sourceSampler);
   if(s.captureBackend&&s.capture.active())s.captureBackend->stop();
   engine.studioRack.punch.store(0);s.studioAUsed=false;s.studioHelp=false;s.studioShortcutRelease=true;
   if(s.studioTab==3){s.studioOpen=false;host.stop_preview(true);}else{s.studioTab=3;s.studioRow=0;}
  }return true;
 }
 if(s.studioTab==0)studio_synth_sync_target(s);
 if(s.studioTab==0&&e.button==Button::L_SHIFT){if(e.action==ButtonAction::RELEASED)studio_synth_audition(s,host,false);else if(!s.studioHelp&&!e.mods.r)studio_synth_audition(s,host,true);return true;}
 if(s.studioTab==0&&e.action==ButtonAction::PRESSED&&e.mods.r&&!e.mods.b&&(e.button==Button::DPAD_LEFT||e.button==Button::DPAD_RIGHT)){
  s.studioSynthPage=(s.studioSynthPage+studio_synth_page_count(s)+(e.button==Button::DPAD_RIGHT?1:-1))%studio_synth_page_count(s);s.studioRow=3;s.studioAUsed|=e.mods.a;s.studioConfirm=false;s.studioMessage.clear();return true;
 }
 if(e.button==Button::START){studio_synth_stop_audition(s,host);drumlab_stop(s.drumLab,host,engine);source_sampler_stop(s.sourceSampler);engine.studioRack.punch.store(0);if(s.studioTab==3)s.studioAUsed=false;s.studioConfirm=false;return false;}
 if(s.studioTab==7&&s.sourceSampler.listFocused&&!s.studioBUsed&&e.button==Button::B)
  return source_sampler_button(e,s.sourceSampler,host,engine);
 if(s.studioTab==5&&e.button!=Button::B&&!(is_dpad(e.button)&&e.mods.b)){
  if(e.button==Button::A&&e.action==ButtonAction::RELEASED&&s.studioAUsed){s.studioAUsed=false;s.drumLab.aUsed=false;return true;}
  const auto serial=s.drumLab.applySerial;
  const bool handled=drumlab_button(e,s.drumLab,host,engine);
  if(s.drumLab.applySerial!=serial)++s.projectVersion;
  return handled;
 }
 if(s.studioTab==7&&e.button!=Button::B&&!(is_dpad(e.button)&&e.mods.b)){
  if(e.button==Button::A&&e.action==ButtonAction::RELEASED&&s.studioAUsed){s.studioAUsed=false;s.sourceSampler.aUsed=false;return true;}
  const bool handled=source_sampler_button(e,s.sourceSampler,host,engine);
  auto& v=s.sourceSampler;
  if(v.importRequested){
   v.importRequested=false;
   if(s.sourceImportedFor!=v.importPath){s.sourceImportedFor=v.importPath;s.sourceImportedPath.clear();}
   if(v.targetInstrument>=0)s.currentInstrument=std::clamp(v.targetInstrument,0,127);
   if(studio_import_take(s,host,engine,fs,v.importPath,s.sourceImportedPath,v.title,"Samples/Clips")){
    source_sampler_stop(v);s.studioOpen=false;s.pendingSampleEditor=true;
   }else v.status=s.studioMessage;
  }
  return handled;
 }
 if(e.action==ButtonAction::RELEASED){
  if(e.button==Button::A){engine.studioRack.punch.store(0);if(s.studioAUsed){s.studioAUsed=false;return true;}
   if(s.studioTab==4){studio_capture_action(s,host,engine,fs);return true;}
   if(s.studioTab==0&&s.studioRow==studio_synth_apply_row(s)){studio_synth_apply(s,host,engine);
   }else if(s.studioTab==1&&s.studioRow==0){apply_preset(true,studio::effect_preset(s.studioEffectPreset),host.edit_project());changed(s,host,engine);
   }else if(s.studioTab==2&&s.studioRow==0){apply_preset(false,studio::master_preset(s.studioMasterPreset),host.edit_project());changed(s,host,engine);
   }else if((s.studioTab==1&&s.studioRow==10)||(s.studioTab==2&&s.studioRow==14)){
    s.pendingStudioPresetBrowser=true;
   }else if((s.studioTab==2&&s.studioRow>=12)||(s.studioTab==1&&s.studioRow>=8)){
    const auto folder=fs.make_app_folder("Studio");if(folder.empty()){s.studioMessage="CHOOSE A WRITABLE HOME FOLDER";return true;}
    const int slot=s.studioTab==1?s.studioFxUserSlot:s.studioMasterUserSlot;
    char suffix[24];std::snprintf(suffix,sizeof(suffix),"-%02d.json",slot+1);
    const auto path=folder+(s.studioTab==1?"/Custom-FX":"/Custom-Master")+(slot==0?".json":suffix);
    if((s.studioTab==2&&s.studioRow==13)||(s.studioTab==1&&s.studioRow==9)){std::string data;if(fs.read_file(path,data)){if(load_custom_preset(s.studioTab==1,data,host.edit_project())){changed(s,host,engine);s.studioMessage="CUSTOM PRESET LOADED";}else s.studioMessage="INVALID PRESET - SETTINGS KEPT";}else s.studioMessage="NO CUSTOM PRESET FOUND";}
    else{songcore::json j;j["version"]=1;j["kind"]=s.studioTab==1?"effects":"master";j["master"]=host.project().studioMaster;s.studioMessage=fs.write_file(path,j.dump(2))?"CUSTOM PRESET SAVED":"SAVE FAILED - CHECK FOLDER";}
   }
  }else if(e.button==Button::B){if(s.studioBUsed)s.studioBUsed=false;else{studio_synth_stop_audition(s,host);drumlab_stop(s.drumLab,host,engine);source_sampler_stop(s.sourceSampler);if(s.captureBackend&&s.capture.active())s.captureBackend->stop();s.studioOpen=false;s.studioConfirm=false;s.studioMessage.clear();engine.studioRack.punch.store(0);host.stop_preview();}}
  return true;
 }
 if(s.studioTab==3){
  if(e.button==Button::L_SHIFT||e.button==Button::R_SHIFT){
   engine.studioRack.punch.store(0);s.studioAUsed=false;
   const int bank=(studio::punch_bank(s.studioPunch)+(e.button==Button::R_SHIFT?1:studio::kPunchBanks-1))%studio::kPunchBanks;
   s.studioPunch=studio::punch_id(bank,studio::punch_pad(s.studioPunch));return true;
  }
  if(is_dpad(e.button)&&!e.mods.b){
   const int pad=studio::punch_pad(s.studioPunch),x=pad%4,y=pad/4;
   const int nx=std::clamp(x+(e.button==Button::DPAD_RIGHT?1:e.button==Button::DPAD_LEFT?-1:0),0,3);
   const int ny=std::clamp(y+(e.button==Button::DPAD_DOWN?1:e.button==Button::DPAD_UP?-1:0),0,1);
   s.studioPunch=studio::punch_id(studio::punch_bank(s.studioPunch),ny*4+nx);
   if(s.studioAUsed&&e.mods.a)engine.studioRack.punch.store(s.studioPunch);return true;
  }
 }
 if(e.button==Button::A){if(s.studioTab==3){engine.studioRack.tempo.store(host.project().tempo);engine.studioRack.punch.store(s.studioPunch);s.studioAUsed=true;}return true;}
 const int dir=e.button==Button::DPAD_RIGHT||e.button==Button::DPAD_UP?1:-1;
 if(is_dpad(e.button)){
  if(e.mods.b&&(e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN)){
   s.studioBUsed=true;
   if(s.studioTab==3){if(e.button==Button::DPAD_UP)live_resample_use(s,host,engine,fs);else live_resample_cycle_length(s);}
   return true;
  }
  if(e.mods.b){studio_synth_stop_audition(s,host);drumlab_stop(s.drumLab,host,engine);source_sampler_stop(s.sourceSampler);if(s.studioTab==4&&s.captureBackend&&s.capture.active())s.captureBackend->stop();s.studioBUsed=true;s.studioAUsed|=e.mods.a;s.studioConfirm=false;s.studioMessage.clear();s.studioTab=(s.studioTab+(dir>0?1:7))%8;s.studioRow=0;engine.studioRack.punch.store(0);if(s.studioTab==4)studio_capture_refresh(s);if(s.studioTab==7){s.sourceSampler.hasPolled=false;source_sampler_poll(s.sourceSampler,0);}return true;}
  if(!e.mods.a&&s.studioTab==0){studio_synth_navigate(s,e.button);return true;}
  if(!e.mods.a&&s.studioTab==4&&(e.button==Button::DPAD_LEFT||e.button==Button::DPAD_RIGHT)){studio_capture_edit(s,e.button==Button::DPAD_RIGHT?1:-1);return true;}
  if(!e.mods.a){if(e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN){s.studioConfirm=false;s.studioMessage.clear();const int delta=e.button==Button::DPAD_UP?-1:1;s.studioRow=std::clamp(s.studioRow+delta,0,(s.studioTab==0?studio_synth_apply_row(s)+1:rows(s.studioTab))-1);if(s.studioTab==0&&s.studioRow>=3+studio_synth_control_count(s)&&s.studioRow<studio_synth_apply_row(s))s.studioRow=delta>0?studio_synth_apply_row(s):2+studio_synth_control_count(s);}return true;}
  s.studioAUsed=true;s.studioConfirm=false;auto& p=host.edit_project();
  if(s.studioTab==0)studio_synth_edit(s,host,engine,s.studioRow,dir,e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN);
  else if(s.studioTab==1){if(s.studioRow==0)s.studioEffectPreset=std::clamp(s.studioEffectPreset+dir,0,studio::effect_preset_count()-1);else if(s.studioRow==7)s.studioFxUserSlot=std::clamp(s.studioFxUserSlot+dir,0,15);else if(s.studioRow<7){int key=s.studioRow==1?studio::FX:s.studioRow==2?studio::FX_MIX:s.studioRow==3?studio::COLOR:s.studioRow==4?studio::TEXTURE:s.studioRow==5?studio::FEEDBACK:studio::ENABLE;p.studioMaster[key]=std::clamp(p.studioMaster[key]+dir,0,key==studio::FX?19:key==studio::ENABLE?1:255);changed(s,host,engine);}}
  else if(s.studioTab==2){if(s.studioRow==0)s.studioMasterPreset=std::clamp(s.studioMasterPreset+dir,0,studio::master_preset_count()-1);else if(s.studioRow==11)s.studioMasterUserSlot=std::clamp(s.studioMasterUserSlot+dir,0,15);else if(s.studioRow<11){int key=masterKey(s.studioRow);p.studioMaster[key]=std::clamp(p.studioMaster[key]+dir,0,key==studio::ENABLE?1:255);changed(s,host,engine);}}
  
  else if(s.studioTab==4){studio_capture_edit(s,dir);
  }else if(s.studioTab==6){
   if(s.studioRow==0)theme_cycle_builtin(s.theme,dir);
   if(s.studioRow==1)s.theme.surfaceStyle=(s.theme.surfaceStyle+dir+kSurfaceStyleCount)%kSurfaceStyleCount;
   if(s.studioRow==2)s.settings.fontStyle=(valid_demo_font_style(s.settings.fontStyle)+dir+kDemoFontStyleCount)%kDemoFontStyleCount;
  }
 }
 return true;
}
}
