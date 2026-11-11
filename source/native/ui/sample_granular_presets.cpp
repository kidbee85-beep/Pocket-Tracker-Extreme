// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/sample_granular_presets.h"
#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace pt::ui {
namespace {
using Parameters=studio::granular::Parameters;
constexpr const char* categories[]={"Atmosphere","Frozen Spaces","Dust / Tape","Pitch Layers","Rhythmic Grains","Micro Percussion","Stereo Drift","Glitch Motion"};
constexpr const char* shortCategories[]={"ATMOS","FREEZE","TAPE","PITCH","RHYTHM","DRUM","STEREO","GLITCH"};
constexpr const char* descriptions[]={
 "Long overlapping grains, slow source motion and soft envelopes.",
 "Freeze the scan, move Position by hand; the sample is preserved.",
 "Short dusty grains, small pitch drift and reverse flecks.",
 "Pitched sample clouds: root, fifths, octaves and gentle detuning.",
 "Regular grain bursts; Density is in Hz, not a tempo-sync claim.",
 "Tiny sample windows and short envelopes for playable percussion.",
 "Wide grain panning, slow scan and opposing source directions.",
 "Scattered positions, reverse probability and abrupt source motion."
};
constexpr const char* names[8][16]={
 {"Velvet Canopy","Glass Loom","Hush Field","Soft Assembly","Paper Aurora","Low Orbit","Moss Circuit","Dawn Raster","Silver Veil","Warm Horizon","Quiet Vectors","Cloud Harbour","Slow Bloom","Linen Signal","Moon Weave","Endless Room"},
 {"Held Smoke","Glass Suspension","Still Water","Locked Mist","Frozen Prism","Amber Memory","Fixed Horizon","Violet Hold","Paused Garden","Static Velvet","Crystal Breath","Suspended Dust","Deep Frame","Quiet Orbit","Last Reflection","Infinite Postcard"},
 {"Dust Reel","Bent Shellac","Flutter Pocket","Worn Cassette","Loose Capstan","Needle Haze","Faded Print","Spliced Air","Warm Dropout","Secondhand Grain","Dusty Fifth","VHS Flecks","Reel Shadow","Old Copper","Slow Spool","Tape Weather"},
 {"Lower Octave","Low Fifth","Root Halo","Fifth Mist","Octave Bloom","Upper Fifth","Two Octaves","Sub Cloud","Fourth Light","High Fourth","Second Drift","High Second","Sixth Glass","High Sixth","Falling Fourth","Minor Third"},
 {"Woven Pulse","Grain Ladder","Click Lattice","Pocket Motor","Small Ratchet","Dotted Thread","Tight Relay","Window Train","Pulse Braids","Tumbling Steps","Mechanical Rain","Busy Loom","Sparse Ticks","Signal Patches","Dry Carousel","Raster Groove"},
 {"Needle Click","Tiny Knocker","Paper Hat","Metal Speck","Wood Packet","Dust Snare","Soft Rim Grain","Wire Tap","Dry Shaker","Micro Conga","Glass Tick","Stone Chip","Brush Fragments","Pixel Clap","Loose Bolts","Grain Tom"},
 {"Open Wings","Left Bank","Right Current","Sideways Haze","Wide Copper","Floating Pair","Twin Horizon","Drifting Rails","Tilted Room","Orbit Threads","Pan Garden","Distant Edges","Passing Lights","Stereo Ribbons","Crossing Dust","Broad Sky"},
 {"Buffer Confetti","Reverse Sparks","Broken Shuttle","Packet Storm","Scattered Tape","Staccato Glass","Xeno Mosaic","Wild Raster","Cut-Up Weather","Byte Sprinkles","Backward Rain","Short Circuit","Pixel Swerve","Lost Reels","Fractured Air","Mirror Fragments"}
};
Parameters settings(int family,int v){
 Parameters p;p.position=.12f+.045f*v;p.level=.42f;
 switch(family){
 case 0:p.sizeMs=180+27*v;p.densityHz=12+2*(v%4);p.sprayMs=25+9*v;p.pitchSpraySemitones=.1f+.05f*(v%5);p.scanSpeed=.04f+.02f*v;p.panSpread=.4f+.03f*v;p.attackMs=150+40*v;p.releaseMs=600+80*v;break;
 case 1:p.position=.08f+.05f*v;p.freeze=true;p.sizeMs=240+29*v;p.densityHz=12+2*(v%3);p.sprayMs=3+2*v;p.pitchSpraySemitones=.04f*(v%8);p.panSpread=.55f+.02f*v;p.attackMs=120;p.releaseMs=1400+100*v;p.level=.44f;break;
 case 2:p.sizeMs=24+5*v;p.densityHz=25+5*(v%8);p.sprayMs=30+12*v;p.scanSpeed=.4f+.04f*v;p.reverseProbability=.03f*(v%6);p.pitchSpraySemitones=.08f*v;p.pitchSemitones=v==10?7:v==14?-12:0;p.panSpread=.15f+.025f*v;p.attackMs=3;p.releaseMs=120+10*v;break;
 case 3:{static constexpr int semitones[]={-12,-7,0,7,12,19,24,-24,5,17,2,14,9,21,-5,3};p.pitchSemitones=semitones[v];p.sizeMs=90+14*v;p.densityHz=14+3*(v%5);p.reverseProbability=.2f*(v%3);p.pitchSpraySemitones=.15f*(v%4);p.scanSpeed=v%2?.1f:-.1f;p.panSpread=.5f+.12f*(v%4);p.attackMs=60;p.releaseMs=800;p.level=.37f;break;}
 case 4:p.sizeMs=12+4*v;p.densityHz=8+8*(v%8);p.scanSpeed=1;p.sprayMs=4*(v%8);p.windowShape=.6f;p.panSpread=.1f+.04f*v;p.attackMs=1;p.releaseMs=100;p.level=.5f;break;
 case 5:p.sizeMs=3+2*v;p.densityHz=5+7*(v%8);p.sprayMs=2*(v%6);p.scanSpeed=v%2?.5f:0;p.pitchSemitones=(v%5-2)*3;p.pitchSpraySemitones=.08f*v;p.panSpread=.06f*v;p.attackMs=1+v%4;p.releaseMs=35+15*v;p.windowShape=.9f;p.level=.48f;break;
 case 6:p.sizeMs=140+24*v;p.densityHz=10+2*(v%5);p.panSpread=.7f+.02f*v;p.reverseProbability=.1f*(v%5);p.scanSpeed=-.45f+.06f*v;p.windowShape=.15f*(v%6);p.pitchSpraySemitones=.2f+.04f*v;p.sprayMs=15+7*v;p.attackMs=80;p.releaseMs=700;p.level=.36f;break;
 default:{static constexpr float scan[]={-2,-1,-.5f,0,.5f,1,2,4};p.sizeMs=8+7*v;p.densityHz=35+8*(v%8);p.sprayMs=80+35*v;p.pitchSpraySemitones=.2f*v;p.reverseProbability=.2f+.05f*v;p.scanSpeed=scan[v%8];p.freeze=v%5==0;p.panSpread=.2f+.05f*v;p.windowShape=1;p.attackMs=1;p.releaseMs=80+8*v;p.level=.38f;break;}
 }
 return p;
}
struct Field {const char* name;float Parameters::*member;float minimum,maximum;};
constexpr Field fields[]={
 {"position",&Parameters::position,0,1},{"sizeMs",&Parameters::sizeMs,2,1000},{"densityHz",&Parameters::densityHz,0,120},{"sprayMs",&Parameters::sprayMs,0,1000},
 {"pitchSemitones",&Parameters::pitchSemitones,-48,48},{"pitchSpraySemitones",&Parameters::pitchSpraySemitones,0,24},{"scanSpeed",&Parameters::scanSpeed,-4,4},
 {"panSpread",&Parameters::panSpread,0,1},{"reverseProbability",&Parameters::reverseProbability,0,1},{"windowShape",&Parameters::windowShape,0,1},
 {"level",&Parameters::level,0,2},{"attackMs",&Parameters::attackMs,0,5000},{"releaseMs",&Parameters::releaseMs,1,10000}
};
}
const SampleGranularPreset& sample_granular_factory_preset(int index){
 static const auto presets=[](){std::array<SampleGranularPreset,kSampleGranularFactoryCount> result{};
  for(int bank=0;bank<8;++bank)for(int v=0;v<16;++v)result[bank*16+v]={names[bank][v],descriptions[bank],bank,settings(bank,v)};return result;}();
 return presets[std::clamp(index,0,kSampleGranularFactoryCount-1)];
}
const char* sample_granular_category(int category){return categories[std::clamp(category,0,7)];}
const char* sample_granular_category_short(int category){return shortCategories[std::clamp(category,0,7)];}
bool sample_granular_relative_reference(const std::string& path){
 if(path.empty()||path.size()>4096||path.front()=='/'||path.front()=='\\'||path.find(':')!=std::string::npos||path.find('\\')!=std::string::npos||path.find('\0')!=std::string::npos)return false;
 size_t at=0;
 while(at<path.size()){
  const auto end=path.find('/',at);const auto part=path.substr(at,end==std::string::npos?path.size()-at:end-at);
  if(part.empty()||part=="."||part=="..")return false;
  if(end==std::string::npos)return true;at=end+1;
 }
 return false;
}
bool parse_sample_granular_preset(const std::string& text,SampleGranularPresetFile& output,std::string& error){
 const auto j=nlohmann::json::parse(text,nullptr,false);
 if(!j.is_object()||!j.contains("version")||!j["version"].is_number_integer()||j["version"]!=1||j.value("kind",nlohmann::json{})!="sample-granular"||!j.contains("parameters")||!j["parameters"].is_object()){
  error="Not a version1 sample-granular preset";return false;
 }
 SampleGranularPresetFile next;const auto& p=j["parameters"];
 for(const auto& field:fields){
  if(!p.contains(field.name)||!p[field.name].is_number()){error=std::string("Missing/invalid ")+field.name;return false;}
  const auto value=p[field.name].get<double>();
  if(!std::isfinite(value)||value<field.minimum||value>field.maximum){error=std::string("Out of range: ")+field.name;return false;}
  next.parameters.*(field.member)=static_cast<float>(value);
 }
 if(!p.contains("freeze")||!p["freeze"].is_boolean()){error="Freeze must be a boolean";return false;}
 next.parameters.freeze=p["freeze"].get<bool>();
 if(j.contains("seed")){
  if(!j["seed"].is_number_integer()||j["seed"]<1||j["seed"]>std::numeric_limits<uint32_t>::max()){error="Invalid granular seed";return false;}
  next.seed=j["seed"].get<uint32_t>();
 }
 if(j.contains("name")){
  if(!j["name"].is_string()||j["name"].get_ref<const std::string&>().size()>96){error="Invalid preset name";return false;}
  next.name=j["name"].get<std::string>();
 }
 if(j.contains("sample")){
  if(!j["sample"].is_string()||!sample_granular_relative_reference(j["sample"].get<std::string>())){error="Sample reference must stay relative to this preset";return false;}
  next.sampleReference=j["sample"].get<std::string>();next.hasSample=true;
 }
 output=std::move(next);error.clear();return true;
}
std::string serialize_sample_granular_preset(const SampleGranularPresetFile& preset){
 nlohmann::json p=nlohmann::json::object();for(const auto& field:fields)p[field.name]=preset.parameters.*(field.member);p["freeze"]=preset.parameters.freeze;
 nlohmann::json j={{"version",1},{"kind","sample-granular"},{"parameters",p}};
 if(!preset.name.empty())j["name"]=preset.name;
 if(preset.hasSample){j["sample"]=preset.sampleReference;j["seed"]=preset.seed;}
 return j.dump(2);
}
std::string sample_granular_factory_json(int index){
 const auto& factory=sample_granular_factory_preset(index);SampleGranularPresetFile p;p.name=factory.name;p.parameters=factory.parameters;return serialize_sample_granular_preset(p);
}
} // namespace pt::ui
