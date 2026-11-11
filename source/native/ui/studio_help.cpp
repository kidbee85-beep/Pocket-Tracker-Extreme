#include "ui/studio_help.h"
#include "ui/demo_font_style.h"
#include "ui/studio.h"
#include "ui/studio_synth_picker.h"
#include "ui/helpers.h"
#include "studio/punch.h"
#include "studio/rack.h"
#include "studio/synth.h"
#include <algorithm>
#include <cctype>

namespace pt::ui {
namespace {
constexpr const char* topics[]={"Synth","FX Rack","Master","Punch / Resample","Input Recorder","Drumlab","Settings / Appearance","Sources"};
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
    if(name=="glide rate")return "Glide rate sweeps the octave layers down or up. Centre holds the pitch.";
    if(name=="root anchor")return "Root anchor mixes the note you played into the octave sweep.";
    if(name=="gravity")return "More gravity shortens the fall and makes impacts arrive sooner.";
    if(name=="rebound")return "More rebound lets the ball bounce more times between drops.";
    if(name=="eccentricity")return "Eccentricity bunches the bell strikes in one part of the orbit.";
    if(name=="rain density")return "Rain density adds independent streams of drops.";
    if(name=="pitch spread")return "Pitch spread sets how far impacts can travel up the selected scale.";
    return "This engine defines the parameter's response. Hold L and compare small changes.";
}
uint16_t bit(Button b){return uint16_t(1u<<static_cast<unsigned>(b));}
void release_momentary_inputs(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine) {
    if(!s.resampleView.ownsSynthRoot&&!s.sampleGranular.recording.ownsRoot)
        studio_synth_stop_audition(s,host);
    if(s.studioTab==5)drumlab_release_grid_audition(s.drumLab,host);
    engine.studioRack.punch.store(0);
    s.studioAUsed=false;s.studioConfirm=false;s.studioPunchBankHeld=0;
    s.studioSynthPicker.chooseArmed=false;
    s.sampleGranular.presetPicker.chooseArmed=false;
    s.sampleGranular.presetPicker.shortcut=GranularPresetShortcut::None;
    s.sourceSampler.quickClipArmed=false;
    s.sourceSampler.collectionArmed=false;
    s.sourceSampler.aUsed=false;
}
void release_help_button(Button button, AppState& s, AudioEngine& engine) {
    if(button==Button::A){
        s.studioAUsed=false;s.drumLab.aUsed=false;s.sourceSampler.aUsed=false;
        engine.studioRack.punch.store(0);
    }
    if(button==Button::B)s.studioBUsed=false;
}
}

StudioHelpContent studio_help_quick_content(const AppState& s) {
    if (s.studioWorkspace.open) {
        return {"Studio sections", {"Up / Down  Choose a section",
            "A  Open the highlighted section", "B  Return to the current section"}};
    }
    switch(s.studioTab) {
    case 0:
        if (sample_granular_preset_picker_open(s)) {
            return {"Grain presets", {"D-pad  Browse presets    L / R  Change bank",
                "A  Apply the highlighted preset", "A + Down  Toggle Favorite    A + Up  Open Favorites",
                "B  Return to Sample Granular"}};
        }
        if (studio_synth_picker_open(s)) {
            return {s.studioSynthPicker.mode==StudioSynthPickerMode::Model?"Synth models":"Synth presets",
                {"D-pad  Browse families and sounds", "Hold L  Audition as you browse",
                 "A  Choose the highlighted item    B  Return to Synth"}};
        }
        if (sample_granular_studio(s)) {
            return {"Sample Granular / "+std::string(sample_granular_page_name(s)),
                {"A on Load Source  Choose audio", "A on waveform  Switch View / Position",
                 "A + arrows  Edit the focused value", "Hold R + Select  Audition the sample",
                 "R + Left / Right  Change parameter page"}};
        }
        return {"Synth / "+studio_synth_page_name(s), {"A on Model or Preset  Browse sounds",
            "A + arrows  Edit the focused value", "Hold L  Audition    R + Left / Right  Change page",
            "A on Apply  Link the draft to this instrument"}};
    case 1: case 2: {
        const auto row=std::clamp(s.studioRow,0,studio_rows(s.studioTab)-1);
        return {std::string(topics[s.studioTab])+" / "+studio_label(s,row),
            {"Up / Down  Choose a control", "A + arrows  Edit the selected value",
             "A on Factory Preset  Apply the selected preset", "B + Left / Right  Move between Studio tabs"}};
    }
    case 3: return {"Punch / "+std::string(studio::punch_name(s.studioPunch)),
        {"D-pad  Choose a pad    L / R  Change bank", "Hold A  Perform the pad. Release A to end.",
         "L + R + Start  Start or stop internal resampling", "B + Down  Choose take length    B + Up  Use take"}};
    case 4: return {s.capture.internal_source()?"Record / Internal mix":"Record / Input",
        {"Up / Down  Choose a control", "Left / Right  Change input, length or format",
         "A on Start / Stop  Record or finish the take", "A on Use Take  Open the audio in the Sample Editor"}};
    case 5:
        if (s.drumLab.gridFocused)
            return {"Drumlab / Step Edit", {"D-pad  Choose a step    L / R  Change bar",
                "A  Toggle the hit    A + Down  Preview the loop", "B + Up / Down  Undo / Redo",
                "B  Return to Drumlab controls"}};
        return {"Drumlab", {"Up / Down  Choose a setting or lane", "A + arrows  Edit a value",
            "A on Slot  Open Step Edit", "A on Preview  Start or stop the loop",
            "A on Apply twice  Add the pattern to the song"}};
    case 6: return {"Appearance", {"Up / Down  Choose palette, material or typeface",
        "A + arrows  Change the selected value", "B  Return to Settings"}};
    case 7: {
        const auto& source=s.sourceSampler;
        if(source.collectionOpen)
            return {"Sources / Browse", {"L / R  Change provider",
                source.collectionProvider>=3?"Up / Down  Choose a radio genre":"Left / Right  Choose a group    Up / Down  Choose a shelf",
                "A  Open the highlighted list    B  Return to Sources"}};
        if(source.listFocused)
            return {"Sources / Results", {"Up / Down  Choose a result or track",
                "A  Open or play the selection", "B / Left  Return to the Sources controls"}};
        if(source.live&&source.liveHistoryAvailable)
            return {"Sources / Live radio", {
                source.recording?"A on Record live  Finish the current take":"R + Left / Right  Jump five seconds",
                "A on Rewind  Return to live", "A on Save last  Open past audio in the Sample Editor",
                "A on IN / OUT  Mark a range. Render selection saves it."}};
        if(source.live)
            return {"Sources / Live radio", {"Up / Down  Choose a control",
                "Left / Right on Record live  Set the take length", "A on Record live  Start or finish a take"}};
        return {"Sources / Archive and radio", {"A on Browse sources  Choose provider and genre",
            "A on a result  Open its tracks. A plays the selected track.",
            "A on Quick clip  Mark an excerpt from the playhead", "A on Render selection  Open the WAV in the Sample Editor"}};
    }
    }
    return {"Studio", {"B + Select  Choose a section", "B + Left / Right  Move between tabs",
        "B  Return to the tracker"}};
}

StudioHelpContent studio_help_content(const AppState& s,int topic,int page) {
    topic=std::clamp(topic,0,STUDIO_HELP_TOPICS-1);page=std::clamp(page,0,STUDIO_HELP_PAGES-1);
    StudioHelpContent h;h.title=std::string(topics[topic])+" / "+pages[page];
    auto& v=h.lines;
    if(topic==0&&sample_granular_studio(s)){
        h.title=std::string("Sample Granular / ")+pages[page];
        if(page==0&&sample_granular_preset_picker_open(s)){
            h.title="Grain presets / Favorites";
            v={"D-pad selects a preset. L / R changes the bank.",
               "A + Down adds or removes the highlighted factory preset.",
               "A + Up opens Favorites or returns to the previous factory bank.",
               "Favorites holds up to 16 presets and saves with app settings.",
               "A applies the selected settings to the current sample. B closes the browser."};
            return h;
        }
        if(page==0)v={"Choose LOAD SOURCE to load audio and activate Sample Granular.","On the waveform, A switches between View and Position.","Position moves the grain start. Scan shows where playback has reached.","A on GRAIN PRESET browses the factory banks and Favorites.","A/B stores settings, then swaps between your two versions.","REC captures the root or the playing mix. STOP saves the WAV.","USE TAKE opens the saved audio in a free instrument slot."};
        else if(page==1)v={"Waveform in View: A + Up / Down zooms. A + Left / Right pans.","In Position: A + Left / Right nudges. A + Up / Down makes a larger jump.","Hold R + Select to audition. Adjust Position while you listen.","R + Left / Right changes the parameter page.","On A/B, A stores or switches sounds. A + Up sets the reference A.","On A/B, A + Left / Right blends A and B. Hold R + Select to listen.","B + Select opens the Studio sections. Start controls transport."};
        else{
            v={sample_granular_page_name(s),s.studioSynthPage==5?"Note level / pan apply on the next note. Grain level / spread edits live.":"Freeze stops auto-scan; Position still moves the source window.","Settings-only .ptgrain files keep the source. Full patches name it.","Crop long sources before loading; no unlimited RAM decode."};
            for(int i=0;i<sample_granular_control_count(s);++i)v.push_back(sample_granular_control_label(s,i)+" = "+sample_granular_control_value(s,i));
        }
        return h;
    }
    if(page==0){
        switch(topic){
        case 0:v={"Choose a target slot. Press A on SYNTH MODEL to browse families.","Press A on PRESET to choose a sound for the current model.","In either browser, hold L and move to audition before choosing.","APPLY puts a draft into the slot. LIVE edits reach the linked slot.","On the bottom row, Right selects A/B. Press A to store sound A.","Edit the working sound B. Press A on A/B to swap while listening.","L + R + Start captures a WAV. With transport stopped, it holds the root."};break;
        case 1:v={"FX Rack adds character to the mix. Choose a factory preset, then press A.","Adjust MODULE, MIX, COLOR, TEXTURE and FEEDBACK while listening.","The selected module determines how Color and Texture shape the effect.","ACTIVE switches the Studio processing path. Compare at a similar level.","USER SLOT plus SAVE CUSTOM / LOAD CUSTOM keeps your own FX settings.","The preset library also offers categorized effect starting points."};break;
        case 2:v={"Balance instruments and tracks before raising the master output.","Choose a master preset with A, then refine dynamics and tone.","Threshold and ratio control compression; attack and release shape its motion.","Output gain raises the result; peak ceiling limits final peaks.","Stereo width and high pass change the image and low-frequency content.","Save a custom slot and the project. Listen to an exported mix too."};break;
        case 3:v={"Eight banks contain 64 momentary effects; the visible grid has 4 x 2 pads.","Hold A to perform a pad. Release A to restore the mix.","L + R + Start starts/stops LIVE RESAMPLE from the main tracker or Studio.","This records internal output, including live synth edits and Punch.","In Punch: B + Down chooses Free or 1 / 2 / 4 / 8 bars (60-second cap).","Stop playback, then B + Up imports the take into a free slot for slicing."};break;
        case 4:v={"Instrument RECORD and Sample Editor RECORD both open this recorder.","Internal master resamples the mix after FX/Master, including Punch.","Start plays the song. A on START RESAMPLE records; A again stops and saves.","Choose Free / 1 / 2 / 4 / 8 bars and an empty target slot.","Stop playback; A on USE TAKE opens the completed take in Edit & Slice.","REFRESH MIC / USB discovers external sources. Internal mix needs no input."};break;
        case 5:v={"Assign loaded samples or synths to the Drumlab lanes.","Choose EUCLIDEAN under Style for a separate cycle on each lane.","PERIOD sets the cycle. HITS are spread evenly. ROTATE moves them later.","GENERATE refreshes unlocked lanes. A on SLOT opens Step Edit.","In Step Edit, A toggles a hit and A + Down previews the loop.","APPLY twice appends it to the song in free chains and phrases.","Lane locks protect triggers, velocities and timing."};break;
        case 6:v={"Appearance belongs to app Settings, separate from musical Studio tabs.","Choose palette, texture/material and Typeface independently.","A + arrows changes the selected appearance value immediately.","Typeface reaches the grid, Studio, browser and Sample Editor.","B returns to Settings. Theme Colors opens detailed color editing.","Existing themes and fonts are retained through the normal settings save."};break;
        case 7:v={"A on Browse sources opens the browser. L/R changes provider.","Archive shelves: Left/Right group, Up/Down shelf, A opens.","Radio genres: Up/Down shows stations, A opens the list.","Files: mark IN/OUT or set a Quick clip from the playhead.",
            s.sourceSampler.liveHistoryAvailable?"Rewind with Left/Right. A returns live. Save last makes a WAV.":"Record live: Left/Right picks 10 to 60 s. A starts a take.",
            "Render + Edit opens the saved excerpt in the Sample Editor.","Source and License opens the original page and its reuse terms."};break;
        }
    }else if(page==1){
        if(topic==0)v={"D-pad moves between target, model, preset, controls and Apply.","A + arrows edits. A + Down on Model opens Sample Granular.","R + Left / Right changes page. Hold L to audition the root.","On A/B, A + Up stores the current sound as a new reference A.","L + R + Start starts or stops resampling. B + Down sets length.","Stop playback, then B + Up loads the take into a free slot.","B + Select opens Studio sections. B + Left / Right steps between tabs.","Start controls playback. B returns to the tracker."};
        if(topic==1||topic==2)v={"Up / Down: select a row. A + arrows: edit the selected value.","A on FACTORY PRESET: apply the chosen preset.","A on SAVE CUSTOM / LOAD CUSTOM: use the selected user slot.","A on the preset Library row: browse categorized preset files.","B + Left / Right: Studio tabs. Start: tracker playback.","B: leave Studio. Select: open this paged guide."};
        if(topic==3)v={"D-pad: choose a pad. Hold A: perform. Release A: restore.","L / R: category. B + Left / Right: Studio tabs, one step.","L + Select: Punch shortcut. R + Select: leave Studio.","L + R + Start: internal resample. Start alone: playback.","B + Down: length. B + Up: use take with playback stopped.","Help, Stop, category change and leaving release Punch."};
        if(topic==4)v={"Up / Down: select a row. Left / Right: change source, length or format.","A on START / STOP: begin capture or keep the completed take.","A on REFRESH MIC / USB: discover external inputs; internal mix stays available.","If external input is stuck: Left on Source returns to internal master.","A on USE TAKE: load the selected empty slot, then open Edit & Slice.","Internal mix: B + Down changes length; B + Up uses the take.","L + R + Start records/stops internal mix. Start alone plays the song.","B returns to the current edit. Unsaved sample edits and old takes are kept."};
        if(topic==5)v={"Up / Down chooses a setting or lane. Left / Right chooses a lane field.","A + arrows edits. On LOCK, A toggles the lane lock.","A on SLOT opens Step Edit. D-pad moves. L / R changes bars.","A toggles the selected hit. A + Down starts or stops loop preview.","In Step Edit, B + Up undoes. B + Down redoes the last step edit.","B leaves Step Edit. A on APPLY twice adds the result to your song.","B + Select opens Studio sections. Start controls tracker playback."};
        if(topic==6)v={"Settings > Appearance: palette, material and Typeface.","Up / Down: choose a row. A + arrows: change its value.","Select: open this paged guide. B: return to Settings.","This Settings dialog does not switch musical Studio tabs.","Theme Colors remains available for detailed color editing.","The app settings save keeps Typeface independently of the palette."};
        if(topic==7)v={"A opens Browse sources. Inside, L/R changes provider.","A opens a result. B steps back.",
            s.sourceSampler.liveHistoryAvailable?"R + Left/Right seeks 5 s. On Rewind, A returns to live.":"Files: R + arrows seeks 5 s; A sets IN/OUT at playhead.",
            "On IN/OUT: A + arrows trims 10 ms fine or 1 s coarse.",
            s.sourceSampler.liveHistoryAvailable?"Save last opens audio up to the playhead in the Sample Editor.":"Quick clip: Left / Right picks length. A marks from playhead.",
            "Record live: Left/Right chooses length. A starts or finishes.","Render + Edit opens the saved excerpt when it is ready.","B + Left / Right: Studio tabs."};
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
        case 4:
            if(s.capture.internal_source())v={"Source: Internal master / resample","Captured frames: "+std::to_string(s.resampleView.frames),"Records tracker output after FX and Master; Punch is included.","Start plays the song. Record alone with stopped playback may capture silence.","No USB device or external source service is needed.","Stop playback before Use Take. Existing sample edits stay protected."};
            else v={"Recorder status: "+studio_capture_status(s.capture),"Received frames: "+std::to_string(s.capture.frames),"Direct iPhone USB failed; the phone reports excessive power demand.","A power warning means this setup is not an audio route.","Class-compliant interface capture remains a separate hardware test.","Charging, detection or Trust alone does not confirm audio."};break;
        case 5:v={"Style: "+std::string(drumlab_genre_name(s.drumLab.genre)),"Lanes: "+std::to_string(s.drumLab.activeLanes)+"   Steps: "+std::to_string(s.drumLab.steps),
                 s.drumLab.generated?"Generated pattern ready; preview it before applying.":"Generate a pattern before Preview or Apply.","A lane lock preserves its current rhythm during regeneration.","Apply claims empty resources and appends after occupied song rows.","Save the resulting project so its new arrangement is retained."};break;
        case 6:v={"Current palette: "+s.theme.name,"Material ID: "+std::to_string(s.theme.surfaceStyle),"Typeface: "+std::string(demo_font_style_name(s.settings.fontStyle)),
                 "Choose readable values, clear contrast and a texture you enjoy.","Help uses the current palette so it stays consistent with your screen.","No musical parameters are changed by a design selection."};break;
        case 7:v={"Provider: "+std::string(source_sampler_provider_name(s.sourceSampler.provider)),"Genre: "+std::string(source_sampler_genre_name(s.sourceSampler.genre)),
                 "Track: "+short_text(s.sourceSampler.title,58),"A result may contain several playable tracks.",
                 "File: set IN/OUT. Live: choose a short recording length.","Descriptions can be absent. Check Source and License."};break;
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

bool studio_help_captures(const ButtonEvent& e,const AppState& s) {
    // In the closed state the SELECT bit arms an ordinary tap. Other
    // retained bits belong to the modal and must consume their releases.
    return s.studioHelp || (e.button!=Button::SELECT && (s.studioHelpHeldButtons&bit(e.button)));
}

bool studio_help_button(const ButtonEvent& e,AppState& s,songcore::SongcoreHost& host,AudioEngine& engine) {
    const auto mask=bit(e.button);
    if (studio_help_captures(e,s)) {
        const bool wasHeld=(s.studioHelpHeldButtons&mask)!=0;
        if(e.action==ButtonAction::RELEASED||e.cancelled){
            s.studioHelpHeldButtons&=~mask;
            release_help_button(e.button,s,engine);
        }
        if(e.cancelled||!s.studioHelp)return true;
        if(e.action==ButtonAction::PRESSED){
            s.studioHelpHeldButtons|=mask;
            if(e.button==Button::DPAD_LEFT||e.button==Button::DPAD_RIGHT){
                const bool right=e.button==Button::DPAD_RIGHT;
                s.studioHelpPage=s.studioHelpPage==STUDIO_HELP_QUICK?(right?0:STUDIO_HELP_PAGES-1)
                    :(s.studioHelpPage+STUDIO_HELP_PAGES+(right?1:-1))%STUDIO_HELP_PAGES;
            }
            if(e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN){
                s.studioHelpTopic=(s.studioHelpTopic+STUDIO_HELP_TOPICS+
                    (e.button==Button::DPAD_DOWN?1:-1))%STUDIO_HELP_TOPICS;
                s.studioHelpPage=0;
            }
        }else if(wasHeld){
            if(e.button==Button::SELECT||e.button==Button::B){
                s.studioHelp=false;
                // A SELECT already held when B closes belongs to Help, not
                // to a new ordinary tap on the view below it.
                const auto heldSelect=s.studioHelpHeldButtons&bit(Button::SELECT);
                s.studioInputSuppress|=heldSelect;
                s.studioHelpHeldButtons&=~heldSelect;
            }
            if(e.button==Button::A)
                s.studioHelpPage=s.studioHelpPage==STUDIO_HELP_QUICK?0:STUDIO_HELP_QUICK;
        }
        return true;
    }
    if(!s.studioOpen||e.button!=Button::SELECT||e.cancelled||e.mods.l||e.mods.r||e.mods.b)return false;
    if(e.action==ButtonAction::PRESSED){
        s.studioHelpHeldButtons|=mask;
        // A release that began on the covered view must not choose a preset,
        // start a recording, or enter Details when it reaches the modal.
        if(e.mods.a)s.studioInputSuppress|=bit(Button::A);
        release_momentary_inputs(s,host,engine);
    }else if(s.studioHelpHeldButtons&mask){
        s.studioHelpHeldButtons&=~mask;
        if(e.mods.a)s.studioInputSuppress|=bit(Button::A);
        s.studioInputSuppress|=s.studioWorkspace.held;
        s.studioWorkspace.held=0;
        s.studioHelp=true;
        s.studioHelpTopic=std::clamp(s.studioTab,0,STUDIO_HELP_TOPICS-1);
        s.studioHelpPage=STUDIO_HELP_QUICK;
        release_momentary_inputs(s,host,engine);
    }
    return true;
}

void draw_studio_help(Canvas& c,const AppState& s) {
    const auto& t=s.theme;
    c.reset_clip();
    if(s.studioHelpPage==STUDIO_HELP_QUICK){
        c.panel_rect(16,119,608,274,t.rowEvery4th,with_alpha(t.rowCursor,.75f),7);
        c.draw_label("QUICK HELP",32,134,t.rowCursor,12);
        const auto content=studio_help_quick_content(s);
        c.draw_label(Canvas::clip_label(content.title,560,18),32,156,t.textTitle,18);
        const auto lines=studio_help_wrapped_lines(c,content);
        for(size_t i=0;i<lines.size();++i)c.draw_label(lines[i],32,192+int(i)*20,t.textValue,14);
        c.fill_rect(32,350,576,1,with_alpha(t.rowCursor,.38f));
        c.draw_label("A  Details      B / Select  Back",32,365,t.textTitle,12);
        return;
    }
    const int topic=std::clamp(s.studioHelpTopic,0,STUDIO_HELP_TOPICS-1);
    const int page=std::clamp(s.studioHelpPage,0,STUDIO_HELP_PAGES-1);
    c.panel_rect(16,102,608,316,t.rowEvery4th,with_alpha(t.rowCursor,.65f),7);
    c.draw_label("STUDIO HELP",32,116,t.rowCursor,12);
    const auto content=studio_help_content(s,topic,page);
    c.draw_label(short_text(content.title,55),32,138,t.textTitle,18);
    const auto lines=studio_help_wrapped_lines(c,content);
    for(size_t i=0;i<lines.size();++i)c.draw_label(lines[i],32,171+int(i)*21,t.textValue,14);
    for(int i=0;i<3;++i){
        const int x=31+i*195;
        c.panel_rect(x,388,188,21,i==page?t.rowSelection:t.background,
                     with_alpha(t.rowCursor,i==page?.85f:.16f),3);
        c.draw_label(pages[i],x+9,391,i==page?t.textTitle:t.textParam);
    }
    c.draw_label("Left / Right  Page   Up / Down  Topic   A  Quick help   B / Select  Back",24,438,t.textParam,12);
}
}
