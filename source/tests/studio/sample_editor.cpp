#include "ui/modules/sample_editor.h"
#include "ui/input_dispatcher.h"
#include "ui/std_filesystem.h"
#include "songcore/project_io.h"
#include "audio-engine.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <climits>
#include <cstdio>
using namespace pt::ui;

static void shot(const std::filesystem::path& path,const SampleEditorState& s) {
    Canvas c;SampleEditorModule module;module.draw(c,0,0,s,theme_studio());
    std::ofstream f(path,std::ios::binary);f<<"P6\n640 480\n255\n";
    for(int i=0;i<640*480;++i){const auto p=c.pixels()[i];char rgb[]={char(p>>16),char(p>>8),char(p)};f.write(rgb,3);}
}

int main(int argc,char** argv) {
    assert(argc==2);std::filesystem::create_directories(argv[1]);
    SampleEditorState s;
    s.totalFrames=48000000;s.sampleRate=48000;s.selectionStart=12345678;s.selectionEnd=12345978;
    s.set_zoom(100);assert(s.zoomLevel==s.max_zoom());assert(s.max_zoom()>4);
    assert(s.visible_frames()<=128);assert(s.fine_step()==1);
    assert(s.view_start()>=0&&s.view_end()<=s.totalFrames&&s.view_end()>s.view_start());
    s.fit_selection(false);assert(s.view_start()<=s.selectionStart&&s.view_end()>=s.selectionEnd);
    const auto fitted=s.view_start();s.playbackPosition=.9f;
    assert(s.view_start()==fitted); // a fitted region stays fixed during audition
    s.set_zoom(0);assert(s.viewCenter==-1&&s.view_start()==0&&s.view_end()==s.totalFrames);
    s.set_zoom(-99);assert(s.zoomLevel==0);
    s.totalFrames=INT_MAX;s.selectionStart=INT_MAX-800;s.selectionEnd=INT_MAX-400;s.playbackPosition=-1;
    s.fit_selection(false);assert(s.view_start()<=s.selectionStart&&s.view_end()>=s.selectionEnd);
    s.set_zoom(s.max_zoom());assert(s.fine_step()==1);assert(s.view_end()<=INT_MAX);
    s.totalFrames=0;s.fit_selection(false);assert(s.view_start()==0&&s.view_end()==0);

    s=SampleEditorState{};s.totalFrames=192000;s.sampleRate=48000;
    s.sampleName="Drum break - sample laboratory";
    s.sliceMethod=SampleEditorModule::SLICE_DIVIDE;s.sliceDivisions=16;s.sliceIndex=7;
    s.selectionStart=84000;s.selectionEnd=96000;s.cursorRow=11;s.cursorCol=1;
    s.fit_selection(true);assert(s.view_start()<=84000&&s.view_end()>=96000);
    s.waveformData.resize(1240);s.overviewData.resize(1240);
    for(int i=0;i<620;++i){
        const float pulse=.1f+.8f*std::exp(-float(i%78)/18.f);
        s.waveformData[i*2]=-.8f*pulse;s.waveformData[i*2+1]=pulse;
        s.overviewData[i*2]=-.8f*pulse;s.overviewData[i*2+1]=pulse;
    }
    shot(std::filesystem::path(argv[1])/"sample-fit.ppm",s);
    s.set_zoom(0);s.cursorRow=9;s.cursorCol=3;s.snapEnabled=false;
    shot(std::filesystem::path(argv[1])/"sample-overview.ppm",s);
    s.set_zoom(s.max_zoom());s.waveformGain=2;s.selectionStart=84000;s.selectionEnd=84032;
    for(int i=0;i<620;++i){const float v=.2f*std::sin(i*.04f);s.waveformData[i*2]=v;s.waveformData[i*2+1]=v;}
    s.cursorRow=8;s.cursorCol=0;assert(s.fine_step()==1);
    shot(std::filesystem::path(argv[1])/"sample-detail.ppm",s);
    SampleEditorModule m;s.cursorRow=1;s.cursorCol=0;
    m.handle_input(s,{ActionType::SET_VALUE,99});assert(s.zoomLevel==s.max_zoom());
    assert(SampleEditorModule::row_below(8)==9&&SampleEditorModule::row_above(10)==9);
    assert(SampleEditorModule::row_below(14)==15&&SampleEditorModule::row_above(16)==15);
    assert(SampleEditorModule::max_col_for_row(9)==4&&SampleEditorModule::max_col_for_row(15)==2);
    // Exercise the actual dispatcher, including removing the last computed divide boundary.
    auto engine=std::make_unique<AudioEngine>();songcore::SongcoreHost host(engine.get(),48000);
    assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
    AppState app;app.project=&host.edit_project();app.currentScreen=ScreenType::SAMPLE_EDITOR;
    StdFileSystem fs(argv[1]);InputDispatcher dispatch(app,host,fs);
    auto& edit=app.sampleEditor;edit.totalFrames=192000;edit.sampleRate=48000;
    edit.sliceMethod=SampleEditorModule::SLICE_MANUAL;edit.fileMarkers={24000,48000};
    edit.selectionStart=96000;edit.selectionEnd=144000;edit.cursorRow=12;edit.cursorCol=0;
    dispatch.on_button_a();assert(edit.marker_count()==3&&edit.marker_position(2)==96000);
    assert(edit.isModified&&edit.selectionStart==96000);
    edit.cursorCol=1;dispatch.on_button_a();assert(edit.marker_count()==2&&edit.selectionStart==48000);
    edit=SampleEditorState{};edit.totalFrames=192000;edit.sliceMethod=SampleEditorModule::SLICE_DIVIDE;
    edit.sliceDivisions=2;edit.sliceIndex=1;edit.cursorRow=12;edit.cursorCol=1;
    dispatch.on_button_a();assert(edit.sliceMethod==SampleEditorModule::SLICE_MANUAL&&edit.marker_count()==0);
    int64_t begin=0,end=0;edit.slice_bounds(0,begin,end);assert(begin==0&&end==192000);
    edit.cursorRow=9;edit.cursorCol=3;dispatch.on_button_a();assert(!edit.snapEnabled);
    edit.cursorCol=4;dispatch.on_button_a();assert(edit.waveformGain==1);
    edit=SampleEditorState{};edit.totalFrames=192000;edit.sliceMethod=SampleEditorModule::SLICE_MANUAL;
    for(int i=1;i<=songcore::PROGRAM_SLICE_MARKERS;++i)edit.fileMarkers.push_back(i*100);
    edit.selectionStart=100000;edit.selectionEnd=110000;edit.cursorRow=12;edit.cursorCol=0;
    dispatch.on_button_a();assert(edit.marker_count()==songcore::PROGRAM_SLICE_MARKERS);
    assert(!edit.toolMessage.empty());
    std::vector<float> audio(64,.25f);assert(engine->loadSample(0,audio.data(),64));
    edit=SampleEditorState{};edit.totalFrames=64;edit.selectionStart=8;edit.selectionEnd=56;
    edit.sliceMethod=SampleEditorModule::SLICE_MANUAL;edit.fileMarkers={16,32};
    edit.cursorRow=15;edit.cursorCol=1;dispatch.on_button_a();assert(edit.isModified);
    edit.cursorRow=14;edit.cursorCol=5;dispatch.on_button_a();
    assert(edit.marker_count()==2&&edit.selectionStart==8&&edit.selectionEnd==56);
    puts("PASS: frame-resolution zoom, large-file bounds, fit selection/slice, pinned audition, editor tool navigation and rendered views");
}
