// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/canvas.h"
#include "ui/helpers.h"
#include <algorithm>

namespace pt::ui::brand {

// These are gesture diagrams, not fabricated output meters. Their changing phase is decorative;
// only the separate output scope and sequencer cursor use engine readback. Integer geometry,
// fixed-size loops and a stack clip keep the renderer allocation-free and independent of DSP.
inline void punch_gesture(Canvas& canvas,int bank,int id,int x,int y,int width,int height,
                          int phaseMs,bool held,Argb ink) {
    if(width<4||height<4)return;
    Canvas::ClipScope clip(canvas,x,y,width,height);
    const int tick=held?std::clamp(phaseMs,0,999)*32/1000:0;
    const Argb dim=with_alpha(ink,.22f),middle=with_alpha(ink,.54f),ghost=with_alpha(ink,.10f);
    const auto px=[&](int a){return x+a*(width-1)/96;};
    const auto py=[&](int a){return y+a*(height-1)/24;};
    const auto line=[&](int a,int b,int c,int d,Argb color){
        canvas.line(px(a),py(b),px(c),py(d),color);
        if(held&&height>=24&&(color==ink||color==middle))
            canvas.line(px(a),py(b)+1,px(c),py(d)+1,color);
    };
    const auto dot=[&](int a,int b,int w,int h,Argb color){canvas.fill_rect(px(a),py(b),std::max(1,w*width/96),std::max(1,h*height/24),color);};
    const auto frame=[&](int a,int b,int w,int h,Argb color){canvas.stroke_rect(px(a),py(b),std::max(2,w*width/96),std::max(2,h*height/24),color);};
    static constexpr int sine[]={0,2,5,7,8,10,11,12,12,12,11,10,8,7,5,2,0,-2,-5,-7,-8,-10,-11,-12,-12,-12,-11,-10,-8,-7,-5,-2};
    const auto wave=[&](int phase,int amplitude){return sine[phase&31]*amplitude/12;};
    if(height>=24){
        // An engraved, bank-specific stage behind the gesture: reel perforations, a log grid,
        // a wire room, packet buses, grain constellations or broken bit rails. Quiet when idle.
        const Argb stage=with_alpha(ink,held?.17f:.09f);
        if(bank==1){
            for(int i=0;i<12;++i){const int p=(i*9+tick)%100;dot(p,0,4,1,stage);dot(96-p,23,4,1,stage);}
        }else if(bank==2){
            for(int p:{2,10,22,40,65,94})line(p,0,p,24,stage);
            line(0,5,96,5,ghost);line(0,19,96,19,ghost);
        }else if(bank==3){
            for(int p=0;p<=96;p+=16)line(48,9,p,24,stage);
            for(int i=0;i<4;++i)line(0,13+i*3,96,13+i*3,stage);
        }else if(bank==4){
            for(int i=0;i<6;++i){const int p=(i*19+tick*2)%96;line(p,0,p,3,stage);line(p,21,p+8,21,stage);}
            line(0,1,96,1,ghost);line(0,23,96,23,ghost);
        }else if(bank==5){
            int previousX=0,previousY=12;
            for(int i=0;i<10;++i){
                const int p=(i*29+id*3+tick/2)%94,q=2+(i*11+id)%20;
                if(i%3)line(previousX,previousY,p,q,ghost);
                dot(p,q,i%3?1:2,1,stage);previousX=p;previousY=q;
            }
        }else if(bank==7){
            for(int i=0;i<16;++i){const int bits=(i*13+id*7)^(tick/2);dot(i*6,1+(bits&1),3,1,stage);dot(i*6,22-((bits>>1)&1),3,1,stage);}
        }else{
            for(int i=0;i<13;++i){dot(i*8,1,1,1,stage);dot(i*8,23,1,1,stage);}
            for(int i=0;i<4;++i)line(i*24,0,i*24,24,ghost);
        }
    }
    line(0,12,96,12,ghost);
    for(int i=0;i<5;++i)dot(i*24,22,1,2,dim);

    if(bank==0){
        if(id==1||id==2||id==23||id==25){
            const int cells=id==1?4:id==2?8:id==25?6:12;
            const int cursor=(tick*cells/32)%cells;
            for(int i=0;i<cells;++i){
                const int left=i*96/cells,w=std::max(3,96/cells-3);
                frame(left,3,w,16,held&&i==cursor?ink:dim);
                line(left+2,15,left+w/3,6,held&&i==cursor?ink:middle);
                line(left+w/3,6,left+w-1,14,held&&i==cursor?ink:middle);
            }
            if(held)line(cursor*96/cells,20,(cursor+1)*96/cells-3,20,ink);
        }else if(id==7||id==26){
            for(int i=0;i<8;++i){
                const int left=i*12,open=id==26?(i%2?4:8):6;
                const Argb color=held&&i==tick/4?ink:middle;
                line(left,18,left,5,color);line(left,5,left+open,5,color);
                line(left+open,5,left+open,18,color);line(left+open,18,left+12,18,dim);
            }
        }else{
            int last=id==22?20:12;
            for(int i=1;i<=24;++i){
                const int next=id==22?20-((i*4+tick*3)%96)*16/96:12-wave(i*2+tick,8);
                line((i-1)*4,last,i*4,next,i%4?middle:ink);last=next;
            }
        }
    }else if(bank==1){
        // Two reel rings and a directional tape path: slow, reverse and wobble stay distinct.
        for(int reel:{14,82}){
            line(reel-9,5,reel+9,5,dim);line(reel+9,5,reel+12,12,dim);
            line(reel+12,12,reel+9,19,dim);line(reel+9,19,reel-9,19,dim);
            line(reel-9,19,reel-12,12,dim);line(reel-12,12,reel-9,5,dim);
            const int speed=id==9?tick/2:id==10?tick*2:tick;
            line(reel-wave(speed,7),12-wave(speed+8,6),reel+wave(speed,7),12+wave(speed+8,6),middle);
        }
        const bool reverse=id==3||id==11||id==29;
        const int travel=(reverse?31-tick:tick)*38/32;
        line(25,5,71,5,ghost);line(25,19,71,19,dim);
        for(int i=0;i<7;++i){const int p=27+(i*7+travel)%42;line(p,8,p+(id==28?wave(tick+i*3,3):0),16,i%3?middle:ink);}
        const int arrow=reverse?39:57,back=reverse?49:47;
        line(back,8,arrow,12,ink);line(arrow,12,back,16,ink);
        if(id==8||id==27){line(27,21,69,21,dim);dot(id==8?69-tick:27+tick,20,2,3,ink);}
    }else if(bank==2){
        for(int i=1;i<4;++i)line(i*24,2,i*24,21,ghost);
        const int cut=(id==31||id==30||id==34)?28+(wave(tick,16)):48;
        int last=18;
        for(int i=0;i<=24;++i){
            const int p=i*4,dist=std::abs(p-cut);int value=0;
            if(id==4||id==31)value=std::clamp((cut-p+18)/3,0,15);
            else if(id==12)value=std::clamp((p-cut+18)/3,0,15);
            else if(id==13||id==34)value=std::max(0,16-dist/2);
            else if(id==30)value=std::min(15,dist/2);
            else value=(i%3==0?14:4)+(id==32?1:0);
            if(id==31&&dist<8)value=19-dist;
            const int yy=21-value;if(i)line(p-4,last,p,yy,middle);last=yy;
        }
        line(cut,2,cut,22,ink);dot(cut-2,1,5,2,ink);
        if(held)dot(tick*94/32,23,3,1,ink);
        if(id==34){const int second=std::min(92,cut+29);line(second,4,second,19,dim);dot(second-2,7,5,2,middle);}
    }else if(bank==3){
        if(id==39){
            // Spring: a travelling coil, attached at both ends.
            line(1,12,9,12,dim);int previous=12;
            for(int i=1;i<=20;++i){const int yy=12+wave(i*8+tick,8);line(5+i*4-4,previous,5+i*4,yy,i%4?middle:ink);previous=yy;}
            line(85,previous,95,12,dim);
        }else if(id==37){
            for(int i=0;i<4;++i){const int inset=i*8;frame(4+inset,2+i*2,88-inset*2,20-i*4,i==tick/8?ink:dim);}
            line(5,3,43,11,middle);line(91,21,53,13,middle);
        }else{
            for(int i=0;i<5;++i){
                const int left=4+i*18,age=(i+tick/4)%5,hh=std::max(3,18-age*3);
                const int yy=id==35?(i%2?13:2):12-hh/2;
                frame(left,yy,11,hh,age==0?ink:age<3?middle:dim);
                if(i<4)line(left+12,12,left+17,12,dim);
                if(id==38){line(left+2,6,left+5,2,middle);line(left+5,2,left+8,6,middle);}
                if(id==40)dot(left+4,yy+hh/2,3,2,middle);
                if(id==41)line(left+8,yy+2,left+2,yy+hh-2,middle);
            }
        }
    }else if(bank==4){
        if(id==24){
            // DROP OUT still visibly indicates a held gesture while the real output is silent.
            line(2,12,25,12,middle);line(70,12,94,12,middle);
            line(36,4,59,20,ink);line(36,20,59,4,ink);
            const int p=tick*90/32;dot(p,1,5,1,dim);dot(90-p,22,5,1,middle);
        }else{
            static constexpr int order[]={0,5,2,7,1,6,3,4};
            for(int i=0;i<8;++i){
                const int from=id==43?order[(i+tick/4)%8]:(i+tick/4)%8;
                const int top=3+(from%3)*5,hh=4+from%4;
                if(id==47&&i==(tick/4)%8)continue;
                dot(i*12+1,top,9,hh,i==(tick/4)%8?ink:middle);
                if(id==46)dot(i*12+1,22-top,9,2,dim);
                if(id==45)frame(i*12,2,10,18,dim);
            }
            if(id==44){line(0,20,48,5,dim);line(48,5,96,20,dim);}
        }
    }else if(bank==5){
        for(int lane=0;lane<2;++lane){
            const int direction=id==51?-1:1,speed=id==52?3:id==53?2:1;
            for(int i=0;i<6;++i){
                const int position=(i*17+direction*tick*speed+lane*8+256)%90;
                const int length=id==54?3+(i%3):id==55?10:7,top=3+lane*10;
                line(position,top+7,position+length/2,top,i%3?middle:ink);
                line(position+length/2,top,position+length,top+7,i%3?middle:ink);
                if(id==50||id==56)dot(position+2,top+9,3,1,dim);
            }
        }
        if(id==55){line(42,2,42,22,ink);line(54,2,54,22,ink);}
    }else if(bank==6){
        if(id==19||id==20||id==21){
            const int opening=id==20?0:id==21?8:wave(tick,7);
            line(0,4,45,12-opening,middle);line(45,12-opening,95,4,ink);
            line(0,20,45,12+opening,middle);line(45,12+opening,95,20,ink);
            dot(43,10-opening,4,3,ink);dot(43,11+opening,4,3,middle);
            if(held)dot(tick*91/32,11,4,2,ink);
        }else{
            for(int lane=0;lane<2;++lane){
                int last=12+wave(tick+lane*8,7);
                const int frequency=id==16?(lane?2:5):id==59?3:2;
                for(int i=1;i<=24;++i){const int yy=12+wave(i*frequency+tick+lane*(id==57?6:8),7);line((i-1)*4,last,i*4,yy,lane?dim:middle);last=yy;}
            }
            const int p=(tick*3)%94;line(p,2,p,22,ink);
            if(id==58)for(int i=0;i<8;++i)line(i*12,19,i*12+5,19,dim);
        }
    }else{
        int previous=12;
        for(int i=1;i<=24;++i){
            const int raw=wave(i*3+tick,10);int yy=12;
            if(id==5)yy=12-(raw/4)*4;
            else if(id==60)yy=12-(wave((i/3)*9+tick,10)/4)*4;
            else if(id==15)yy=12-wave(i*7+tick,10);
            else if(id==61)yy=20-std::abs(raw)*16/10;
            else if(id==62)yy=12-std::clamp(raw*2,-7,7);
            else if(id==63)yy=3+((i*11+tick*7)^(i*3))%18;
            else yy=12-std::clamp(raw*(id==64?2:3),-5,9);
            line((i-1)*4,previous,i*4,yy,i%4?middle:ink);previous=yy;
        }
        line(0,4,96,4,dim);line(0,20,96,20,dim);
        const int p=(tick*3)%90;dot(p,0,6,1,ink);
    }
}
} // namespace pt::ui::brand
