#include "ui/studio.h"
#include "ui/studio_navigation.h"
#include "ui/studio_help.h"
#include "ui/button_mapper.h"
#include "ui/input_dispatcher.h"
#include "ui/settings_store.h"
#include "ui/settings_row_layout.h"
#include "ui/help_text.h"
#include "ui/live_resample.h"
#include "ui/std_filesystem.h"
#include "studio/punch.h"
#include "songcore/project_io.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace {
using namespace pt::ui;
struct Input {
 AppState& s;songcore::SongcoreHost& host;AudioEngine& engine;FileSystem& fs;
 InputDispatcher dispatch;MapperState mapper;ButtonMods held;uint64_t now=1000;
 Input(AppState& state,songcore::SongcoreHost& h,AudioEngine& e,FileSystem& f)
  :s(state),host(h),engine(e),fs(f),dispatch(s,h,f){}
 void modifier(Button b,bool down){
  if(b==Button::A)held.a=down;if(b==Button::B)held.b=down;
  if(b==Button::L_SHIFT)held.l=down;if(b==Button::R_SHIFT)held.r=down;
  if(b==Button::SELECT)held.select=down;
 }
 void event(Button b,ButtonAction action,bool cancelled=false){
  now+=20;dispatch.set_now(now);ButtonEvent e{b,action,held,cancelled};
  if(is_live_resample_chord(e)&&action==ButtonAction::PRESSED)mapper.shoulderClearPending=false;
  if(!studio_button(e,s,host,engine,fs))handle_button(e,dispatch,mapper,now);
 }
 void press(Button b){modifier(b,true);event(b,ButtonAction::PRESSED);}
 void repeat(Button b){event(b,ButtonAction::PRESSED);}
 void release(Button b){modifier(b,false);event(b,ButtonAction::RELEASED);}
 void cancel(Button b){modifier(b,false);event(b,ButtonAction::RELEASED,true);}
 void tap(Button b){press(b);release(b);}
};
bool contains(const StudioHelpContent& c,const char* text){
 for(const auto& line:c.lines)if(line.find(text)!=std::string::npos)return true;return false;
}
std::string bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
}

int main(int argc,char** argv){
 using namespace pt::ui;
 assert(argc==2);const auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
 StdFileSystem fs((root/"data").string());auto engine=std::make_unique<AudioEngine>();
 engine->setDeviceSampleRate(48000);songcore::SongcoreHost host(engine.get(),48000);
 assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
 const auto originalProject=songcore::serialize_project(host.project());
 std::filesystem::create_directories(root/"data/Presets/User");
 std::ofstream(root/"data/config.json")<<"{\"controls\":\"retain\"}";
 std::ofstream(root/"data/Presets/User/keep.json")<<"private preset sentinel";
 const auto originalConfig=bytes(root/"data/config.json"),originalPreset=bytes(root/"data/Presets/User/keep.json");

 // Stable public-facing descriptors and saved SettingsRow identities.
 const int ids[]={0,1,2,3,4,5,7};const char* names[]={"Synth","FX","Master","Punch","Record","Drumlab","Sources"};
 for(size_t i=0;i<kStudioMusicTabs.size();++i){assert(kStudioMusicTabs[i].id==ids[i]);assert(std::string(kStudioMusicTabs[i].label)==names[i]);}
 static_assert(static_cast<int>(SettingsRow::THEME)==9);
 static_assert(static_cast<int>(SettingsRow::AUDIO_OUT)==18);
 static_assert(static_cast<int>(SettingsRow::APPEARANCE)==19);
 assert(!studio_is_music_tab(6));std::set<int> settingIds;
 for(auto row:SETTINGS_DISPLAY_ORDER)assert(settingIds.insert(static_cast<int>(row)).second);
 assert(settingIds.size()==20);
 assert(detail::settings_cell_topic(19,1)==HelpTopic::SET_APPEARANCE);

 AppState s;s.project=&host.edit_project();s.studioOpen=true;s.studioTab=3;
 Input in(s,host,*engine,fs);
 // Real first-press / repeated PRESSED / release sequence: one tab per chord.
 in.press(Button::B);in.press(Button::DPAD_RIGHT);assert(s.studioTab==4);
 for(int n=0;n<20;++n)in.repeat(Button::DPAD_RIGHT);
 assert(s.studioTab==4&&s.studioOpen);
 in.release(Button::DPAD_RIGHT);in.press(Button::DPAD_RIGHT);assert(s.studioTab==5);
 in.release(Button::DPAD_RIGHT);in.press(Button::DPAD_RIGHT);assert(s.studioTab==7);
 in.release(Button::DPAD_RIGHT);in.press(Button::DPAD_RIGHT);assert(s.studioTab==0);
 in.release(Button::DPAD_RIGHT);in.release(Button::B);assert(s.studioOpen);
 // Reverse travel skips the settings-only Appearance ID too.
 in.press(Button::B);in.tap(Button::DPAD_LEFT);assert(s.studioTab==7);
 in.tap(Button::DPAD_LEFT);assert(s.studioTab==5);in.release(Button::B);
 // Ending B ends the chord; there is no delayed Back on its release.
 in.press(Button::B);in.press(Button::DPAD_RIGHT);assert(s.studioTab==7);
 in.release(Button::B);assert(s.studioOpen);in.release(Button::DPAD_RIGHT);

 // The section menu opens without leaving Studio or opening Help. Held
 // navigation buttons are owned by the menu until their physical release.
 s.studioTab=0;
 in.press(Button::B);in.press(Button::SELECT);
 assert(s.studioWorkspace.open&&!s.studioHelp&&s.studioWorkspace.selected==0);
 in.release(Button::B);in.release(Button::SELECT);
 in.tap(Button::DPAD_DOWN);in.tap(Button::DPAD_DOWN);
 in.press(Button::DPAD_DOWN);assert(s.studioWorkspace.selected==3);
 {
  Canvas preview;
  draw_studio(preview,s);
  std::ofstream image(root/"studio-sections.ppm",std::ios::binary);
  image<<"P6\n640 480\n255\n";
  for(int i=0;i<640*480;++i){
   const auto pixel=preview.pixels()[i];
   const char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};
   image.write(rgb,3);
  }
 }
 in.tap(Button::A);
 assert(!s.studioWorkspace.open&&s.studioTab==3&&s.studioOpen);
 const int pad=s.studioPunch;
 in.repeat(Button::DPAD_DOWN);in.release(Button::DPAD_DOWN);
 assert(s.studioPunch==pad&&s.studioInputSuppress==0);
 in.press(Button::B);in.tap(Button::SELECT);in.release(Button::B);
 assert(s.studioWorkspace.open&&!s.studioHelp);
 in.tap(Button::DPAD_DOWN);in.tap(Button::B);
 assert(!s.studioWorkspace.open&&s.studioTab==3&&s.studioOpen);
 assert(songcore::serialize_project(host.project())==originalProject);
 std::puts("PASS Studio section menu selection, cancellation and held-button isolation");

 s.studioTab=3;s.studioPunch=studio::punch_id(0,0);
 in.tap(Button::R_SHIFT);assert(s.studioTab==3&&studio::punch_bank(s.studioPunch)==1);
 in.tap(Button::DPAD_DOWN);assert(s.studioTab==3&&studio::punch_pad(s.studioPunch)==4);
 in.press(Button::A);assert(engine->studioRack.punch==s.studioPunch);
 in.press(Button::B);in.press(Button::DPAD_RIGHT);
 assert(s.studioTab==3&&engine->studioRack.punch==s.studioPunch);
 in.release(Button::A);assert(engine->studioRack.punch==0);
 for(int n=0;n<3;++n)in.repeat(Button::DPAD_RIGHT);
 assert(s.studioTab==3);in.release(Button::DPAD_RIGHT);in.release(Button::B);
 // Modal help consumes its own closing release and never starts Record.
 in.press(Button::A);in.tap(Button::SELECT);assert(s.studioHelp&&engine->studioRack.punch==0);
 in.release(Button::A);in.tap(Button::B);assert(!s.studioHelp&&s.studioOpen&&s.studioTab==3);
 assert(contains(studio_help_content(s,3,1),"B + Left / Right"));
 assert(contains(studio_help_content(s,7,1),"B + Left / Right"));

 // Both shoulder orders must preserve a held Punch while the recorder claims Start.
 for(const Button first:{Button::L_SHIFT,Button::R_SHIFT}){
  const Button second=first==Button::L_SHIFT?Button::R_SHIFT:Button::L_SHIFT;
  const int chosen=s.studioPunch;
  in.press(Button::A);in.press(first);in.press(second);
  assert(s.studioPunch==chosen&&engine->studioRack.punch==chosen);
  in.tap(Button::START);
  assert(s.resampleView.active&&engine->studioRack.punch==chosen);
  in.release(second);in.release(first);
  assert(s.studioPunch==chosen&&engine->studioRack.punch==chosen);
  live_resample_stop(s,*engine,fs);
  in.release(Button::A);
 }
 s.studioOpen=false;s.currentScreen=ScreenType::SONG;
 s.selection.active=true;
 in.press(Button::L_SHIFT);in.press(Button::R_SHIFT);in.tap(Button::START);
 assert(s.resampleView.active&&s.selection.active);
 in.release(Button::R_SHIFT);in.release(Button::L_SHIFT);
 assert(s.selection.active&&!in.mapper.shoulderClearPending);
 live_resample_stop(s,*engine,fs);
 in.press(Button::R_SHIFT);in.press(Button::L_SHIFT);
 assert(s.selection.active);
 in.cancel(Button::R_SHIFT);in.cancel(Button::L_SHIFT);
 assert(s.selection.active&&!in.mapper.shoulderClearPending);
 in.press(Button::R_SHIFT);in.press(Button::L_SHIFT);
 assert(s.selection.active);
 in.release(Button::L_SHIFT);in.release(Button::R_SHIFT);
 assert(!s.selection.active&&!in.mapper.shoulderClearPending);
 std::puts("PASS L+R+Start keeps tracker selection and Punch bank/effect; plain L+R clears once on release");

 // Enter Appearance through the actual Settings A route, not a direct helper.
 s.studioOpen=false;s.currentScreen=ScreenType::SETTINGS;s.settingsReturnScreen=ScreenType::PHRASE;
 s.settingsCursorRow=19;s.settingsCursorColumn=1;s.studioTab=7;
 in.now+=400;in.press(Button::A);assert(s.studioOpen&&s.studioAppearanceSettings&&s.studioTab==6);
 in.release(Button::A);assert(s.studioTab==6&&!s.studioConfirm);
 const auto settingsRow=s.settingsCursorRow,settingsColumn=s.settingsCursorColumn;
 in.press(Button::B);in.tap(Button::DPAD_RIGHT);in.release(Button::B);
 assert(s.studioOpen&&s.studioAppearanceSettings&&s.studioTab==6);
 in.tap(Button::SELECT);assert(s.studioHelp&&s.studioHelpTopic==6);
 in.tap(Button::B);assert(!s.studioHelp&&s.studioOpen&&s.studioAppearanceSettings);
 assert(contains(studio_help_content(s,6,1),"return to Settings"));
 in.tap(Button::B);
 assert(!s.studioOpen&&!s.studioAppearanceSettings&&s.studioTab==7);
 assert(s.currentScreen==ScreenType::SETTINGS&&s.settingsCursorRow==settingsRow&&s.settingsCursorColumn==settingsColumn);
 // A separate, next Back gesture leaves Settings to its original return screen.
 in.tap(Button::B);assert(s.currentScreen==ScreenType::PHRASE);

 // Appearance values use existing settings serialization; no project/control/preset writes.
 s.currentScreen=ScreenType::SETTINGS;s.settingsCursorRow=19;s.settingsCursorColumn=1;
 in.now+=400;in.tap(Button::A);assert(s.studioAppearanceSettings);
 s.studioRow=2;const int oldFont=s.settings.fontStyle;
 in.press(Button::A);in.tap(Button::DPAD_RIGHT);in.release(Button::A);assert(s.settings.fontStyle!=oldFont);
 const auto selectedTheme=s.theme;const auto selectedSettings=s.settings;
 assert(save_settings(fs,selectedSettings,selectedTheme));
 SettingsValues loadedSettings;Theme loadedTheme;
 assert(load_settings(fs,loadedSettings,loadedTheme));
 assert(loadedSettings.fontStyle==selectedSettings.fontStyle&&loadedTheme.name==selectedTheme.name&&loadedTheme.surfaceStyle==selectedTheme.surfaceStyle);
 assert(bytes(root/"data/config.json")==originalConfig&&bytes(root/"data/Presets/User/keep.json")==originalPreset);
 assert(songcore::serialize_project(host.project())==originalProject);
 in.tap(Button::B);assert(s.currentScreen==ScreenType::SETTINGS&&!s.studioOpen);
 // Explicit Studio shortcuts also leave the Settings-only context cleanly.
 in.now+=400;in.tap(Button::A);assert(s.studioAppearanceSettings);
 in.press(Button::R_SHIFT);in.press(Button::SELECT);in.release(Button::SELECT);in.release(Button::R_SHIFT);
 assert(!s.studioOpen&&!s.studioAppearanceSettings&&s.studioTab==7&&s.currentScreen==ScreenType::SETTINGS);
 assert(!s.studioHelp&&!s.helpOpen&&!s.helpFull);
 in.now+=400;in.tap(Button::A);assert(s.studioAppearanceSettings);
 in.press(Button::L_SHIFT);in.press(Button::SELECT);in.release(Button::SELECT);in.release(Button::L_SHIFT);
 assert(s.studioOpen&&!s.studioAppearanceSettings&&s.studioTab==3&&s.currentScreen==ScreenType::SETTINGS);
 in.tap(Button::B);assert(!s.studioOpen&&s.currentScreen==ScreenType::SETTINGS);
 assert(songcore::serialize_project(host.project())==originalProject);
 std::cout<<"PASS: seven stable music tabs, held-chord repeat latch, release safety, local Punch banks, settings-only Appearance entry/back/help, stable row IDs and settings/control/project preservation\n";
}
