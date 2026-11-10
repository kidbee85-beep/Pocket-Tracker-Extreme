// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/canvas.h"
#include "ui/helpers.h"
#include "ui/trackwerk_art.h"
#include <algorithm>

namespace pt::ui::brand {

// The same original stacked TW silhouette as the vector app-icon study.
inline void mark(Canvas& c,int x,int y,int size,Argb ink,Argb secondary) {
    const int m=std::max(12,size);
    for(int layer=0;layer<2;++layer)for(int row=0;row<32;++row){
        const auto bits=art::markLayers[layer][row];
        const int top=y+row*m/32,height=(row+1)*m/32-row*m/32;
        if(!height)continue;
        for(int col=0;col<32;){
            if(!(bits&(uint32_t(1)<<(31-col)))){++col;continue;}
            int end=col+1;while(end<32&&(bits&(uint32_t(1)<<(31-end))))++end;
            c.fill_rect(x+col*m/32,top,end*m/32-col*m/32,height,layer?secondary:ink);col=end;
        }
    }
}

inline bool expressive_material(const Theme& t) { return t.surfaceStyle>=12; }

// A quiet scanner on the chrome, never painted through text. Motion is decorative; the live
// waveform and capture meters remain the only audio activity indicators.
inline void registration(Canvas& c,int x,int y,int width,const Theme& t,int phaseMs,bool animate) {
    c.fill_rect(x,y,width,1,with_alpha(t.textParam,.18f));
    for(int i=0;i<5;++i)c.fill_rect(x+i*(width-1)/4,y-2,1,5,with_alpha(t.textParam,.24f));
    const int phase=std::clamp(phaseMs,0,999),offset=phase*(width-8)/1000;
    c.fill_rect(x+(animate?offset:0),y,8,1,with_alpha(t.rowCursor,.75f));
}

// Compact bank-specific vector movement, intentionally separate from the true output scope.
// There are at most 24 line segments; the caller draws it only on the one held pad.
inline void punch_motion(Canvas& c,int bank,int pad,int x,int y,int w,int h,int phaseMs,Argb ink) {
    Canvas::ClipScope clip(c,x,y,w,h);
    const int tick=std::clamp(phaseMs,0,999)*24/1000,mid=y+h/2;
    const auto faint=with_alpha(ink,.23f);
    for(int i=0;i<12;++i){
        const int xx=x+i*w/12;
        if(bank==0||bank==4){
            const int height=3+((i+tick+pad)%4)*3;
            c.fill_rect(xx,mid-height/2,std::max(2,w/12-2),height,(i+tick)%4==0?ink:faint);
        }else if(bank==1){
            const int shift=(tick*2+i*7)%std::max(w,1);
            c.line(x+shift,y+2,x+shift-5,y+h-3,i%3?faint:ink);
        }else if(bank==2){
            const int top=y+2+((i+tick/2)%8);
            c.line(xx,top,xx+w/12,top+(i<6?-1:1),i%3?faint:ink);
        }else if(bank==3){
            const int age=(i+tick)%12,height=std::max(2,h-age);
            c.stroke_rect(xx,mid-height/2,std::max(2,w/12-2),height,with_alpha(ink,1.f-age/14.f));
        }else if(bank==5){
            c.fill_rect(x+(i*17+tick*2)%std::max(1,w-4),y+2+(i*7+tick)%std::max(1,h-5),2+i%3,2,i%3?faint:ink);
        }else if(bank==6){
            const int wave=(i+tick)%12,yy=mid+(wave<6?wave-3:9-wave);
            c.line(xx,yy,xx+w/12,yy+(wave<6?2:-2),i%3?faint:ink);
        }else{
            const int yy=y+3+(i*7+tick/3)%std::max(1,h-5);
            c.line(xx,mid,xx+w/12,yy,i%2?ink:faint);
        }
    }
}
} // namespace pt::ui::brand
