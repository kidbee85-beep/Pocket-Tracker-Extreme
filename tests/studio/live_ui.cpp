#include "ui/studio.h"
#include "studio/synth.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void snapshot(const std::filesystem::path& path,const pt::ui::AppState& state){
    pt::ui::Canvas canvas;pt::ui::draw_studio(canvas,state);
    std::ofstream file(path,std::ios::binary);file<<"P6\n640 480\n255\n";
    for(int i=0;i<640*480;++i){const auto pixel=canvas.pixels()[i];
        const char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};file.write(rgb,3);
    }
    assert(file.good());
}
}

// Render actual production views with explicit preview state. Audio behavior is exercised by
// the live DSP/control checks; this exports the screens for visual inspection at device size.
int main(int argc,char** argv){
    using namespace pt::ui;
    assert(argc==2);const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    auto project=songcore::make_default_project();project.name="UI PREVIEW";
    AppState state;state.project=&project;state.theme=theme_studio();state.studioOpen=true;
    size_t views=0;
    for(const int model:{8,25,72,78,94,102}){
        auto& instrument=project.instruments[0];
        instrument=songcore::Instrument(0);instrument.sampleFilePath=studio::uri(studio::preset_code(model,0));
        instrument.volume=170;instrument.loopMode="fwd";
        auto& env=instrument.modSlots[0];env.type=songcore::ModType::ADSR;env.dest=songcore::ModDest::VOLUME;
        env.attack=3;env.decay=60;env.sustain=180;env.release=45;
        open_studio_synth(state,instrument);
        const int pages=studio_synth_page_count(state);assert(pages>0&&pages<32);
        for(int page=0;page<pages;++page){
            state.studioSynthPage=page;state.studioRow=3;
            const int count=studio_synth_control_count(state);assert(count>=0&&count<=4);
            assert(studio_synth_apply_row(state)>=3+count);
            snapshot(out/("model-"+std::to_string(model)+"-page-"+std::to_string(page)+".ppm"),state);++views;
        }
    }
    project.instruments[0]=songcore::Instrument(0);
    open_studio_synth(state,project.instruments[0]);state.studioSynthPage=0;state.studioRow=3;
    snapshot(out/"draft.ppm",state);
    state.studioAuditionHeld=true;snapshot(out/"draft-audition.ppm",state);
    state.studioAuditionHeld=false;state.studioConfirm=true;snapshot(out/"replace-confirm.ppm",state);
    state.studioHelp=true;snapshot(out/"live-help.ppm",state);
    std::cout<<"PASS: "<<views<<" actual engine/control-page layouts plus draft, audition, confirmation and help previews\n";
}
