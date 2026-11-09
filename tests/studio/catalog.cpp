#include "ui/layout.h"
#include "ui/studio.h"
#include "ui/theme_io.h"
#include "studio/synth.h"
#include "studio/synth_params.h"
#include "studio/rack.h"
#include "effects/modules/reverb-presets.h"
#include "effects/modules/delay-presets.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <set>

using namespace pt::ui;
void snapshot(const std::filesystem::path& path, AppState& state) {
    Canvas canvas; TrackerLayout layout; layout.draw(canvas, state);
    std::ofstream file(path, std::ios::binary); file << "P6\n640 480\n255\n";
    for (int i=0;i<640*480;++i) {
        auto p=canvas.pixels()[i];
        const char rgb[]={static_cast<char>(p>>16),static_cast<char>(p>>8),static_cast<char>(p)};
        file.write(rgb,3);
    }
}
int main(int argc,char** argv) {
    assert(argc==2);const std::filesystem::path folder(argv[1]);
    std::filesystem::create_directories(folder);
    songcore::json catalog;catalog["version"]="0.1.0.6";
    catalog["synth_models"]=studio::model_count();
    for(int model=0;model<studio::model_count();++model){
        songcore::json controls;controls["model"]=model;controls["name"]=studio::model_name(model);
        controls["macros"]=songcore::json::array();controls["engine_parameters"]=songcore::json::array();
        for(int i=0;i<studio::macro_count(model);++i)controls["macros"].push_back(studio::macro_name(model,i));
        for(int i=0;i<studio::synth_param_count(model);++i)controls["engine_parameters"].push_back({{"name",studio::synth_param_name(model,i)},{"default",studio::synth_param_default(model,i)}});
        catalog["synth_controls"].push_back(controls);
    }
    for(int model=0;model<studio::model_count();++model)
        for(int variant=0;variant<studio::variation_count();++variant) {
            const int code=studio::preset_code(model,variant);
            catalog["synth_presets"].push_back({{"id",code},{"name",studio::preset_name(code)},
                {"source",studio::uri(code)},{"macros",studio::preset_macros(code)}});
        }
    for(int i=0;i<studio::effect_preset_count();++i)
        catalog["mutable_effect_presets"].push_back({{"name",studio::effect_preset_name(i)},
            {"values",studio::effect_preset(i)}});
    for(int i=0;i<studio::master_preset_count();++i)
        catalog["master_presets"].push_back({{"name",studio::master_preset_name(i)},
            {"values",studio::master_preset(i)}});
    for(const auto& p:kReverbPresets) {
        assert(reverb_preset_match(p.algo,p.decay,p.room,p.damp,p.pre,p.width,p.mod)>=0);
        catalog["reverb_presets"].push_back(p.name);
    }
    for(const auto& p:kDelayPresets) {
        assert(delay_preset_match(p.pong,p.sync,p.time,p.feedback,p.tone,p.wobble,p.reverbSend)>=0);
        if(p.sync)assert(p.time>=0&&p.time<12);
        catalog["delay_presets"].push_back(p.name);
    }
    auto project=songcore::make_default_project();project.name="STUDIO SESSION";
    AppState state;state.project=&project;state.currentScreen=ScreenType::SONG;open_studio_synth(state,project.instruments[0]);state.studioOpen=false;
    std::set<std::string> names;
    for(const auto& t:theme_builtins()) {
        assert(names.insert(t.name).second);Theme loaded;
        assert(parse_theme(serialize_theme(t),loaded));
        assert(loaded.rowCursor==t.rowCursor&&loaded.background==t.background);
        assert(loaded.surfaceStyle==t.surfaceStyle);
        catalog["themes"].push_back(t.name);state.theme=t;
        catalog["theme_materials"][t.name]=surface_style_name(t.surfaceStyle);
        snapshot(folder/("theme-"+t.name+".ppm"),state);
        state.studioOpen=true;state.studioModel=94;state.studioRow=4;
        snapshot(folder/("design-"+t.name+".ppm"),state);state.studioOpen=false;
    }
    state.theme=theme_studio();state.currentScreen=ScreenType::PROJECT;
    snapshot(folder/"project.ppm",state);
    state.studioOpen=true;
    for(int tab=0;tab<4;++tab) {state.studioTab=tab;state.studioRow=0;snapshot(folder/("studio-"+std::to_string(tab)+".ppm"),state);}
    std::ofstream(folder/"preset-catalog.json")<<catalog.dump(2)<<'\n';
    std::puts("PASS: theme persistence, preset catalog and rendered design previews");
}
