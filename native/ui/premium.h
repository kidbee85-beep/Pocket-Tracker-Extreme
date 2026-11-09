#pragma once
#include "ui/helpers.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pt::ui::premium {
inline Argb mix(Argb a,Argb b,float f){
 f=std::clamp(f,0.f,1.f);Argb out=0xFF000000u;
 for(int shift:{0,8,16})out|=static_cast<Argb>(((a>>shift)&255)*(1-f)+((b>>shift)&255)*f)<<shift;
 return out;
}
inline void card(Canvas& c,int x,int y,int w,int h,const Theme& t,bool focus=false){
 c.panel_rect(x,y+2,w,h,0x28000000,0x28000000,7);
 c.panel_rect(x,y,w,h,t.rowEvery4th,focus?t.rowCursor:with_alpha(t.textParam,.22f),7);
 if(focus)c.fill_rect(x+1,y+8,2,std::max(0,h-16),t.rowCursor);
}
inline void label(Canvas& c,const std::string& text,int x,int y,Argb ink,int width,int size=12){
 c.draw_label(Canvas::clip_label(text,width,size),x,y,ink,size);
}
inline void chip(Canvas& c,const std::string& text,int x,int y,int w,const Theme& t,bool active=false){
 c.panel_rect(x,y,w,23,active?mix(t.rowEvery4th,t.rowCursor,.16f):darken(t.rowEvery4th,.8f),
              with_alpha(active?t.rowCursor:t.textParam,active?.75f:.2f),4);
 label(c,text,x+(w-Canvas::label_width(text,12))/2,y+4,active?t.rowCursor:t.textParam,w-8);
}
inline void disc(Canvas& c,int cx,int cy,int r,Argb base){
 for(int y=-r;y<=r;++y){
  const int half=static_cast<int>(std::sqrt(std::max(0,r*r-y*y)));
  const float f=(y+r)/float(std::max(1,2*r));
  c.fill_rect(cx-half,cy+y,2*half+1,1,mix(base,darken(base,.65f),f));
 }
}
inline void knob(Canvas& c,int cx,int cy,int value,const Theme& t,bool focus=false,int radius=25){
 value=std::clamp(value,0,255);
 const float pi=3.14159265359f;
 for(int i=0;i<=32;++i){const float a=(135+270*i/32.f)*pi/180;
  const int r=radius+8;const int x=cx+static_cast<int>(r*std::cos(a)),y=cy+static_cast<int>(r*std::sin(a));
  c.fill_rect(x,y,2,2,i*255/32<=value?(focus?t.rowCursor:with_alpha(t.rowCursor,.7f)):with_alpha(t.textParam,.20f));
 }
 disc(c,cx,cy+2,radius+1,darken(t.rowEvery4th,.40f));
 disc(c,cx,cy,radius,mix(t.rowEvery4th,t.textParam,.25f));
 disc(c,cx,cy,radius-3,t.rowEvery4th);
 const float angle=(135+270*value/255.f)*pi/180;
 c.line(cx+static_cast<int>(std::cos(angle)*8),cy+static_cast<int>(std::sin(angle)*8),
        cx+static_cast<int>(std::cos(angle)*(radius-5)),cy+static_cast<int>(std::sin(angle)*(radius-5)),
        focus?t.rowCursor:t.textValue);
 c.fill_rect(cx-1,cy-1,3,3,with_alpha(t.textParam,.4f));
}
inline void percent(Canvas& c,int value,int cx,int y,const Theme& t,bool focus=false){
 const auto text=std::to_string(static_cast<int>(std::lround(value*100/255.f)))+"%";
 c.draw_text(text,cx-Canvas::text_width(text,1,3)/2,y,focus?t.rowCursor:t.textValue,1,3);
}
inline void row(Canvas& c,int x,int y,int w,const std::string& name,const std::string& value,const Theme& t,bool focus){
 c.panel_rect(x,y,w,28,focus?mix(t.rowEvery4th,t.rowCursor,.10f):t.rowEvery4th,
              focus?t.rowCursor:with_alpha(t.textParam,.10f),4);
 if(focus)c.fill_rect(x+1,y+5,2,18,t.rowCursor);
 label(c,name,x+11,y+7,focus?t.textValue:t.textParam,w/2-17);
 label(c,value,x+w/2,y+6,focus?t.rowCursor:t.textValue,w/2-10);
}
inline void scope(Canvas& c,int x,int y,int w,int h,const float* samples,int count,const Theme& t){
 c.fill_rect(x,y,w,h,darken(t.vizBackground,.8f));
 for(int i=1;i<4;++i)c.fill_rect(x+i*w/4,y,1,h,with_alpha(t.textParam,.09f));
 c.fill_rect(x,y+h/2,w,1,with_alpha(t.textParam,.22f));
 if(!samples||count<1)return;
 int last=y+h/2;
 for(int i=0;i<w;++i){
  const float sample=std::isfinite(samples[i*count/w])?samples[i*count/w]:0.f;
  const int next=y+h/2-static_cast<int>(std::clamp(sample,-1.f,1.f)*(h/2-2));
  if(i)c.line(x+i-1,last,x+i,next,t.vizWave);last=next;
 }
}
inline void meter(Canvas& c,int x,int y,int w,int h,float peak,const Theme& t){
 if(!std::isfinite(peak))peak=0;
 c.fill_rect(x,y,w,h,darken(t.rowEvery4th,.6f));
 const float db=20.f*std::log10(std::max(peak,.001f));
 const int filled=static_cast<int>(std::clamp((db+60)/60,0.f,1.f)*w);
 for(int i=0;i<filled;i+=4)c.fill_rect(x+i,y,std::min(3,filled-i),h,i>w*.9f?t.meterHigh:i>w*.7f?t.meterMid:t.meterLow);
}
} // namespace pt::ui::premium
