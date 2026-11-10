// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone visual contract, linked to the production pt-ui library. No screenshot mocks.
#include "ui/studio.h"
#include "ui/layout.h"
#include "ui/theme_io.h"
#include "studio/punch.h"
#include "studio/synth.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>

using namespace pt::ui;
namespace fs=std::filesystem;
namespace {
std::vector<uint32_t> pixels(const Canvas& c){return {c.pixels(),c.pixels()+640*480};}
void save(const fs::path& p,const Canvas& c){
 std::ofstream f(p,std::ios::binary);f<<"P6\n640 480\n255\n";
 for(int i=0;i<640*480;++i){const auto q=c.pixels()[i];const char rgb[]={char(q>>16),char(q>>8),char(q)};f.write(rgb,3);}assert(f.good());
}
uint64_t hash(const Canvas& c){uint64_t h=1469598103934665603ull;for(int i=0;i<640*480;++i)h=(h^c.pixels()[i])*1099511628211ull;return h;}
}
int main(int argc,char** argv){
 assert(argc==2);const fs::path out=argv[1];fs::create_directories(out);
 const auto themes=theme_builtins();assert(themes.size()==50&&kSurfaceStyleCount==18);
 Canvas canvas;std::set<uint64_t> materials;
 for(int style=0;style<kSurfaceStyleCount;++style){
  canvas.set_surface_style(style);canvas.surface_rect(0,0,640,480,0xFF000000);
  for(int i=0;i<640*480;++i)assert(canvas.pixels()[i]==0xFF000000);
  canvas.surface_rect(0,0,640,480,0xFF243041);materials.insert(hash(canvas));
  const auto first=pixels(canvas);canvas.clear(0xFFFF00FF);canvas.surface_rect(0,0,640,480,0xFF243041);assert(first==pixels(canvas));
  Theme t=themes.back(),roundtrip;t.surfaceStyle=style;assert(parse_theme(serialize_theme(t),roundtrip));assert(t.surfaceStyle==roundtrip.surfaceStyle);
  if(style>=12){save(out/("material-"+std::to_string(style)+".ppm"),canvas);
   for(const auto q:first)for(int shift:{0,8,16})assert(std::abs(int((q>>shift)&255)-int((0xFF243041u>>shift)&255))<=6);
  }
 }
 assert(materials.size()==18);
 auto project=songcore::make_default_project();project.name="BUNKA VISUAL LAB";
 const auto projectBefore=songcore::serialize_project(project);
 AppState s;s.project=&project;s.studioOpen=true;s.studioTab=6;s.theme=themes[32];
 // Retain the previous eight material-theme snapshots, then cover all ten appended palettes.
 for(size_t i=32;i<themes.size();++i){s.theme=themes[i];draw_studio(canvas,s);save(out/("style-"+s.theme.name+".ppm"),canvas);}
 s.theme=themes[32];
 for(int bank=0;bank<8;++bank){
  s.studioTab=3;s.studioPunch=studio::punch_id(bank,bank);s.studioAUsed=true;s.blinkPhaseMs=267;
  draw_studio(canvas,s);save(out/("held-bank-"+std::to_string(bank+1)+".ppm"),canvas);
 }
 s.studioAUsed=false;draw_studio(canvas,s);save(out/"punch-dry.ppm",canvas);
 // Legacy palettes are unchanged by the optional decorative scanner. Held motion is strictly
 // inside the selected pad; released, help and other pages remain eligible for idle frame skip.
 s.theme=themes[8];s.studioPunch=studio::punch_id(5,6);s.studioAUsed=true;s.blinkPhaseMs=0;
 draw_studio(canvas,s);const auto one=pixels(canvas);s.blinkPhaseMs=700;draw_studio(canvas,s);
 int changed=0;const int x=16+2*154,y=166+92;
 for(int py=0;py<480;++py)for(int px=0;px<640;++px)if(one[py*640+px]!=canvas.pixels()[py*640+px]){
  ++changed;if(!(px>=x&&px<x+146&&py>=y&&py<y+81)){std::cerr<<"Unexpected motion at "<<px<<","<<py<<" for pad "<<studio::punch_pad(s.studioPunch)<<" material "<<s.theme.surfaceStyle<<"\n";assert(false);}
 }
 assert(changed>20);s.studioAUsed=false;draw_studio(canvas,s);const auto dry=pixels(canvas);
 s.blinkPhaseMs=0;draw_studio(canvas,s);assert(dry==pixels(canvas));
 TrackerLayout layout;assert(!layout.has_falling_meters(s));s.studioAUsed=true;assert(layout.has_falling_meters(s));
 s.studioHelp=true;assert(!layout.has_falling_meters(s));s.studioHelp=false;s.studioTab=0;assert(!layout.has_falling_meters(s));
 s.theme=themes[33];s.studioAUsed=false;s.studioTab=0;
 open_studio_synth(s,project.instruments[0]);s.studioModel=24;draw_studio(canvas,s);save(out/"synthesis.ppm",canvas);
 s.studioTab=1;draw_studio(canvas,s);save(out/"effects.ppm",canvas);
 s.studioTab=2;draw_studio(canvas,s);save(out/"master.ppm",canvas);
 s.studioTab=5;drumlab_generate(s.drumLab);draw_studio(canvas,s);save(out/"drumlab.ppm",canvas);
 s.studioOpen=false;s.currentScreen=ScreenType::SONG;layout.draw(canvas,s);save(out/"tracker.ppm",canvas);
 assert(songcore::serialize_project(project)==projectBefore);
 std::vector<double> times;times.reserve(400);s.studioOpen=true;s.studioTab=3;s.studioAUsed=true;
 for(int i=0;i<400;++i){s.studioPunch=studio::punch_id(i%8,i%8);s.blinkPhaseMs=i*17%1000;
  const auto start=std::chrono::steady_clock::now();draw_studio(canvas,s);
  times.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());
 }
 std::sort(times.begin(),times.end());
 // Native-frame animation proof: a held grain gesture, bank change, then immediate dry release.
 std::ofstream raw(out/"punch-motion.rgb",std::ios::binary);
 for(int frame=0;frame<96;++frame){s.blinkPhaseMs=frame*1000/24%1000;s.studioPunch=studio::punch_id(frame<48?5:1,frame<48?3:0);s.studioAUsed=frame<72;draw_studio(canvas,s);
  for(int i=0;i<640*480;++i){const auto q=canvas.pixels()[i];const char rgb[]={char(q>>16),char(q>>8),char(q)};raw.write(rgb,3);}
 }assert(raw.good());
 std::ofstream report(out/"visual-qa.json");report<<"{\n  \"ok\": true,\n  \"native_size\": [640,480],\n  \"baseline_theme_count\": 40,\n  \"new_palette_count\": 10,\n  \"snapshot_theme_count\": 18,\n  \"themes\": 50,\n  \"materials\": 18,\n  \"unique_material_hashes\": "<<materials.size()<<",\n  \"pure_black_preserved\": true,\n  \"material_roundtrip_and_cache\": true,\n  \"held_animation_localized\": true,\n  \"dry_animation_stops\": true,\n  \"project_unchanged\": true,\n  \"host_draw_samples\": 400,\n  \"host_draw_us_median\": "<<times[200]<<",\n  \"host_draw_us_p95\": "<<times[380]<<",\n  \"host_draw_us_worst\": "<<times.back()<<",\n  \"performance_scope\": \"Host measurement; not a physical handheld benchmark\"\n}\n";
 std::cout<<"PASS: 50 themes / 18 cached materials, 18 palette snapshots (8 previous + 10 new), exact OLED black, persistence, localized held animation, dry idle, unchanged score; 400 host draws median "<<times[200]<<"us, p95 "<<times[380]<<"us.\n";
}
