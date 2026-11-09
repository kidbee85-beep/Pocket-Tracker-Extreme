#include "ui/studio.h"
#include "ui/premium.h"
#include "studio/synth.h"
#include "studio/rack.h"
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
 c.draw_label("Pocket Tracker",18,14,t.textValue,18);
 c.draw_label("Extreme",147,14,t.rowCursor,18);
 const bool audition=s.studioTab==0&&s.studioAuditionHeld;
 chip(c,audition?"AUDITION":s.isPlaying?"PLAYING":"STUDIO",530,14,94,t,s.isPlaying||audition);
 c.fill_rect(18,47,606,1,with_alpha(t.textParam,.2f));
 const char* tabs[]={"Synth","FX Rack","Master","Punch","Input","Drumlab","Style","Digging"};
 for(int i=0;i<8;++i)chip(c,tabs[i],16+i*77,60,72,t,i==s.studioTab);
}
void preset(Canvas& c,const AppState& s,const std::string& title){
 const auto& t=s.theme;studio_card(c,16,102,608,66,t,s.studioRow==0);
 c.draw_label(title,30,112,t.textParam);
 label(c,studio_value(s,0),30,133,t.textValue,570,18);
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
 const auto& t=s.theme;preset(c,s,s.studioTab==1?"Effect preset selection - A to apply":"Master preset selection - A to apply");
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
 studio_card(c,16,102,608,70,t);
 c.draw_label(v.active()?"Recording input":"Sample recorder",30,114,t.textValue,18);
 label(c,s.captureBackend?(v.routedName.empty()?"Select microphone or USB audio input":v.routedName):"Recording unavailable",30,145,t.textParam,445);
 char time[32];std::snprintf(time,sizeof(time),"%02lld:%02lld",static_cast<long long>(v.frames/std::max(v.actualRate,1)/60),static_cast<long long>(v.frames/std::max(v.actualRate,1)%60));
 c.draw_text(time,531,119,v.active()?t.meterHigh:t.textValue,1,3);
 const auto input=v.inputs.empty()?"Refresh inputs":v.inputs[std::clamp(v.selected,0,int(v.inputs.size())-1)].name;
 const std::string names[]={"Input device","Channels","Sample rate","Target instrument","Recording","Use take","Devices"};
 const std::string values[]={input,v.channels==1?"Mono":"Stereo",std::to_string(v.rate)+" Hz / 16 bit",songcore::hex2(s.currentInstrument),v.active()?"Stop & save":"Start recording",v.path.empty()?"No take yet":"Import into instrument","Refresh inputs"};
 for(int i=0;i<7;++i)row(c,16,184+i*31,390,names[i],values[i],t,s.studioRow==i);
 studio_card(c,416,184,208,214,t);c.draw_label("Input level",430,196,t.textParam);
 meter(c,430,226,180,13,v.peakL,t);meter(c,430,250,180,13,v.peakR,t);
 c.draw_label("-60",430,270,t.textEmpty);c.draw_label("0 dBFS",568,270,t.textEmpty);
 label(c,v.active()?"REC":v.state,430,307,v.active()?t.meterHigh:t.rowCursor,175,18);
 label(c,v.message.empty()?"Stop before importing":v.message,430,339,t.textParam,176);
 c.draw_label("Software monitoring off",430,374,t.textEmpty);
}
void style_page(Canvas& c,const AppState& s){
 const auto& t=s.theme;
 studio_card(c,16,102,608,68,t,s.studioRow==0);c.draw_label("Design collection",30,113,t.textParam);
 c.draw_label(t.name,30,137,t.textValue,18);label(c,"24 designs",503,140,t.textParam,100);
 studio_card(c,16,180,608,47,t,s.studioRow==1);c.draw_label("Material",30,195,t.textParam);
 label(c,surface_style_name(t.surfaceStyle),145,192,t.rowCursor,460,18);
 studio_card(c,16,240,608,168,t);c.draw_label("Your workspace",32,254,t.textParam);
 for(int i=0;i<3;++i){int x=32+i*194;c.panel_rect(x,282,182,106,t.background,with_alpha(t.textParam,.22f),6);
  c.draw_label(i==0?"Sound":i==1?"Sequence":"Mix",x+14,296,t.textValue,18);
  for(int k=0;k<5;++k)c.fill_rect(x+14,331+k*8,154-k*21,3,k%2?t.rowCursor:t.textTitle);
 }
}
void punch_page(Canvas& c,const AppState& s){
 const auto& t=s.theme;studio_card(c,16,102,608,88,t,s.studioRow==0);
 c.draw_label("Performance effect",32,115,t.textParam);
 label(c,studio_value(s,0),32,145,t.textValue,560,18);
 studio_card(c,16,204,608,204,t,s.studioRow==1);
 c.draw_label(s.studioAUsed?"PERFORMING":"Hold A to perform",32,222,s.studioAUsed?t.rowCursor:t.textValue,18);
 scope(c,33,260,574,85,s.waveform,WAVEFORM_SIZE,t);
 c.draw_label("Release A to return to the mix",32,375,t.textParam);
}
} // namespace
void draw_studio(Canvas& c,const AppState& s){
 c.set_surface_style(s.theme.surfaceStyle);heading(c,s);const auto& t=s.theme;
 if(s.studioHelp){
  studio_card(c,16,102,608,306,t);
  if(s.studioTab==0){
   c.draw_label("Live synthesis & audition",32,124,t.textValue,18);
   const char* lines[]={"R + Select opens Studio directly.",
    "A linked synth changes immediately, including during song playback.",
    "On a sample slot, load and confirm once to link the new synth.",
    "Hold L to hear the root note. A + arrows edits the selected control.",
    "R + Left / Right: parameter pages. B + Left / Right: Studio pages.",
    "Select closes help; B returns to the page."};
   for(int i=0;i<6;++i)label(c,lines[i],32,180+i*32,t.textParam,570);
  }else{
  const char* help[]={"Choose an engine, shape its macros, then load the instrument.","Process the mix through Mutable effects. Presets apply with A.","Shape dynamics, saturation, stereo width and output level.","Hold A for the selected performance effect. Release to return.","Choose a microphone or USB input. Stop, then import the WAV.","Assign samples, generate a groove, preview, then apply.","Choose a complete design and its background material."};
  label(c,help[std::clamp(s.studioTab,0,6)],32,124,t.textValue,570,18);
  const char* lines[]={"B + Left / Right changes the page.","Up / Down selects. Hold A + D-pad to edit.","A applies actions and presets. Start controls playback.","Select an available microphone or USB audio input.","Select closes help; B returns to the page."};
  for(int i=0;i<5;++i)c.draw_label(lines[i],32,197+i*34,t.textParam);
  }
 }else if(s.studioTab==0)synth_page(c,s);
 else if(s.studioTab==1||s.studioTab==2)list_page(c,s);
 else if(s.studioTab==3)punch_page(c,s);
 else if(s.studioTab==4)input_page(c,s);
 else if(s.studioTab==5){drumlab_draw(c,s.drumLab,t,*s.project);return;}
 else if(s.studioTab==6)style_page(c,s);
 else if(s.studioTab==7){source_sampler_draw(c,s.sourceSampler,t,*s.project);return;}
 // Drumlab draws its own grid between the shared heading and footer.
 c.fill_rect(18,424,606,1,with_alpha(t.textParam,.2f));
 const std::string hint=s.studioTab==0?"B + Left / Right: Studio pages     Select: Help":"B + Left / Right: Page     A + D-pad: Edit     Select: Help";
 label(c,s.studioMessage.empty()?hint:s.studioMessage,22,435,s.studioMessage.empty()?t.textParam:t.rowCursor,590);
 const auto controls=s.studioTab==0?"A + arrows: Edit     Hold L: Audition     Start: Playback     B: Back":s.studioTab==4?"A: Record / Stop / Import     B: Back":"A: Apply     Start: Playback     B: Back";
 label(c,controls,22,458,t.textEmpty,590);
}
} // namespace pt::ui
