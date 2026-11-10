#include "ui/studio.h"
#include "ui/demo_font_style.h"
#include "ui/product_brand.h"
#include "ui/studio_help.h"
#include "ui/live_resample.h"
#include "ui/premium.h"
#include "ui/brand_graphics.h"
#include "studio/synth.h"
#include "studio/rack.h"
#include "studio/punch.h"
#include "ui/modules/theme_editor.h"
#include "ui/modules/oscilloscope.h"
#include <cmath>
#include <cstdio>
namespace pt::ui {
namespace {
using namespace premium;
void panel_texture(Canvas& c,int x,int y,int w,int h,const Theme& t){
 if(t.surfaceStyle==7||(t.rowEvery4th&0x00ffffffu)==0)return;
 Canvas::ClipScope clip(c,x+5,y+3,w-10,h-6);
 const auto light=with_alpha(t.textValue,.025f),dark=0x16000000u;
 if(t.surfaceStyle==1||t.surfaceStyle==3){
  for(int py=y+5;py<y+h-4;py+=5)c.fill_rect(x+5,py,w-10,1,((py-y)/5)%2?light:dark);
 }else if(t.surfaceStyle==2||t.surfaceStyle==6){
  for(int px=x-h;px<x+w;px+=14){c.line(px,y+h-3,px+h,y+3,light);if(t.surfaceStyle==2)c.line(px+5,y+h-3,px+h+5,y+3,dark);}
 }else if(t.surfaceStyle==4||t.surfaceStyle==5){
  for(int px=x+12;px<x+w-7;px+=28)c.fill_rect(px,y+4,1,h-8,light);
  for(int py=y+11;py<y+h-4;py+=20)c.fill_rect(x+5,py,w-10,1,light);
  if(t.surfaceStyle==4)for(int px=x+12;px<x+w-10;px+=56)c.fill_rect(px-1,y+10,3,3,with_alpha(t.rowCursor,.11f));
 }else if(t.surfaceStyle>=12){
  // New materials have an optional engraved treatment on cards as well as the cached background.
  // Keep it near the edges so knobs, captions and waveform values retain a quiet ground.
  const auto ink=with_alpha(t.rowCursor,.045f);
  for(int px=x+7;px<x+w-7;px+=12){
   c.line(px,y+4,px+8,y+12,ink);
   if(t.surfaceStyle==12)c.line(px+8,y+4,px,y+12,ink);
  }
  if(w>180)brand::mark(c,x+w-38,y+h-35,24,with_alpha(t.rowCursor,.055f),ink);
 }else{
  for(int py=y+6;py<y+h-5;py+=11)for(int px=x+8;px<x+w-6;px+=13){
   const unsigned hash=static_cast<unsigned>(px*31+py*17);c.fill_rect(px+(hash%5),py,1,1,hash&8?light:dark);
  }
 }
}
void studio_card(Canvas& c,int x,int y,int w,int h,const Theme& t,bool focus=false){
 premium::card(c,x,y,w,h,t,focus);panel_texture(c,x,y,w,h,t);
 c.fill_rect(x+8,y+1,w-16,1,with_alpha(t.textValue,focus?.13f:.06f));
 c.fill_rect(x+8,y+h-2,w-16,1,0x34000000u);
 if(focus)c.fill_rect(x+10,y+3,w-20,1,with_alpha(t.rowCursor,.19f));
}
int synth_family(int model){return model>=102?5:model>=94?4:model>=78?3:model>=72?2:model>=24?1:0;}
Theme synth_theme(const AppState& s){
 Theme t=s.theme;
 static constexpr Argb colors[]={0xFF5CD8D0,0xFFB699F3,0xFF78B7EE,0xFFE4BD7A,0xFF8FDEA6,0xFFE89BBE};
 if(t.name!="MONO"&&t.name.find("OLED")==std::string::npos)
  t.rowCursor=mix(t.rowCursor,colors[synth_family(s.studioModel)],t.name=="STUDIO"||t.name=="GRAPHITE"||t.name=="CARBON"?.52f:.20f);
 return t;
}
// Static circuit emblems identify engine families; these are diagrams, never output meters.
void engine_diagram(Canvas& c,int family,int x,int y,Argb color){
 const auto faint=with_alpha(color,.32f);
 if(family==0||family==5){
  for(int i=0;i<3;++i){const int px=x+i*29;c.panel_rect(px,y+2,20,16,0x17000000u,faint,3);
   if(family==0){c.line(px+5,y+13,px+10,y+6,color);c.line(px+10,y+6,px+15,y+13,color);}
   else{c.line(px+5,y+6,px+15,y+14,color);c.line(px+5,y+14,px+15,y+6,color);}
   if(i<2)c.line(px+21,y+10,px+28,y+10,faint);
  }
 }else if(family==1){
  for(int i=0;i<3;++i){c.line(x,y+4+i*5,x+75,y+16-i*5,color);c.line(x,y+16-i*5,x+75,y+4+i*5,faint);}
 }else if(family==2){
  c.line(x+1,y+2,x+1,y+18,faint);c.line(x+76,y+2,x+76,y+18,faint);
  for(int i=0;i<4;++i){const int py=y+4+i*4;c.line(x+3,py,x+38,py+(i%2?3:-2),color);c.line(x+38,py+(i%2?3:-2),x+74,py,faint);}
 }else if(family==3){
  c.line(x+5,y+10,x+38,y+1,color);c.line(x+38,y+1,x+74,y+10,color);c.line(x+74,y+10,x+38,y+19,faint);c.line(x+38,y+19,x+5,y+10,faint);
  c.line(x+22,y+6,x+55,y+14,faint);c.line(x+22,y+14,x+55,y+6,faint);c.fill_rect(x+36,y+8,4,4,color);
 }else{
  for(int i=0;i<18;++i){const int px=x+(i*37)%76,py=y+2+(i*11)%16;c.fill_rect(px,py,i%3+2,2,i%2?color:faint);}
 }
}
void heading(Canvas& c,const AppState& s){
 const auto& t=s.theme;
 c.surface_rect(0,0,640,480,t.background);
 c.draw_label(kProductName,18,14,t.textValue,18);
 c.draw_label("MUSIC / CODE",145,19,t.rowCursor,12);
 brand::mark(c,278,13,27,t.rowCursor,with_alpha(t.textTitle,.42f));
 c.draw_label("KID BUNKA",316,14,t.textValue,12);
 c.draw_label("DEMOSCENE AUDIO LAB",316,30,t.textParam,12);
 if(brand::expressive_material(t))brand::registration(c,452,33,60,t,s.blinkPhaseMs,s.isPlaying||s.studioAuditionHeld||s.studioAUsed);
 const bool audition=s.studioTab==0&&s.studioAuditionHeld;
 chip(c,s.studioTab==3?(s.studioAUsed?"FX HELD":"DRY MIX"):audition?"AUDITION":s.isPlaying?"PLAYING":"STUDIO",530,14,94,t,s.isPlaying||audition||s.studioAUsed);
 c.fill_rect(18,47,606,1,with_alpha(t.textParam,.2f));
 if(s.studioTab==3){
  const char* banks[]={"RHYTHM","TAPE","FILTER","DUB","GLITCH","GRAIN","MOD","DAMAGE"};
  for(int i=0;i<studio::kPunchBanks;++i)chip(c,banks[i],16+i*77,60,72,t,i==studio::punch_bank(s.studioPunch));
  return;
 }
 const char* tabs[]={"Synth","FX Rack","Master","Punch","Input","Drumlab","Style","Digging"};
 for(int i=0;i<8;++i)chip(c,tabs[i],16+i*77,60,72,t,i==s.studioTab);
}
void preset(Canvas& c,const AppState& s,const std::string& title){
 const auto& t=s.theme;studio_card(c,16,102,608,66,t,s.studioRow==0);
 label(c,title,30,109,t.textParam,570);
 label(c,studio_value(s,0),30,129,t.textValue,570,18);
 label(c,s.studioTab==1?studio::effect_preset_description(s.studioEffectPreset):studio::master_preset_description(s.studioMasterPreset),30,152,t.textEmpty,570);
}
void dynamics(Canvas& c,const AppState& s){
 const auto& t=s.theme;const auto& p=s.project->studioMaster;
 studio_card(c,416,180,208,226,t);
 c.draw_label(p[studio::ENABLE]?"Drive / comp / output":"Dynamics bypassed",430,191,t.textParam);
 const int x=436,y=219,w=165,h=106;
 c.fill_rect(x,y,w,h,darken(t.vizBackground,.75f));
 for(int k=0;k<=4;++k){c.fill_rect(x+k*w/4,y,1,h,with_alpha(t.textParam,.13f));c.fill_rect(x,y+k*h/4,w,1,with_alpha(t.textParam,.13f));}
 c.line(x,y+h-1,x+w-1,y,with_alpha(t.textParam,.25f));
 const float threshold=-48+48*p[studio::THRESHOLD]/255.f,ratio=1+19*p[studio::RATIO]/255.f;
 const float gain=20*(p[studio::OUTPUT]-128)/128.f,ceiling=-12+12*p[studio::CEILING]/255.f;
 int last=y+h-1;
 for(int k=0;k<w;++k){const float in=-48+48*k/float(w-1);float out=in;
  if(p[studio::ENABLE]){
   if(p[studio::DRIVE]){const float drive=1+7*p[studio::DRIVE]/255.f;out=20*std::log10(std::max(1e-6f,std::tanh(std::pow(10.f,in/20)*drive)/std::tanh(drive)));}
   if(out>threshold)out=threshold+(out-threshold)/ratio;out=std::min(ceiling,out+gain);
  }
  const int cy=y+h-1-static_cast<int>(std::clamp((out+48)/48,0.f,1.f)*(h-1));if(k)c.line(x+k-1,last,x+k,cy,t.rowCursor);last=cy;}
 c.draw_text("-48",x,y+h+5,t.textEmpty,1,2);c.draw_text("0 dB",x+w-43,y+h+5,t.textEmpty,1,2);
 c.draw_label("Output",430,355,t.textParam);label(c,studio_value(s,8),515,354,t.textValue,96);
 c.draw_label("Peak ceiling",430,379,t.textParam);label(c,studio_value(s,9),515,378,t.rowCursor,96);
}
void list_page(Canvas& c,const AppState& s){
 const auto& t=s.theme;preset(c,s,std::string(s.studioTab==1?"FX selection / ":"MASTER selection / ")+(s.studioTab==1?studio::effect_preset_category(s.studioEffectPreset):studio::master_preset_category(s.studioMasterPreset))+" / A to apply");
 const int first=std::max(1,s.studioRow>6?s.studioRow-6:1);
 for(int i=first;i<std::min(first+7,studio_rows(s.studioTab));++i)
  row(c,16,180+(i-first)*32,390,studio_label(s,i),studio_value(s,i),t,s.studioRow==i);
 if(s.studioTab==2){dynamics(c,s);return;}
 const auto& p=s.project->studioMaster;studio_card(c,416,180,208,226,t);
 c.draw_label("Wet / Dry",430,192,t.textParam);knob(c,520,247,p[studio::FX_MIX],t,s.studioRow==2);
 percent(c,p[studio::FX_MIX],520,291,t,s.studioRow==2);
 c.draw_label("Signal path",430,329,t.textParam);
 chip(c,"IN",430,353,38,t);c.line(470,364,483,364,t.textEmpty);
 chip(c,p[studio::FX]?"FX":"DRY",487,353,54,t,p[studio::FX]&&p[studio::ENABLE]);
 c.line(544,364,557,364,t.textEmpty);chip(c,"OUT",561,353,48,t);
 label(c,studio::effect_name(p[studio::FX]),430,384,t.textValue,180);
}
void synth_page(Canvas& c,const AppState& s){
 const auto t=synth_theme(s);studio_card(c,16,102,100,68,t,s.studioRow==0);
 c.draw_label("Instrument",29,113,t.textParam);
 c.draw_text(songcore::hex2(s.currentInstrument),29,137,t.textValue,2,3);
 const bool live=studio_synth_live(s);
 chip(c,live?"LIVE":"Draft",74,134,36,t,live);
 studio_card(c,126,102,498,68,t,s.studioRow==1);
 c.draw_label("Sound engine",140,113,t.textParam);
 const char* family=s.studioModel>=102?"Studio DSP":s.studioModel>=94?"Granular":s.studioModel>=78?"Elements":s.studioModel>=72?"Rings":s.studioModel>=24?"Braids":"Plaits";
 chip(c,family,508,110,100,t);
 label(c,studio::model_name(s.studioModel),140,137,t.textValue,356,18);
 engine_diagram(c,synth_family(s.studioModel),522,141,t.rowCursor);
 studio_card(c,16,180,608,40,t,s.studioRow==2);
 c.draw_label("Preset",30,195,t.textParam);
 label(c,studio_value(s,2),101,192,t.textValue,501,18);
 const std::string page=std::string(studio_synth_page_name(s))+"   "+std::to_string(s.studioSynthPage+1)+" / "+std::to_string(studio_synth_page_count(s));
 label(c,page,20,222,t.rowCursor,445);
 label(c,"R + Left / Right",493,222,t.textParam,131);
 const int controls=std::clamp(studio_synth_control_count(s),0,4);
 const int cells=std::max(3,controls),width=cells==4?146:196,step=cells==4?154:206;
 for(int i=0;i<cells;++i){const int x=16+i*step;const bool selected=s.studioRow==i+3&&i<controls;
  studio_card(c,x,237,width,130,t,selected);
  if(i>=controls){
   c.draw_label("Output scope",x+14,248,t.textParam);
   scope(c,x+14,278,width-28,66,s.waveform,WAVEFORM_SIZE,t);continue;
  }
  label(c,studio_synth_control_label(s,i),x+12,248,selected?t.rowCursor:t.textParam,width-24);
  knob(c,x+width/2,297,studio_synth_control_level(s,i),t,selected,cells==4?23:25);
  const auto value=studio_synth_control_value(s,i);
  const auto fitted=Canvas::clip_label(value,width-20,18);
  c.draw_label(fitted,x+(width-Canvas::label_width(fitted,18))/2,338,selected?t.rowCursor:t.textValue,18);
 }
 const bool applyFocus=s.studioRow==studio_synth_apply_row(s);
 studio_card(c,16,379,390,30,t,applyFocus);
 const std::string action=s.studioConfirm?"Press A again to replace this instrument":live?"Live editing - changes are heard immediately":"Load once to link this instrument";
 label(c,action,30,386,applyFocus?t.rowCursor:t.textValue,362);
 studio_card(c,416,379,208,30,t,s.studioAuditionHeld);
 label(c,std::string(s.studioAuditionHeld?"Release L: stop ":"Hold L: ")+studio_synth_note_name(s),430,386,s.studioAuditionHeld?t.rowCursor:t.textParam,180);
}
void input_page(Canvas& c,const AppState& s){
 const auto& t=s.theme;const auto& v=s.capture;
 const CaptureInput* selected=v.inputs.empty()?nullptr:&v.inputs[std::clamp(v.selected,0,int(v.inputs.size())-1)];
 const bool phone=selected&&selected->kind.rfind("iphone",0)==0;
 studio_card(c,16,102,608,70,t);
 c.draw_label("RECORD / Sample recorder",30,113,t.textValue,18);
 label(c,s.captureBackend?"Microphone / USB interface / experimental iPhone":"Recording unavailable on this platform",30,145,t.textParam,477);
 char time[32];std::snprintf(time,sizeof(time),"%02lld:%02lld",static_cast<long long>(v.frames/std::max(v.actualRate,1)/60),static_cast<long long>(v.frames/std::max(v.actualRate,1)%60));
 c.draw_text(time,531,119,v.receiving()?t.meterHigh:t.textValue,1,3);
 const auto input=selected?selected->name:"No input - Refresh";
 const bool empty=v.targetInstrument>=0&&s.project&&songcore::instrument_is_free(s.project->instruments[std::clamp(v.targetInstrument,0,127)]);
 const std::string names[]={"Source  < / >","Channels  < / >","Sample rate  < / >","New take slot  < / >"};
 const std::string values[]={input,v.channels==1?"Mono":"Stereo",std::to_string(v.rate)+" Hz / 16 bit",v.targetInstrument<0?"Auto: first empty slot":songcore::hex2(v.targetInstrument)+(empty?" / empty":" / occupied - protected")};
 for(int i=0;i<4;++i)row(c,16,184+i*31,390,names[i],values[i],t,s.studioRow==i);
 studio_card(c,16,312,390,40,t,s.studioRow==4);
 label(c,v.active()?"A  STOP & KEEP TAKE":"A  START RECORDING",31,323,s.studioRow==4?t.rowCursor:t.textValue,359,18);
 studio_card(c,16,358,390,35,t,s.studioRow==5);
 label(c,v.path.empty()?"USE TAKE  / record first":"A  USE TAKE > EDIT & SLICE",31,368,s.studioRow==5?t.rowCursor:t.textValue,359,16);
 studio_card(c,16,399,390,20,t,s.studioRow==6);
 label(c,"A  REFRESH INPUTS / reconnect devices",31,402,s.studioRow==6?t.rowCursor:t.textParam,359,12);
 studio_card(c,416,184,208,235,t);c.draw_label("Actual input level",430,196,t.textParam);
 meter(c,430,221,180,12,v.receiving()?v.peakL:0.f,t);meter(c,430,242,180,12,v.receiving()?v.peakR:0.f,t);
 label(c,studio_capture_status(v),430,266,v.receiving()?t.meterHigh:t.rowCursor,180,14);
 std::string detail=v.inputError;
 if(detail.empty()&&phone){
  const bool current=v.requestedId==selected->id&&v.state!="idle";
  detail=(v.receiving()?"Receiving audio. ":v.phoneTrusted&&current?"Trust verified; waiting for audio. ":selected->connected?"Phone detected; audio not verified. ":"No phone detected. ");
  detail+=current&&!v.message.empty()?v.message:selected->detail;
  detail+=" Unlock iPhone; allow Trust if asked. Charging alone is not audio. Experimental.";
 }else if(detail.empty())detail=v.message.empty()?"Choose a source, then Start. USB charging alone is not audio. Software monitoring is off.":v.message;
 // Keep real backend errors readable rather than silently clipping to one line.
 size_t offset=0;for(int line=0;line<8&&offset<detail.size();++line){
  size_t end=std::min(detail.size(),offset+26);
  if(end<detail.size()){const auto space=detail.rfind(' ',end);if(space!=std::string::npos&&space>offset)end=space;}
  label(c,detail.substr(offset,end-offset),430,291+line*15,t.textParam,180,12);
  offset=end;while(offset<detail.size()&&detail[offset]==' ')++offset;
 }
}

void style_page(Canvas& c,const AppState& s){
 const auto& t=s.theme;
 studio_card(c,16,102,608,54,t,s.studioRow==0);c.draw_label("Design collection",30,109,t.textParam);
 c.draw_label(t.name,30,128,t.textValue,18);label(c,std::to_string(theme_builtins().size())+" designs",503,130,t.textParam,100);
 studio_card(c,16,166,608,46,t,s.studioRow==1);c.draw_label("Material",30,182,t.textParam);
 label(c,surface_style_name(t.surfaceStyle),145,178,t.rowCursor,460,18);
 studio_card(c,16,222,608,64,t,s.studioRow==2);c.draw_label("Typeface",30,237,t.textParam);
 label(c,demo_font_style_name(s.settings.fontStyle),145,233,t.rowCursor,460,18);
 label(c,demo_font_style_detail(s.settings.fontStyle),30,263,t.textParam,574,12);
 studio_card(c,16,298,608,118,t);c.draw_label("TRACKWERK / TYPE LAB",30,309,t.textValue,18);
 label(c,std::to_string(valid_demo_font_style(s.settings.fontStyle)+1)+" / "+std::to_string(kDemoFontStyleCount),536,312,t.rowCursor,75,12);
 c.draw_text("C#4 A-3 120 00 FF",30,341,t.rowCursor,1,3);
 c.draw_text("0/O  1/I/l  8/B  E/F  #",30,371,t.textValue,1,2);
 label(c,"SAMPLE / SLICE / SYNTH / PUNCH",30,396,t.textParam,580,12);
}
void punch_icon(Canvas& c,int bank,int x,int y,Argb ink){
 const auto faint=with_alpha(ink,.25f);
 for(int i=0;i<4;++i){
  const int xx=x+i*12;
  if(bank==0||bank==4){c.fill_rect(xx,y+2+(i%2)*4,7,bank==4?5:14-(i%2)*4,ink);}
  else if(bank==1){c.line(xx,y+15,xx+5,y+2,ink);c.line(xx+5,y+2,xx+10,y+15,faint);}
  else if(bank==2){c.line(xx,y+13-i*3,xx+12,y+10-i*3,ink);}
  else if(bank==3){c.panel_rect(xx,y+2+i,9,14-i*3,0x00000000u,with_alpha(ink,1-i*.19f),3);}
  else if(bank==5){c.fill_rect(xx+(i*5)%9,y+3+(i*7)%10,3+i%3,3,ink);c.fill_rect(xx+7,y+12-i*2,2,2,faint);}
  else if(bank==6){c.line(xx,y+10,xx+6,y+2,ink);c.line(xx+6,y+2,xx+12,y+10,faint);}
  else{c.line(xx,y+3,xx+3,y+15,ink);c.line(xx+3,y+15,xx+7,y+3,ink);c.line(xx+7,y+3,xx+11,y+10,faint);}
 }
}
void punch_page(Canvas& c,const AppState& s){
 const auto& t=s.theme;const int bank=studio::punch_bank(s.studioPunch),pad=studio::punch_pad(s.studioPunch);
 studio_card(c,16,102,608,51,t);
 c.draw_label(studio::punch_bank_name(bank),31,113,t.textValue,18);
 label(c,s.studioAUsed?std::string(studio::punch_name(s.studioPunch))+" / release A to restore":"64 gestures / 8 banks. Select a pad, then hold A.",31,136,s.studioAUsed?t.rowCursor:t.textParam,485);
 chip(c,s.studioAUsed?"HELD A":"DRY",535,115,73,t,s.studioAUsed);
 for(int i=0;i<studio::kPunchPads;++i){
  const int x=16+(i%4)*154,y=166+(i/4)*92,id=studio::punch_id(bank,i);
  const bool selected=i==pad,active=selected&&s.studioAUsed;
  studio_card(c,x,y,146,81,t,selected);
  if(selected)c.fill_rect(x+1,y+9,3,63,active?t.rowCursor:with_alpha(t.rowCursor,.65f));
  label(c,std::to_string(i+1),x+12,y+10,t.textParam,20);
  if(active)chip(c,"HELD",x+91,y+7,44,t,true);
  else if(selected)label(c,"READY",x+93,y+12,t.rowCursor,43);
  if(active)brand::punch_motion(c,bank,i,x+35,y+11,49,24,s.blinkPhaseMs,t.rowCursor);
  else punch_icon(c,bank,x+38,y+12,selected?t.rowCursor:with_alpha(t.textTitle,.48f));
  label(c,studio::punch_name(id),x+12,y+48,selected?t.textValue:t.textParam,124);
  c.fill_rect(x+12,y+69,122,1,with_alpha(selected?t.rowCursor:t.textParam,selected?.32f:.12f));
  if(active){
   // A short moving highlight describes the held gesture, not input level or render progress.
   const int position=std::clamp(s.blinkPhaseMs,0,999)*102/1000;
   c.fill_rect(x+12+position,y+69,20,2,t.rowCursor);
   c.stroke_rect(x+4,y+4,138,73,with_alpha(t.rowCursor,.22f));
  }
 }
 studio_card(c,16,355,608,23,t);
 label(c,studio::punch_detail(s.studioPunch),30,360,t.textValue,572);
 draw_live_resample_strip(c,s,16,382,608,35);
}

} // namespace
void draw_studio(Canvas& c,const AppState& s){
 c.set_surface_style(s.theme.surfaceStyle);c.set_font_style(s.settings.fontStyle);heading(c,s);const auto& t=s.theme;
 if(s.studioHelp){draw_studio_help(c,s);return;}
 if(s.studioTab==0)synth_page(c,s);
 else if(s.studioTab==1||s.studioTab==2)list_page(c,s);
 else if(s.studioTab==3)punch_page(c,s);
 else if(s.studioTab==4)input_page(c,s);
 else if(s.studioTab==5){drumlab_draw(c,s.drumLab,t,*s.project);return;}
 else if(s.studioTab==6)style_page(c,s);
 else if(s.studioTab==7){source_sampler_draw(c,s.sourceSampler,t,*s.project);return;}
 // Drumlab draws its own grid between the shared heading and footer.
 c.fill_rect(18,424,606,1,with_alpha(t.textParam,.2f));
 const std::string hint=s.studioTab==3?"L / R: Category     D-pad: Select pad     Select: Help":s.studioTab==0?"B + Left / Right: Studio pages     Select: Help":s.studioTab==4?"B + Left / Right: Studio pages    Select: Recording help":"B + Left / Right: Page     A + D-pad: Edit     Select: Help";
 label(c,s.studioMessage.empty()?hint:s.studioMessage,22,435,s.studioMessage.empty()?t.textParam:t.rowCursor,590);
 const auto controls=s.studioTab==3?"Hold A: Punch    Release A: Restore    Start: Play / Stop    B: Back":s.studioTab==0?"A + arrows: Edit     Hold L: Audition     Start: Playback     B: Back":s.studioTab==4?"Up / Down: Select    Left / Right: Change    A: Action    B: Back":"A: Apply     Start: Playback     B: Back";
 label(c,controls,22,458,t.textEmpty,590);
}
} // namespace pt::ui
