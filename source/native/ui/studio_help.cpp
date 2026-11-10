#include "ui/studio_help.h"
#include "ui/demo_font_style.h"
#include "ui/studio.h"
#include "ui/helpers.h"
#include "studio/punch.h"
#include "studio/rack.h"
#include "studio/synth.h"
#include <algorithm>
#include <cctype>

namespace pt::ui {
namespace {
constexpr const char* topics[]={"Synth","FX Rack","Master","Punch / Resample","Input Recorder","Drumlab","Style","Digging / Jazz"};
constexpr const char* pages[]={"Workflow","Controls","Current context"};
std::string short_text(const std::string& s,int chars=74){return Canvas::clip_text(s,chars);}
std::string control_hint(std::string name) {
    for(char& c:name)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(name=="attack")return "Attack sets the fade-in time after the note starts.";
    if(name=="decay")return "Decay sets the transition from the peak to sustain.";
    if(name=="sustain")return "Sustain is the level while the note remains held.";
    if(name=="release")return "Release sets the fade after note-off; long values keep a tail.";
    if(name.find("cutoff")!=std::string::npos)return "Cutoff sets the filter boundary. Listen with resonance at a moderate level.";
    if(name.find("resonance")!=std::string::npos)return "Resonance emphasizes the region near the filter cutoff.";
    if(name=="pan")return "Pan positions the sound between the left and right channels.";
    if(name.find("detune")!=std::string::npos)return "Detune adjusts pitch around the instrument's normal tuning.";
    if(name=="volume")return "Volume sets this instrument's level before the track and master mix.";
    if(name=="drive")return "Drive adds saturation. Compare at similar output loudness.";
    return "This engine defines the parameter's response. Hold L and compare small changes.";
}
uint16_t bit(Button b){return uint16_t(1u<<static_cast<unsigned>(b));}
void release_performance(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine) {
    studio_synth_stop_audition(s,host);drumlab_stop(s.drumLab,host,engine);
    source_sampler_stop(s.sourceSampler);engine.studioRack.punch.store(0);
    s.studioAUsed=false;s.studioConfirm=false;
}
}

StudioHelpContent studio_help_content(const AppState& s,int topic,int page) {
    topic=std::clamp(topic,0,STUDIO_HELP_TOPICS-1);page=std::clamp(page,0,STUDIO_HELP_PAGES-1);
    StudioHelpContent h;h.title=std::string(topics[topic])+" / "+pages[page];
    auto& v=h.lines;
    if(page==0){
        switch(topic){
        case 0:v={"Choose TARGET SLOT, SYNTH MODEL and PRESET before shaping a sound.","Hold L to audition the root note; release L to stop the audition.","DRAFT needs LOAD INTO INSTRUMENT once; replacing a sound asks again.","LIVE edits reach the loaded sound while its sequence plays.","R + Left / Right changes engine, filter, amplifier and envelope pages.","Save an instrument preset and the project to keep a useful sound."};break;
        case 1:v={"FX Rack adds character to the mix. Choose a factory preset, then press A.","Adjust MODULE, MIX, COLOR, TEXTURE and FEEDBACK while listening.","The selected module determines how Color and Texture shape the effect.","ACTIVE switches the Studio processing path. Compare at a similar level.","USER SLOT plus SAVE CUSTOM / LOAD CUSTOM keeps your own FX settings.","The preset library also offers categorized effect starting points."};break;
        case 2:v={"Balance instruments and tracks before raising the master output.","Choose a master preset with A, then refine dynamics and tone.","Threshold and ratio control compression; attack and release shape its motion.","Output gain raises the result; peak ceiling limits final peaks.","Stereo width and high pass change the image and low-frequency content.","Save a custom slot and the project. Listen to an exported mix too."};break;
        case 3:v={"Eight banks contain 64 momentary effects; the visible grid has 4 x 2 pads.","Hold A to perform a pad. Release A to restore the mix.","L + R + Start starts/stops LIVE RESAMPLE from the main tracker or Studio.","This records internal output, including live synth edits and Punch.","In Punch: B + Down chooses Free or 1 / 2 / 4 / 8 bars (60-second cap).","Stop playback, then B + Up imports the take into a free slot for slicing."};break;
        case 4:v={"Instrument RECORD and Sample Editor RECORD both open this recorder.","Choose a source, mono/stereo, sample rate and an empty target instrument.","A on START RECORDING begins; A on STOP & KEEP TAKE finishes the WAV.","Waiting for audio means no frames yet; REC and meters mean frames arrived.","Stop playback; A on USE TAKE opens the completed take in Edit & Slice.","Save existing unsaved sample edits first. The completed take is retained."};break;
        case 5:v={"Load 5-8 sample or synth instruments first, then assign the Drumlab lanes.","Choose style, length, density, variation, swing, humanize and fill.","A on GENERATE creates a pattern; generating again changes its seed.","A on PREVIEW starts/stops its loop without changing your song.","A on APPLY asks again, then appends using only empty unused resources.","Lane locks preserve triggers, velocities and timing through Generate."};break;
        case 6:v={"Choose palette, surface material and Typeface independently.","A + arrows changes the selected appearance value immediately.","Typeface reaches the grid, Studio, browser and Sample Editor.","17 faces: Classic, six demo-scene and ten cleaner reading styles.","Each clean face suggests a matching palette; choices stay independent.","Appearance is saved with app settings through the normal clean exit."};break;
        case 7:v={"Choose an available provider and genre (for example Archive + Jazz).","Search finds a known item; Random suggests material in that genre.","Open a result, choose a track and use its transport to preview audio.","Set IN and OUT, then RENDER the region to a local PCM WAV (max 3 minutes).","IMPORT opens the chosen target in the normal Sample Editor for slicing.","Check the original source and license; listening access is not reuse permission."};break;
        }
    }else if(page==1){
        if(topic==0)v={"D-pad: move between target/model, preset, controls and Load.","A + Left / Right: small edits. A + Up / Down: larger edits.","R + Left / Right: parameter pages. Hold L: root-note audition.","A on Load: link a draft, with replacement confirmation if needed.","Start: tracker playback. B: leave Studio.","B + Left / Right: Studio tabs. Select: open this paged guide."};
        if(topic==1||topic==2)v={"Up / Down: select a row. A + arrows: edit the selected value.","A on FACTORY PRESET: apply the chosen preset.","A on SAVE CUSTOM / LOAD CUSTOM: use the selected user slot.","A on the preset Library row: browse categorized preset files.","B + Left / Right: Studio tabs. Start: tracker playback.","B: leave Studio. Select: open this paged guide."};
        if(topic==3)v={"D-pad: choose a pad. Hold A: perform. Release A: restore.","L / R: choose the category. Held A + D-pad: change the active gesture.","L + Select: open/close Punch. R + Select: leave Studio.","L + R + Start: start/stop internal live recording (not transport).","B + Down: capture length. B + Up: use the take when playback is stopped.","Opening help, stopping, changing bank or leaving releases Punch."};
        if(topic==4)v={"Up / Down: select a row. Left / Right: change source or format.","A on START / STOP: begin capture or keep the completed take.","A on REFRESH: recheck inputs after connecting a device.","A on USE TAKE: load a free slot, then open Edit & Slice.","B returns to the current edit. Start controls tracker playback.","No software input monitoring: use interface direct monitoring if available."};
        if(topic==5)v={"Up / Down: fields. Left / Right: source/lock or bottom action.","A + arrows: edit (Up / Down is coarse). A on LOCK: toggle lane lock.","L / R: show another 16-step bar. A on GENERATE: make a variation.","A on PREVIEW: loop/stop. A twice on APPLY: append safely.","B + Left / Right: Studio tabs. Start: tracker playback.","B: leave Studio. Select: open this paged guide."};
        if(topic==6)v={"Up / Down: palette, material or Typeface. A + arrows: change it.","B + Left / Right: Studio tabs. Start: tracker playback.","Select: open this paged guide. B: return to the tracker.","The app settings save keeps Typeface independently of the palette."};
        if(topic==7)v={"A + arrows: provider / genre. On Search, Left / Right picks Search or Random.","Results / Tracks: Right enters the list; Up / Down selects; A opens/plays.","Left or B leaves a focused list. L toggles source order / title order.","R + Left / Right: seek 5 s; add A for 1 s steps.","A on IN / OUT: mark here. A + Left / Right: 10 ms; A + Up / Down: 1 s.","A on RECORD toggles region recording; A on RENDER renders IN to OUT.","A on IMPORT: use the WAV. Source / License opens the original HTTPS page."};
    }else{
        switch(topic){
        case 0:{
            const int count=studio_synth_control_count(s),control=s.studioRow-3;
            v={"Engine: "+std::string(studio::model_name(s.studioModel)),"Parameter page: "+studio_synth_page_name(s),
               studio_synth_live(s)?"LIVE: edits update the linked instrument.":"DRAFT: audition first, then Load once to link it."};
            if(control>=0&&control<count)v.push_back(control_hint(studio_synth_control_label(s,control)));
            for(int i=0;i<count;++i)v.push_back((i==control?"> ":"  ")+studio_synth_control_label(s,i)+" = "+studio_synth_control_value(s,i));
            break;}
        case 1:case 2:{
            const int row=std::clamp(s.studioRow,0,studio_rows(topic)-1);
            v={topic==s.studioTab?std::string(topic==1?"FX setting: ":"Master setting: ")+studio_label(s,row):"Open this Studio tab to inspect its selected control.",
               topic==s.studioTab?"Value: "+studio_value(s,row):"Help topics can be browsed without changing the underlying tab.",
               "Factory presets apply with A; editing changes the active mix.","Custom slots store FX and Master settings separately.",
               "Keep output peaks below full scale. Compare at a similar loudness.","Preset files are starting points; adjust them to your material."};break;}
        case 3:v={"Selected gesture: "+std::string(studio::punch_name(s.studioPunch)),studio::punch_detail(s.studioPunch),
                 "Punch affects the output mix; it does not rewrite tracker notes.","Live resample captures the actual internal output, not an external input.","Free/fixed capture is capped at 60 seconds; keep an eye on the status strip.","Use Take requires stopped playback and a free instrument; existing sounds stay."};break;
        case 4:v={"Recorder status: "+studio_capture_status(s.capture),"Received frames: "+std::to_string(s.capture.frames),
                 "Direct iPhone-to-Brick USB-C failed in the user's tested setup.","Class-compliant interfaces are a separate, still hardware-dependent route.","Charging, detection or Trust alone is not a successful audio recording.","If frames stay at zero, check the selected route and input error."};break;
        case 5:v={"Style: "+std::string(drumlab_genre_name(s.drumLab.genre)),"Lanes: "+std::to_string(s.drumLab.activeLanes)+"   Steps: "+std::to_string(s.drumLab.steps),
                 s.drumLab.generated?"Generated pattern ready; preview it before applying.":"Generate a pattern before Preview or Apply.","A lane lock preserves its current rhythm during regeneration.","Apply claims empty resources and appends after occupied song rows.","Save the resulting project so its new arrangement is retained."};break;
        case 6:v={"Current palette: "+s.theme.name,"Material ID: "+std::to_string(s.theme.surfaceStyle),"Typeface: "+std::string(demo_font_style_name(s.settings.fontStyle)),
                 "Choose readable values, clear contrast and a texture you enjoy.","Help uses the current palette so it stays consistent with your screen.","No musical parameters are changed by a design selection."};break;
        case 7:v={"Provider: "+std::string(source_sampler_provider_name(s.sourceSampler.provider)),"Genre: "+std::string(source_sampler_genre_name(s.sourceSampler.genre)),
                 "Track: "+short_text(s.sourceSampler.title,58),"A result may contain several tracks; choose the actual audio track.",
                 "Archive example: Jelly Roll Morton / Shreveport Stomp (1924).","For an easy first cut, mark a 10-30 second region, render, then import.","Descriptions and instrument tags may be absent. Check Source and License."};break;
        }
    }
    return h;
}

std::vector<std::string> studio_help_wrapped_lines(Canvas& c,const StudioHelpContent& content) {
    std::vector<std::string> lines;
    for(const auto& paragraph:content.lines){
        std::string row;
        for(size_t pos=0;pos<paragraph.size();){
            const auto end=paragraph.find(' ',pos);const auto word=paragraph.substr(pos,end==std::string::npos?end:end-pos);
            const auto joined=row.empty()?word:row+" "+word;
            if(!row.empty()&&c.label_width(joined,14)>560){lines.push_back(row);row=word;}else row=joined;
            if(end==std::string::npos)break;pos=end+1;
        }
        if(!row.empty())lines.push_back(row);
    }
    return lines;
}

bool studio_help_button(const ButtonEvent& e,AppState& s,songcore::SongcoreHost& host,AudioEngine& engine) {
    const auto mask=bit(e.button);
    if(!s.studioHelp&&(s.studioHelpHeldButtons&mask)){
        if(e.action==ButtonAction::RELEASED){s.studioHelpHeldButtons&=~mask;if(e.button==Button::A){s.studioAUsed=false;s.drumLab.aUsed=false;s.sourceSampler.aUsed=false;engine.studioRack.punch.store(0);}if(e.button==Button::B)s.studioBUsed=false;}
        return true;
    }
    if(!s.studioOpen)return false;
    if(!s.studioHelp&&e.button==Button::SELECT&&!e.mods.l&&!e.mods.r){
        release_performance(s,host,engine);
        if(e.mods.a)s.studioHelpHeldButtons|=bit(Button::A);if(e.mods.b)s.studioHelpHeldButtons|=bit(Button::B);
        if(e.action==ButtonAction::RELEASED){s.studioHelp=true;s.studioHelpTopic=std::clamp(s.studioTab,0,7);s.studioHelpPage=0;
            s.drumLab.showHelp=false;s.sourceSampler.showHelp=false;
            if(e.mods.a)s.studioHelpHeldButtons|=bit(Button::A);if(e.mods.b)s.studioHelpHeldButtons|=bit(Button::B);}
        return true;
    }
    if(!s.studioHelp)return false;
    if(e.action==ButtonAction::PRESSED)s.studioHelpHeldButtons|=mask;
    else s.studioHelpHeldButtons&=~mask;
    if(e.button==Button::A&&e.action==ButtonAction::RELEASED){engine.studioRack.punch.store(0);s.studioAUsed=false;s.drumLab.aUsed=false;s.sourceSampler.aUsed=false;}
    if(e.action==ButtonAction::PRESSED){
        if(e.button==Button::DPAD_LEFT||e.button==Button::DPAD_RIGHT)s.studioHelpPage=(s.studioHelpPage+3+(e.button==Button::DPAD_RIGHT?1:-1))%3;
        if(e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN){s.studioHelpTopic=(s.studioHelpTopic+8+(e.button==Button::DPAD_DOWN?1:-1))%8;s.studioHelpPage=0;}
    }else if(e.button==Button::SELECT||e.button==Button::B){s.studioHelp=false;s.studioBUsed=false;}
    return true;
}

void draw_studio_help(Canvas& c,const AppState& s) {
    const auto& t=s.theme;const int topic=std::clamp(s.studioHelpTopic,0,7),page=std::clamp(s.studioHelpPage,0,2);
    c.reset_clip();c.panel_rect(16,102,608,318,t.rowEvery4th,with_alpha(t.rowCursor,.65f),7);
    c.draw_label("CONTEXT GUIDE / "+std::to_string(topic+1)+" OF 8",32,114,t.rowCursor,12);
    const auto content=studio_help_content(s,topic,page);
    c.draw_label(short_text(content.title,55),32,138,t.textTitle,18);
    const auto lines=studio_help_wrapped_lines(c,content);
    for(size_t i=0;i<lines.size();++i)c.draw_label(lines[i],32,174+int(i)*24,t.textValue,14);
    c.draw_label("Left / Right: page "+std::to_string(page+1)+" / 3    Up / Down: help topic",24,436,t.textParam,12);
    c.draw_label("Select or B: return to your screen    Help navigation does not edit sounds",24,458,t.textEmpty,12);
}
}
