// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/local_sample_range.h"
#include "ui/brand_graphics.h"
#include "ui/helpers.h"
#include "ui/product_brand.h"
#include <algorithm>
#include <array>
#include <cstdio>

namespace pt::ui {
std::string local_sample_time(int64_t frame,int rate){
    const int64_t ms=rate>0?std::max<int64_t>(0,frame)*1000/rate:0;
    char text[40];std::snprintf(text,sizeof(text),"%lld:%02lld.%03lld",
        static_cast<long long>(ms/60000),static_cast<long long>((ms/1000)%60),static_cast<long long>(ms%1000));
    return text;
}
void draw_local_sample_range(Canvas& c,const LocalSampleRangeState& v,const Theme& t){
    c.surface_rect(0,0,640,480,t.background);
    c.draw_label(kProductName,18,16,t.textValue,18);
    brand::mark(c,202,12,30,t.rowCursor,t.textTitle);
    c.draw_label("LONG SAMPLE / CHOP SOURCE",254,20,t.rowCursor,12);
    c.fill_rect(18,47,604,1,with_alpha(t.textParam,.3f));
    c.draw_label(Canvas::clip_label(v.title,604,18),18,56,t.textValue,18);
    char metadata[160];
    std::snprintf(metadata,sizeof(metadata),"%.1f MiB / %d Hz / %d-bit %s / %s",
        double(v.source.dataBytes)/(1024.*1024.),v.source.rate,v.source.bits,
        v.source.isFloat?"float":v.source.channels==2?"stereo":"mono",
        local_sample_time(v.source.totalFrames,v.source.rate).c_str());
    c.draw_label(Canvas::clip_label(metadata,604,12),18,83,t.textParam,12);
    if(v.help){
        c.panel_rect(18,110,604,286,t.rowEvery4th,with_alpha(t.rowCursor,.6f),5);
        static const std::array<const char*,10> lines={{
            "Long WAVs play from storage; the full recording stays on the card.",
            "Up / Down selects a control. Left / Right adjusts its value.",
            "Position: move through the full file; Start plays or pauses.",
            "A on IN / OUT marks the current playback position.",
            "A + Left / Right adjusts markers by 10 ms; A + Up / Down: 10 s.",
            "R + Left / Right seeks 5 s without moving the selected control.",
            "Quick length sets an excerpt from IN, within this device's RAM.",
            "Crop to editor saves a NEW WAV, then opens it for slicing.",
            "The source file is never replaced. Whole-file load needs more RAM.",
            "B or Select closes this guide and returns to your range."
        }};
        c.draw_label("LOCAL WAV / QUICK GUIDE",32,126,t.rowCursor,18);
        for(size_t i=0;i<lines.size();++i)c.draw_label(Canvas::clip_label(lines[i],576,12),32,160+int(i)*23,t.textValue,12);
    }else{
        c.panel_rect(18,109,604,77,t.rowEvery4th,with_alpha(t.textParam,.35f),5);
        c.draw_label(v.source.playing?"PLAYING FROM CARD":"CARD PREVIEW / PAUSED",30,120,t.rowCursor,12);
        c.draw_label(local_sample_time(v.source.positionFrame,v.source.rate),465,117,t.textValue,18);
        constexpr int left=30,width=580,y=154;
        const auto point=[&](int64_t frame){return left+int(double(std::clamp<int64_t>(frame,0,v.source.totalFrames))*width/std::max<int64_t>(1,v.source.totalFrames));};
        c.fill_rect(left,y,width,3,with_alpha(t.textParam,.3f));
        const int in=point(v.inFrame),out=point(v.outFrame),pos=point(v.source.positionFrame);
        if(out>in)c.fill_rect(in,y-3,out-in,9,with_alpha(t.rowCursor,.35f));
        c.fill_rect(in,y-5,2,14,t.rowCursor);c.fill_rect(out-1,y-5,2,14,t.textTitle);
        c.fill_rect(pos,y-8,1,19,t.textValue);
        c.draw_label("IN / OUT",30,168,t.textParam,12);
        c.draw_label(local_sample_time(v.inFrame,v.source.rate)+" - "+local_sample_time(v.outFrame,v.source.rate),378,168,t.textValue,12);
        const int64_t length=std::max<int64_t>(0,v.outFrame-v.inFrame);
        const std::array<std::string,7> values={{
            local_sample_time(v.source.positionFrame,v.source.rate),
            local_sample_time(v.inFrame,v.source.rate),local_sample_time(v.outFrame,v.source.rate),
            local_sample_time(length,v.source.rate),
            v.source.playing?"PAUSE":"PLAY FROM POSITION",
            "NEW WAV / "+local_sample_time(length,v.source.rate),
            "FULL FILE / RAM CHECK"
        }};
        constexpr const char* labels[]={"Position","IN marker","OUT marker","Quick length","Preview","Crop to editor","Load whole file"};
        for(int row=0;row<7;++row){
            const bool selected=v.row==row;const int top=195+row*28;
            c.panel_rect(18,top,604,25,selected?t.rowCursor:t.rowEvery4th,with_alpha(t.textParam,.22f),3);
            const Argb ink=selected?t.background:t.textValue;
            c.draw_label(labels[row],30,top+5,ink,12);
            c.draw_label(Canvas::clip_label(values[row],335,12),275,top+5,ink,12);
        }
    }
    c.draw_label(Canvas::clip_label("Editor excerpt limit here: "+local_sample_time(v.maxImportFrames,v.source.rate)+" / full-file preview stays available",604,12),18,405,t.textParam,12);
    const auto message=v.message.empty()?"Choose a section, crop it, then use the normal Sample Editor.":v.message;
    c.draw_label(Canvas::clip_label(message,604,12),18,429,v.source.error.empty()?t.textTitle:t.meterHigh,12);
    c.fill_rect(18,452,604,1,with_alpha(t.textParam,.25f));
    c.draw_label("Start: Preview   A: Mark / Choose   Select: Guide   B: Back",18,463,t.textValue,12);
}
}
