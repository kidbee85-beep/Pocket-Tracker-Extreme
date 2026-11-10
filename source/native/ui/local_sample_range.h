// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "common/local_wav_source.h"
#include "ui/canvas.h"
#include <cstdint>
#include <string>

namespace pt::ui {
// A transient browser layer. The original file and project are untouched until import succeeds.
struct LocalSampleRangeState {
    bool isOpen=false,help=false;
    int row=0,targetInstrument=0;
    int64_t inFrame=0,outFrame=0,maxImportFrames=0;
    std::string path,title,message,previousAutoName;
    pt::LocalWavSnapshot source;
};
std::string local_sample_time(int64_t frame,int rate);
void draw_local_sample_range(Canvas&,const LocalSampleRangeState&,const Theme&);
}
