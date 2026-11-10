#include "ui/drumlab.h"
#include "ui/canvas.h"
#include "ui/theme.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "songcore/effects.h"
#include "audio-engine.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <cstring>

using namespace pt::ui;
namespace {
struct Notes : songcore::IMidiConsumer {
    std::vector<songcore::Event> events;
    void consume(const songcore::Event& event) override { if(event.type==songcore::EV_NOTE_ON)events.push_back(event); }
    void on_play(const std::string&,const std::string&,int64_t,int,int) override {}
    void on_stop() override {}
};
songcore::Project project() {
    auto p = songcore::make_default_project();
    const int models[] = {21,22,23,23,57,59,19,17};
    const int octaves[] = {1,3,5,5,3,4,3,4};
    for (int lane=0;lane<8;++lane) {
        auto& ins=p.instruments[lane];
        ins.name=drumlab_lane_name(lane);
        ins.sampleFilePath="/@mutable/p"+std::to_string(models[lane])+"-100-160-120.wav";
        ins.root={0,octaves[lane]};ins.volume=140;ins.loopMode="fwd";ins.transposeEnabled=false;
        auto& envelope=ins.modSlots[0];envelope.type=songcore::ModType::AHD;
        envelope.dest=songcore::ModDest::VOLUME;envelope.attack=0;envelope.hold=0;
        envelope.decay=lane<2?8:lane==2?2:12;
    }
    return p;
}

songcore::json document(const songcore::SongcoreHost& host) {
    return songcore::json::parse(songcore::serialize_project(host.project()));
}

void no_change(DrumLabState& s,songcore::Project p) {
    songcore::SongcoreHost host(nullptr,48000);
    assert(host.push_project(songcore::serialize_project(p)));
    const auto before=songcore::serialize_project(host.project());
    assert(!drumlab_apply(s,host));
    assert(songcore::serialize_project(host.project())==before);
}

void shot(const std::filesystem::path& target,const DrumLabState& state,const songcore::Project& p) {
    Canvas c;const auto theme=theme_studio();c.surface_rect(0,0,640,480,theme.background);
    c.draw_label("PocketTracker Studio / Drum Lab",18,28,theme.textValue,18);
    drumlab_draw(c,state,theme,p);
    std::ofstream f(target,std::ios::binary);f<<"P6\n640 480\n255\n";
    for(int i=0;i<640*480;++i){auto pixel=c.pixels()[i];char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};f.write(rgb,3);}
}

void word(std::ofstream& f,uint16_t v){const char b[]={char(v),char(v>>8)};f.write(b,2);}
void dword(std::ofstream& f,uint32_t v){const char b[]={char(v),char(v>>8),char(v>>16),char(v>>24)};f.write(b,4);}
void wav(const std::filesystem::path& target,const std::vector<float>& audio){
    std::ofstream f(target,std::ios::binary);f.write("RIFF",4);dword(f,36+audio.size()*2);f.write("WAVEfmt ",8);
    dword(f,16);word(f,1);word(f,2);dword(f,48000);dword(f,48000*4);word(f,4);word(f,16);f.write("data",4);dword(f,audio.size()*2);
    for(float v:audio)word(f,static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(v,-1.f,1.f)*32767))));
}
}

int main(int argc,char**argv){
    assert(argc==2);const std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);
    int recipes=0;
    for(int genre=0;genre<8;++genre)for(int length:{16,32,64})for(int lanes:{5,8})for(uint32_t seed:{0u,1u,0xdeadbeefu}){
        DrumLabState a;a.genre=genre;a.steps=length;a.activeLanes=lanes;a.seed=seed;
        auto b=a;assert(drumlab_generate(a)&&drumlab_generate(b));assert(a.grid==b.grid);
        assert(!a.dirty&&a.generatedSteps==length&&drumlab_hit_count(a)>0);
        assert(a.grid[0][0].velocity>0);
        for(int lane=0;lane<8;++lane)for(int step=0;step<64;++step){
            assert(a.grid[lane][step].velocity<=127&&a.grid[lane][step].delayTicks<12);
            if(lane>=lanes||step>=length)assert(a.grid[lane][step].velocity==0);
        }
        ++recipes;
    }
    DrumLabState sparse,dense;sparse.density=0;sparse.fill=0;dense=sparse;dense.density=100;
    assert(drumlab_generate(sparse)&&drumlab_generate(dense));assert(drumlab_hit_count(dense)>=drumlab_hit_count(sparse));
    const auto locked=dense.grid[2];dense.lanes[2].locked=true;++dense.seed;dense.genre=3;dense.steps=64;
    assert(drumlab_generate(dense));assert(dense.grid[2]==locked);
    const auto beforeInvalid=dense.grid;dense.steps=17;assert(!drumlab_generate(dense)&&dense.grid==beforeInvalid);

    // Append preserves populated-but-unreferenced material and deliberate song gaps.
    auto p=project();p.tracks[0].chainRefs={0,-1,1};p.tracks[1].chainRefs={4};
    p.grooves[0].steps={16,8,-1}; // generated phrases must explicitly choose another straight groove
    p.chains[0].phraseRefs[0]=0;p.chains[1].phraseRefs[0]=1;
    p.chains[2].phraseRefs[0]=2; // orphan chain must survive
    p.chains[3].phraseRefs[0]=4; // even an empty referenced phrase is reserved
    p.phrases[0].steps[0].note=songcore::Note::C4();p.phrases[0].steps[0].volume=70;
    p.phrases[1].steps[2].fx1Type=songcore::FX_LAT;p.phrases[1].steps[2].fx1Value=5;
    p.phrases[3].steps[3].note=songcore::Note::C4(); // orphan phrase must survive
    songcore::SongcoreHost host(nullptr,48000);assert(host.push_project(songcore::serialize_project(p)));
    auto before=document(host);DrumLabState s;s.activeLanes=5;s.steps=64;assert(drumlab_generate(s));
    assert(drumlab_apply(s,host));assert(s.appliedSongRow==3&&s.applySerial==1);
    auto after=document(host);
    for(const char* key:{"instruments","tables","grooves"})assert(before[key]==after[key]);
    for(int id=0;id<5;++id)assert(before["chains"][id]==after["chains"][id]);
    for(int id=0;id<5;++id)assert(before["phrases"][id]==after["phrases"][id]);
    assert(host.project().tracks[0].chainRefs[1]==-1);
    std::set<int> newChains,newPhrases;
    for(int lane=0;lane<8;++lane){
        const auto& refs=host.project().tracks[lane].chainRefs;
        if(lane>=5){assert(refs[3]==-1);continue;}
        const int chainId=refs[3];assert(chainId>=5&&newChains.insert(chainId).second);
        const auto& chain=host.project().chains[chainId];
        for(int bar=0;bar<4;++bar){
            const int phraseId=chain.phraseRefs[bar];assert(phraseId>=5&&newPhrases.insert(phraseId).second);
            const auto& phrase=host.project().phrases[phraseId];
            assert(phrase.steps[0].fx2Type==songcore::FX_GRV&&phrase.steps[0].fx2Value==1);
            for(int step=0;step<16;++step){const auto& hit=s.grid[lane][bar*16+step];const auto& row=phrase.steps[step];
                if(!hit.velocity){assert(row.note==songcore::Note::EMPTY());continue;}
                assert(row.instrument==s.lanes[lane].instrument&&row.volume==hit.velocity&&row.note==p.instruments[lane].root);
                assert(row.fx1Type==(hit.delayTicks?songcore::FX_LAT:0)&&row.fx1Value==hit.delayTicks);
            }
        }
    }
    assert(newChains.size()==5&&newPhrases.size()==20);
    assert(drumlab_apply(s,host)&&s.appliedSongRow==4&&s.applySerial==2);
    auto unavailable=project();for(auto& chain:unavailable.chains)chain.phraseRefs[0]=0;no_change(s,unavailable);
    unavailable=project();for(auto& phrase:unavailable.phrases)phrase.steps[0].note=songcore::Note::C4();no_change(s,unavailable);
    unavailable=project();unavailable.tracks[7].chainRefs.resize(256,-1);unavailable.tracks[7].chainRefs[255]=0;no_change(s,unavailable);
    unavailable=project();unavailable.instruments[4].sampleFilePath.reset();no_change(s,unavailable);
    unavailable=project();unavailable.instruments[4].root=songcore::Note::EMPTY();no_change(s,unavailable);
    unavailable=project();for(auto& groove:unavailable.grooves)groove.steps={16,8,-1};no_change(s,unavailable);
    auto dirty=s;dirty.dirty=true;no_change(dirty,project());
    auto corrupt=s;corrupt.grid[0][0].velocity=255;no_change(corrupt,project());

    // Compare the production preview payload with the actual sequencer bus, not a copied formula.
    int noteComparisons=0;
    for(bool soundfont:{false,true})for(int transpose:{12,-12})for(bool constrained:{false,true}){
        auto tuned=project();tuned.transpose=transpose&255;tuned.scaleKey=0;
        if(constrained){tuned.scales[0].enabled.assign(12,0);tuned.scales[0].enabled[2]=1;}
        for(int lane=0;lane<5;++lane){auto& ins=tuned.instruments[lane];ins.root={1,3};ins.transposeEnabled=true;
            if(soundfont){ins.instrumentType=songcore::InstrumentType::SOUNDFONT;ins.soundfontPath="test-tone.sf2";ins.sampleFilePath.reset();}}
        songcore::SongcoreHost tunedHost(nullptr,48000);assert(tunedHost.push_project(songcore::serialize_project(tuned)));
        DrumLabState pattern;pattern.steps=16;pattern.activeLanes=5;assert(drumlab_generate(pattern)&&drumlab_apply(pattern,tunedHost));
        Notes notes;tunedHost.router().add_consumer(&notes);tunedHost.schedule_song_range(pattern.appliedSongRow,pattern.appliedSongRow,nullptr);
        assert(!notes.events.empty());const int64_t frames=songcore::frames_per_step(tuned.tempo,48000);
        for(const auto& event:notes.events){const int step=static_cast<int>(event.frame/frames);assert(step>=0&&step<16);
            const auto preview=drumlab_note_on(tunedHost.project(),event.instrument,pattern.grid[event.track][step]);
            assert(std::memcmp(&preview,&event.noteOn,sizeof(preview))==0);
            if(soundfont){auto program=songcore::make_program(tunedHost.project().instruments[event.instrument],1,0);
                const auto expected=songcore::derive_soundfont_note(event.noteOn,event.frame,event.track,event.instrument,program,tuned.tempo,48000,false);
                const auto actual=songcore::derive_soundfont_note(preview,event.frame,event.track,event.instrument,program,tuned.tempo,48000,false);
                assert(actual.midiNote==expected.midiNote&&actual.midiVelocity==expected.midiVelocity);}
            ++noteComparisons;
        }
    }

    // Actual engine preview must sound, stop cleanly, and never alter the document.
    auto engine=std::make_unique<AudioEngine>();engine->setDeviceSampleRate(48000);
    songcore::SongcoreHost audioHost(engine.get(),48000);assert(audioHost.push_project(songcore::serialize_project(project())));
    assert(audioHost.load_media("").failed==0);audioHost.push_params();
    const auto original=songcore::serialize_project(audioHost.project());
    DrumLabState audioState;audioState.activeLanes=5;assert(drumlab_generate(audioState));
    assert(drumlab_preview(audioState,audioHost,*engine));
    std::vector<float> pcm;double energy=0;float peak=0;float block[512*2]{};
    for(int i=0;i<480;++i){drumlab_tick(audioState,audioHost,*engine);engine->processLiveBlock(block,512,2,48000);
        for(float value:block){assert(std::isfinite(value));energy+=double(value)*value;peak=std::max(peak,std::fabs(value));pcm.push_back(value);}}
    assert(energy>1e-5&&peak<1.5f&&audioState.previewDisplayStep>=0);
    assert(songcore::serialize_project(audioHost.project())==original);
    drumlab_stop(audioState,audioHost,*engine);assert(!audioState.previewRunning);
    // Eight physical WAV sources exercise the ordinary sample path as well as Mutable synthesis.
    auto sampleProject=project();std::vector<float> fixture;
    for(int lane=0;lane<8;++lane){
        fixture.clear();
        for(int frame=0;frame<6000;++frame){const float value=.2f*std::sin(frame*(.013f+.006f*lane))*std::exp(-frame/1700.f);fixture.push_back(value);fixture.push_back(value);}
        const auto path=std::filesystem::absolute(out/("fixture-"+std::to_string(lane)+".wav"));wav(path,fixture);
        sampleProject.instruments[lane].sampleFilePath=path.string();
    }
    assert(audioHost.push_project(songcore::serialize_project(sampleProject)));
    assert(audioHost.load_media("").loaded==8);audioHost.push_params();
    const auto sampleOriginal=songcore::serialize_project(audioHost.project());
    DrumLabState sampleState;sampleState.genre=3;sampleState.steps=16;sampleState.activeLanes=8;
    assert(drumlab_generate(sampleState)&&drumlab_preview(sampleState,audioHost,*engine));double sampleEnergy=0;
    for(int i=0;i<180;++i){drumlab_tick(sampleState,audioHost,*engine);engine->processLiveBlock(block,512,2,48000);for(float value:block){assert(std::isfinite(value));sampleEnergy+=double(value)*value;}}
    drumlab_stop(sampleState,audioHost,*engine);assert(sampleEnergy>1e-5&&songcore::serialize_project(audioHost.project())==sampleOriginal);
    // Stop a continuously looping source and inspect voices BEFORE any project/media replacement.
    auto continuous=sampleProject;for(auto& ins:continuous.instruments)for(auto& mod:ins.modSlots)mod.type=songcore::ModType::NONE;
    assert(audioHost.push_project(songcore::serialize_project(continuous)));assert(audioHost.load_media("").loaded==8);audioHost.push_params();
    DrumLabState held;held.activeLanes=8;assert(drumlab_generate(held)&&drumlab_preview(held,audioHost,*engine));
    for(int i=0;i<40;++i){drumlab_tick(held,audioHost,*engine);engine->processLiveBlock(block,512,2,48000);}
    assert(engine->getActiveVoiceCount()>0);drumlab_stop(held,audioHost,*engine);
    for(int i=0;i<20;++i)engine->processLiveBlock(block,512,2,48000);
    assert(engine->getActiveVoiceCount()==0);
    // Present path metadata cannot make an unloaded WAV or SF source previewable.
    audioHost.clear_instrument(0);audioHost.edit_project().instruments[0].sampleFilePath="missing-drum.wav";
    DrumLabState missing;missing.activeLanes=5;assert(drumlab_generate(missing));
    const auto missingDocument=songcore::serialize_project(audioHost.project());
    assert(!drumlab_preview(missing,audioHost,*engine)&&missing.status.find("not loaded")!=std::string::npos);
    assert(songcore::serialize_project(audioHost.project())==missingDocument);
    auto& unloadedSF=audioHost.edit_project().instruments[0];unloadedSF.instrumentType=songcore::InstrumentType::SOUNDFONT;
    unloadedSF.soundfontPath="missing-drum.sf2";unloadedSF.sampleFilePath.reset();
    assert(!drumlab_preview(missing,audioHost,*engine)&&missing.status.find("not loaded")!=std::string::npos);
    // MIDI START must discard preview lookahead BEFORE planning the new transport.
    assert(audioHost.push_project(songcore::serialize_project(sampleProject)));assert(audioHost.load_media("").loaded==8);audioHost.push_params();
    engine->processLiveBlock(block,512,2,48000); // let the audio-thread project-reset request complete before injecting bytes
    engine->resetEffectState();DrumLabState midiPreview;midiPreview.activeLanes=8;assert(drumlab_generate(midiPreview));
    int transportStarts=0;audioHost.set_before_play_hook([&]{++transportStarts;drumlab_before_transport(midiPreview,*engine);});
    assert(drumlab_preview(midiPreview,audioHost,*engine));audioHost.set_midi_sync_in(songcore::SYNC_IN_FULL);
    audioHost.poll();
    const uint8_t start[]={0xfa,0xf8};audioHost.midi_in_sink().on_bytes(start,2); // START arms; the first clock is the downbeat
    engine->processLiveBlock(block,512,2,48000);audioHost.poll();
    if(transportStarts!=1||midiPreview.previewRunning)std::cerr<<"MIDI fixture: starts="<<transportStarts<<" preview="<<midiPreview.previewRunning<<" bytes="<<audioHost.midi_in_bytes()<<" messages="<<audioHost.midi_in_messages()<<" mode="<<audioHost.midi_sync_in()<<" playing="<<audioHost.is_playing()<<'\n';
    assert(transportStarts==1&&!midiPreview.previewRunning);
    double transportEnergy=0;
    for(int i=0;i<40;++i){audioHost.poll();engine->processLiveBlock(block,512,2,48000);for(float value:block)transportEnergy+=double(value)*value;}
    assert(transportEnergy<1e-10&&engine->getActiveVoiceCount()==0);
    audioHost.stop();audioHost.set_midi_sync_in(songcore::SYNC_IN_OFF);audioHost.set_before_play_hook({});
    assert(audioHost.push_project(original));assert(audioHost.load_media("").failed==0);audioHost.push_params();
    drumlab_stop(audioState,audioHost,*engine);assert(!audioState.previewRunning);
    for(int i=0;i<20;++i)engine->processLiveBlock(block,512,2,48000);
    assert(engine->getActiveVoiceCount()==0);
    assert(songcore::serialize_project(audioHost.project())==original);
    // Controls follow their visible geometry: list rows move vertically, SLOT / LOCK and the
    // three bottom action buttons move horizontally. Boundaries do not wrap onto another axis.
    {
        DrumLabState navigation;navigation.row=16;
        const auto preserved=songcore::serialize_project(audioHost.project());
        const auto move=[&](Button button){assert(drumlab_button({button,ButtonAction::PRESSED,{}},navigation,audioHost,*engine));};
        move(Button::DPAD_DOWN);assert(navigation.row==17);
        move(Button::DPAD_RIGHT);assert(navigation.row==18);
        move(Button::DPAD_RIGHT);assert(navigation.row==19);
        move(Button::DPAD_RIGHT);assert(navigation.row==19);
        move(Button::DPAD_DOWN);assert(navigation.row==19);
        move(Button::DPAD_UP);assert(navigation.row==16);
        move(Button::DPAD_DOWN);assert(navigation.row==19); // retain the last action column
        move(Button::DPAD_LEFT);assert(navigation.row==18);
        move(Button::DPAD_UP);assert(navigation.row==16);
        move(Button::DPAD_LEFT);assert(navigation.row==16&&navigation.col==0);
        move(Button::DPAD_RIGHT);assert(navigation.row==16&&navigation.col==1);
        navigation.row=0;move(Button::DPAD_UP);assert(navigation.row==0);
        assert(drumlab_generate(navigation));navigation.row=17;
        const auto generatedGrid=navigation.grid;const auto generatedSeed=navigation.seed;
        drumlab_button({Button::A,ButtonAction::PRESSED,{}},navigation,audioHost,*engine);
        drumlab_button({Button::DPAD_RIGHT,ButtonAction::PRESSED,{true}},navigation,audioHost,*engine);
        drumlab_button({Button::A,ButtonAction::RELEASED,{}},navigation,audioHost,*engine);
        assert(!navigation.dirty&&navigation.grid==generatedGrid&&navigation.seed==generatedSeed);
        assert(songcore::serialize_project(audioHost.project())==preserved);
    }
    // Holding A to edit must not also run the Generate action on release.
    audioState.row=3;drumlab_button({Button::A,ButtonAction::PRESSED,{}},audioState,audioHost,*engine);
    drumlab_button({Button::DPAD_RIGHT,ButtonAction::PRESSED,{true}},audioState,audioHost,*engine);
    const auto seedBefore=audioState.seed;drumlab_button({Button::A,ButtonAction::RELEASED,{}},audioState,audioHost,*engine);
    assert(audioState.seed==seedBefore&&audioState.dirty);
    assert(drumlab_generate(audioState));audioState.row=19;
    drumlab_button({Button::A,ButtonAction::RELEASED,{}},audioState,audioHost,*engine);assert(audioState.confirmApply&&audioState.applySerial==0);
    drumlab_button({Button::A,ButtonAction::RELEASED,{}},audioState,audioHost,*engine);assert(!audioState.confirmApply&&audioState.applySerial==1);
    audioHost.play_song(audioState.appliedSongRow);assert(!drumlab_preview(audioState,audioHost,*engine));audioHost.stop();
    assert(drumlab_generate(audioState));
    audioState.row=3;shot(out/"drumlab-settings.ppm",audioState,audioHost.project());
    audioState.row=11;audioState.lanes[2].locked=true;shot(out/"drumlab-sources.ppm",audioState,audioHost.project());
    wav(out/"drumlab-preview.wav",pcm);
    songcore::json report={{"deterministic_recipes",recipes},{"allocation_safety","append only; occupied and orphan pools preserved; exhausted banks and invalid data rejected atomically"},
        {"preview_frames",pcm.size()/2},{"preview_peak",peak},{"preview_energy",energy},{"preview_document_unchanged",true},{"eight_wav_sources_energy",sampleEnergy},{"native_note_payload_comparisons",noteComparisons},{"saved_timing","LAT plus explicit straight GRV and project base-scale reset"}};
    std::ofstream(out/"drumlab-validation.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: "<<recipes<<" seeded recipes, lane locks, atomic safe append, blocked failures, polyphonic engine preview/document preservation, control release\n";
}
