// Host integration: GPL-3.0-or-later. Mutable DSP retains Emilie Gillet's MIT notices.
#include "synth.h"
#include "synth_extra.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
// Each firmware's generated resources use global macro names. Its inline DSP is
// parsed with its own values; discard conflicting names before the next module.
#include "plaits/dsp/voice.h"
#undef LUT_FM_FREQUENCY_QUANTIZER
#undef LUT_FM_FREQUENCY_QUANTIZER_SIZE
#include "braids/macro_oscillator.h"
#undef LUT_FM_FREQUENCY_QUANTIZER
#undef LUT_SINE_SIZE
#undef LUT_SVF_SHIFT
#undef LUT_STIFFNESS
#undef LUT_STIFFNESS_SIZE
#include "rings/dsp/part.h"
#undef LUT_SINE_SIZE
#undef LUT_4_DECADES
#undef LUT_STIFFNESS
#undef LUT_ENV_EXPO
#undef LUT_ENV_EXPO_SIZE
#undef LUT_FM_FREQUENCY_QUANTIZER
#undef LUT_SVF_SHIFT
#include "elements/dsp/part.h"
namespace studio {
namespace {
constexpr int N=24;
const char* plaitsNames[]={"P VCF","P PHASE DIST","P DX BANK A","P DX BANK B","P DX BANK C","P WAVE TERRAIN","P STRING MACHINE","P CHIPTUNE","P VIRTUAL ANALOG","P WAVESHAPER","P FM","P GRAINS","P ADDITIVE","P WAVETABLE","P CHORD","P SPEECH","P SWARM","P NOISE","P PARTICLES","P STRING","P MODAL","P KICK","P SNARE","P HI HAT"};
const char* ringsNames[]={"R MODAL","R SYMPATHETIC","R STRING","R FM","R QUANT STRINGS","R STRING REVERB"};
const char* elementsNames[]={"E MODAL","E STRING","E CHORD","E OMINOUS"};
constexpr int braidsCount=static_cast<int>(braids::MACRO_OSC_SHAPE_LAST);
constexpr int legacyModels = 24 + braidsCount + 6 + 4;
constexpr int elementsProfiles = 12;
constexpr int additionalModels = 34;
static_assert(legacyModels == 82, "Existing model IDs must remain stable");
static_assert(legacyModels + additionalModels <= 128, "Custom URI has a seven-bit model field");
const char* additionalNames[] = {
 "E BOWED PLATE", "E AIR PLATE", "E MALLET PLATE", "E HYBRID PLATE",
 "E BOWED STRING", "E AIR STRING", "E PLUCK STRING", "E HYBRID STRING",
 "E BOWED CHORD", "E AIR CHORD", "E STRUCK CHORD", "E HYBRID CHORD",
 "G TAPE CLOUD", "G SHIMMER CLOUD", "G REVERSE DUST", "G CHORD SWARM",
 "G FORMANT GRAINS", "G SPRAY CLOUD", "G RESONANT GRAINS", "G STUTTER CLOUD",
 "S SUPER SAW", "S VECTOR", "S PHASE FM", "S RING BANK", "S ADDITIVE TILT",
 "S KARPLUS NYLON", "S BOWED COMB", "S FORMANT VOWEL", "S HARD SYNC", "S WAVEFOLD",
 "S OCTAVE ORBIT", "S GRAVITY BOUNCE", "S ORBIT BELLS", "S GRAVITY RAIN"
};
static_assert(sizeof(additionalNames) / sizeof(*additionalNames) == additionalModels);
static_assert(kGravityFirstModel + kGravityModelCount == legacyModels + additionalModels);
static_assert(kOctaveOrbitModel + 1 == kGravityFirstModel);
constexpr int extendedFlag = 1 << 29;
const char* variants[]={"WARM","GLASS","DARK","AIR","PLUCK","BELL","HOLLOW","SOFT","ORGAN","METALLIC","THIN","MOTION"};
constexpr float macroA[]={.35f,.72f,.17f,.88f,.28f,.61f,.12f,.47f,.78f,.93f,.08f,.56f};
constexpr float macroB[]={.42f,.63f,.24f,.77f,.21f,.82f,.54f,.16f,.38f,.94f,.69f,.46f};
constexpr float macroC[]={.50f,.23f,.75f,.60f,.14f,.37f,.85f,.29f,.68f,.48f,.11f,.92f};
struct NamedPreset { const char* name; std::array<int, 3> macros; };
constexpr NamedPreset kOrbitPresets[] = {
    {"Slow Rise",     {162, 186, 102}}, {"Slow Descent", { 90, 186, 102}},
    {"Deep Drift",    {109, 204, 181}}, {"Still Choir",  {128, 255,  75}},
    {"Gravity Keys",  {136, 107, 232}}, {"Crystal Lift", {183, 112,  44}},
    {"Falling Glass", { 68, 104,  51}}, {"Low Orbit",    {120, 161, 251}},
    {"Wide Halo",     {150, 254,  19}}, {"High Spiral",  {224, 238,  32}},
    {"Falling Debris",{ 16, 209,  49}}, {"Fast Ascent",  {255, 161,   8}},
};
static_assert(std::size(kOrbitPresets) == std::size(variants));
constexpr NamedPreset kGravityPresets[kGravityModelCount][std::size(variants)] = {
    {{"Pinball Keys", {164,192, 96}}, {"Rubber Mallet", {111,165, 38}},
     {"Drop Counter", {202, 32, 65}}, {"Coin Cascade",  {237,222,184}},
     {"Low Rebound",  { 78,144, 13}}, {"Copper Spring", {175,255,137}},
     {"Slow Hopper",  { 33,186, 80}}, {"Moon Drop",     { 12,102,214}},
     {"Loose Marbles",{209,126,213}}, {"Skitter",        {255,234,246}},
     {"One Pitch",    {125,205,  0}}, {"Glass Floor",   {157, 64,239}}},
    {{"Clock Bells",  {119, 75,154}}, {"Dorian Moon",    { 65,160,200}},
     {"Tidal Clock",  { 88,239, 96}}, {"Two Satellites",{164,207,130}},
     {"Slow Halo",    { 26,137,231}}, {"Bright Crossing",{192,171,185}},
     {"Pendulum",     {126,249, 63}}, {"Soft Ellipse",   { 56, 48,163}},
     {"Bell Dial",    {185, 15, 40}}, {"Comet Tail",     {239,233,255}},
     {"Single Moon",  {111,162,  0}}, {"Fast Eclipse",   {255,255,219}}},
    {{"Tin Roof",     {135,164, 96}}, {"Light Shower",  { 58, 26,145}},
     {"Metal Drizzle",{104,108,187}}, {"Spring Rain",   { 79,142,205}},
     {"Low Weather",  { 36,173, 36}}, {"Hail Bells",    {196,198,247}},
     {"Window Drops", { 43, 76,113}}, {"Chime Mist",    { 21,216,254}},
     {"Tin Spouts",   {154,233, 59}}, {"Glass Storm",   {246,238,229}},
     {"Mono Rain",    {123,173,  0}}, {"Cloudburst",    {255,255,255}}},
};

const NamedPreset* named_preset(int model, int variation) {
    if (model == kOctaveOrbitModel) return &kOrbitPresets[variation];
    if (is_gravity_model(model)) return &kGravityPresets[model - kGravityFirstModel][variation];
    return nullptr;
}

constexpr const char* kGravityScaleNames[] = {"Minor pentatonic", "Major pentatonic", "Dorian", "Chromatic"};
constexpr int kGravityScaleValues[] = {0, 85, 170, 255};
int gravity_scale_index(int value) { return std::clamp(value, 0, 255) * 4 / 256; }
int gravity_default_scale(int model) {
    return model == kGravityFirstModel + 1 ? 2 : model == kGravityFirstModel + 2 ? 1 : 0;
}
bool factory_code_valid(int code) {
 if(code<0 || (code&(1<<30)))return false;
 if(code&extendedFlag)return (code&~(extendedFlag|0x7ff))==0 && ((code>>4)&127)<model_count() && (code&15)>=4 && (code&15)<12;
 return code<model_count()*variations;
}
}
static const char* braidsNames[]={"B CSAW","B MORPH","B SAW SQUARE","B SINE TRIANGLE","B BUZZ","B SQUARE SUB","B SAW SUB","B SQUARE SYNC","B SAW SYNC","B TRIPLE SAW","B TRIPLE SQUARE","B TRIPLE TRIANGLE","B TRIPLE SINE","B TRIPLE RING MOD","B SAW SWARM","B SAW COMB","B TOY","B DIGITAL FILTER LP","B DIGITAL FILTER PK","B DIGITAL FILTER BP","B DIGITAL FILTER HP","B VOSIM","B VOWEL","B VOWEL FOF","B HARMONICS","B FM","B FEEDBACK FM","B CHAOTIC FEEDBACK FM","B PLUCKED","B BOWED","B BLOWN","B FLUTED","B STRUCK BELL","B STRUCK DRUM","B KICK","B CYMBAL","B SNARE","B WAVETABLES","B WAVE MAP","B WAVE LINE","B WAVE PARAPHONIC","B FILTERED NOISE","B TWIN PEAKS NOISE","B CLOCKED NOISE","B GRANULAR CLOUD","B PARTICLE NOISE","B DIGITAL MODULATION","B QUESTION MARK"};
int model_count(){return legacyModels + additionalModels;}
bool synth_needs_strike(int model){
 return (model>=2&&model<=4)||(model>=19&&model<=23)||model==52||(model>=56&&model<=60)||
        (model>=72&&model<=81)||(model>=82&&model<94&&(model-82)%4==2)||model==107;
}
const char* model_name(int m){
 if(m<0||m>=model_count())return "UNKNOWN";
 if(m>=legacyModels)return additionalNames[m-legacyModels];
 if(m<24)return plaitsNames[m];m-=24;
 if(m<braidsCount)return braidsNames[m];m-=braidsCount;
 if(m<6)return ringsNames[m];return elementsNames[m-6];
}
int macro_count(int model){
 if(model<0||model>=model_count())return 0;
 return model>=24&&model<24+braidsCount?2:3;
}
const char* macro_name(int model,int index){
 if(index<0||index>=macro_count(model))return "";
 // Names follow the parameter derivations in the corresponding Plaits engine*/..._engine.cc.
 // Several macros deliberately span two functions; both are named rather than implying an
 // independent control that the firmware does not expose.
 static const char* plaits[24][3]={
  {"Resonance / slope","Cutoff","Wave / sub"},
  {"Ratio","PD amount","Pulse width"},
  {"DX patch","Brightness","Envelope"},{"DX patch","Brightness","Envelope"},{"DX patch","Brightness","Envelope"},
  {"Terrain","Radius","Offset"},{"Chord","Filter / ensemble","Registration"},{"Chord","Arp / inversion","Waveform"},
  {"Detune","Square / sync","Saw shape"},{"Wave shape","Fold amount","Pulse / slope"},
  {"FM ratio","FM index","Feedback"},{"Ratio / bleed","Formant","Grain shape"},
  {"Spectral bumps","Centroid","Slope"},{"Table Z","Table X","Table Y"},
  {"Chord","Inversion","Registration"},{"Speech model","Formant shift","Vowel / word"},
  {"Spread","Density","Grain size"},{"Mode / cutoff","Clock","Resonance"},
  {"Spread","Density","Diffusion / Q"},{"Structure","Brightness","Decay"},
  {"Structure","Brightness","Damping"},{"FM / drive","Tone","Decay"},
  {"Snappy","Tone","Decay"},{"Noisiness","Tone","Decay"}
 };
 static const char* braids[]={"Timbre","Color"};
 static const char* rings[]={"Structure","Brightness","Damping"};
 static const char* elements[]={"Geometry","Brightness","Damping"};
 static const char* extras[][3]={
  {"Source color","Grain size","Spread / density"}, // Tape cloud
  {"Source color","Grain size","Spread / density"}, // Shimmer cloud
  {"Source color","Grain size","Spread / density"}, // Reverse dust
  {"Source color","Grain size","Spread / density"}, // Chord swarm
  {"Formant","Grain size","Pitch spread"},          // Formant grains: density follows the note
  {"Source color","Grain size","Spread / density"}, // Spray cloud
  {"Partial spread","Grain size","Spread / density"},
  {"Read position","Grain size","Gap / spread"},    // Stutter: C sets a gap, not overlap density
  {"Detune","Side level","Sub level"},
  {"Shape X","Shape Y","Phase blend"},
  {"Ratio","Index","Feedback"},
  {"Ring ratio 1","Ring ratio 2","Ring blend"},
  {"Rolloff","Even harmonics","Phase warp"},
  {"Decay","Brightness","Dispersion"},
  {"Bow speed","Friction","Brightness"},
  {"Formant 1","Formant 2","Upper formant"},
  {"Sync ratio","Wave blend","Pulse width"},
  {"Fold amount","Wave blend","Symmetry"},
  {"Glide rate","Octave spread","Root anchor"},
  {"Gravity","Rebound","Pitch spread"},
  {"Orbit speed","Eccentricity","Pitch spread"},
  {"Gravity","Rain density","Pitch spread"}
 };
 if(model<24)return plaits[model][index];
 if(model<24+braidsCount){
  // Braids digital_oscillator.cc: parameter0 is FM depth, parameter1 modulator frequency.
  if(model>=49&&model<=51)return index==0?"FM index":"FM ratio";
  if(model==46)return index==0?"Vowel":"Formant shift";
  if(model==48)return index==0?"Harmonic peak":"Spectral width";
  if(model==52)return index==0?"Decay / loss":"Pick position";
  if(model==56)return index==0?"Partial balance":"Inharmonicity";
  if(model==57)return index==0?"Partial balance":"Noise / cutoff";
  if(model==58)return index==0?"Decay":"Tone";
  if(model==59)return index==0?"Cutoff":"Metal / noise";
  if(model==60)return index==0?"Tone balance":"Snappy / decay";
  if(model==61)return index==0?"Table position":"Table bank";
  if(model==62)return index==0?"Wave X":"Wave Y";
  if(model==63)return index==0?"Wave position":"Wave mix";
  if(model==64)return index==0?"Waveform":"Chord";
  return braids[index];
 }
 if(model<24+braidsCount+6)return rings[index];
 if(model==81){static const char* ominous[]={"Filter mode / Q","Cutoff","Damping"};return ominous[index];}
 if(model<legacyModels+elementsProfiles)return elements[index];
 return extras[model-legacyModels-elementsProfiles][index];
}
int variation_count(){return static_cast<int>(sizeof(variants)/sizeof(*variants));}
int preset_code(int model,int variant){
 if(model<0||model>=model_count()||variant<0||variant>=variation_count())return -1;
 return variant<variations?model*variations+variant:extendedFlag|(model<<4)|variant;
}
int preset_model(int code){return (code&(1<<30))?code&127:(code&extendedFlag)?(code>>4)&127:code/variations;}
int preset_variation(int code){return (code&(1<<30))?0:(code&extendedFlag)?code&15:code%variations;}
std::array<int,3> preset_macros(int code){
 const int v=std::clamp(preset_variation(code),0,variation_count()-1);
 if(const auto* preset=named_preset(preset_model(code),v))return preset->macros;
 return {static_cast<int>(std::lround(macroA[v]*255)),static_cast<int>(std::lround(macroB[v]*255)),static_cast<int>(std::lround(macroC[v]*255))};
}
std::string preset_name(int code){
 if(!factory_code_valid(code))return "UNKNOWN";
 const int model=preset_model(code),variant=preset_variation(code);
 const auto* preset=named_preset(model,variant);
 const char* name=preset?preset->name:variants[variant];
 return std::string(model_name(model))+" / "+name;
}
std::string uri(int code){return "/@mutable/"+std::to_string(code)+".wav";}
int custom_code(int model,int a,int b,int c){return (1<<30)|std::clamp(model,0,model_count()-1)|(std::clamp(a,0,255)/2<<7)|(std::clamp(b,0,255)/2<<14)|(std::clamp(c,0,255)/2<<21);}
std::string custom_uri(int model,int a,int b,int c){return "/@mutable/p"+std::to_string(model)+"-"+std::to_string(a)+"-"+std::to_string(b)+"-"+std::to_string(c)+".wav";}
int model_id(int code){return preset_model(code);}
bool valid_code(int code){return code>=0&&(factory_code_valid(code)||((code&(1<<30))&&!(code&((1<<28)|(1<<29)))&&model_id(code)<model_count()));}
namespace {
bool plaits_lpg(int model){return !(model>=2&&model<=4)&&model<19;}
bool elements_modal(int model){return model==78||(model>=82&&model<86);}
// Physical firmware parameter slot <-> the visible, contiguous per-model control array.
int elements_slot(int model,int row){return row<12?row:model==81?row+1:elements_modal(model)?row:row+2;}
int elements_row(int model,int slot){
 if(slot<12)return slot;
 if(model==81)return slot==12?-1:slot-1;
 if(elements_modal(model))return slot;
 return slot<14?-1:slot-2;
}
}

int synth_param_count(int model){
 if(model<0||model>=model_count())return 0;
 if(is_gravity_model(model))return 3;
 if(model<24)return plaits_lpg(model)?4:2;if(model<72)return 3;if(model<78)return model==76?4:3;
 if(model<94)return model==81?15:elements_modal(model)?16:14;if(model<102)return 5;return 2;
}
const char* synth_param_name(int model,int index){
 if(index<0||index>=synth_param_count(model))return "";
 static const char* plaits[]={"Decay","LPG colour","Aux mix","Accent"};
 static const char* braids[]={"Bit depth","Sample rate","Output tone"};
 static const char* rings[]={"Exciter position","Polyphony","Aux mix","Chord"};
 static const char* elements[]={"Bow level","Bow timbre","Blow level","Blow texture","Blow timbre","Strike level","Strike type","Strike timbre","Exciter position","Space","Exciter envelope","Excitation strength","Modulation rate","Modulation depth","Reverb diffusion","Reverb tone"};
 static const char* ominous[]={"Osc 2 detune","Feedback","Osc 1 level","Osc 1 ratio","Osc 1 FM index","Osc 2 level","Osc 2 ratio","Osc 2 FM index","Spatial position","Space","Envelope shape","Excitation strength","Filter separation","Reverb diffusion","Reverb tone"};
 static const char* granular[]={"Grain density","Pitch spread","Grain level","Source noise","Output tone"};
 static const char* original[]={"Output tone","Drive"};
 if(is_gravity_model(model)&&index==2)return "Pitch scale";
 if(model<24)return plaits[plaits_lpg(model)?index:index+2];
 if(model==81)return ominous[index];
 return model<72?braids[index]:model<78?rings[index]:model<94?elements[elements_slot(model,index)]:model<102?granular[index]:original[index];
}
int synth_param_default(int model,int index){
 if(index<0||index>=synth_param_count(model))return 0;
 if(is_gravity_model(model)&&index==2)return kGravityScaleValues[gravity_default_scale(model)];
 const int plaits[]={204,128,0,255},braids[]={255,255,255},rings[]={77,0,0,0},granular[]={128,128,128,0,255},original[]={255,0};
 const int elements[]={0,128,0,128,128,166,128,128,77,77,128,179,23,51,159,179};
 if(model>=78&&model<94){
  index=elements_slot(model,index);
  if(model>=82){int articulation=(model-82)%4;
   if(index==0)return articulation==0?242:articulation==3?153:0;
   if(index==2)return articulation==1?230:articulation==3?115:0;
   if(index==5)return articulation==2?199:articulation==3?102:28;
   if(index==10)return articulation==2?64:219;
   if(index==11)return 204;
  }return elements[index];
 }
 return model<24?plaits[plaits_lpg(model)?index:index+2]:model<72?braids[index]:model<78?rings[index]:model<102?granular[index]:original[index];
}
namespace {
int braids_rate(int value){static constexpr int rates[]={4000,6000,8000,12000,16000,24000,32000,48000,96000};return rates[(std::clamp(value,0,255)*8+127)/255];}
int rings_polyphony(int value){return value<64?1:value<192?2:4;}
}
int synth_param_adjust(int model,int index,int value,int direction,bool coarse){
 if(index<0||index>=synth_param_count(model))return -1;
 value=value<0?synth_param_default(model,index):std::clamp(value,0,255);
 const int sign=direction>0?1:direction<0?-1:0;
 if(is_gravity_model(model)&&index==2)return kGravityScaleValues[std::clamp(gravity_scale_index(value)+sign,0,3)];
 if(model>=24&&model<72&&index==0){const int bits=std::clamp(4+value*12/255+sign,4,16);return ((bits-4)*255+11)/12;}
 if(model>=24&&model<72&&index==1){const int step=std::clamp((value*8+127)/255+sign,0,8);return (step*255+4)/8;}
 if(model>=72&&model<78&&index==1){const int current=value<64?0:value<192?1:2;const int step=std::clamp(current+sign,0,2);return step==0?0:step==1?128:255;}
 if(model==76&&index==3){const int chord=std::clamp(value*10/255+sign,0,10);return (chord*255+9)/10;}
 return std::clamp(value+sign*(coarse?16:1),0,255);
}
std::string synth_param_value(int model,int index,int value){
 if(is_gravity_model(model)&&index==2)return kGravityScaleNames[value<0?gravity_default_scale(model):gravity_scale_index(value)];
 if(value<0)return "Factory";
 value=std::clamp(value,0,255);char text[48];
 if(model>=24&&model<72&&index==0)std::snprintf(text,sizeof(text),"%d bit",4+value*12/255);
 else if(model>=24&&model<72&&index==1)std::snprintf(text,sizeof(text),"%.1f kHz",braids_rate(value)/1000.f);
 else if(model>=72&&model<78&&index==1)std::snprintf(text,sizeof(text),"%d voices",rings_polyphony(value));
 else if(model==76&&index==3)std::snprintf(text,sizeof(text),"Chord %d",1+value*10/255);
 else if(model>=78&&model<94&&elements_slot(model,index)==12)std::snprintf(text,sizeof(text),"%.2f Hz",.05f+4.95f*value/255.f);
 else std::snprintf(text,sizeof(text),"%.0f %%",100.f*value/255.f);
 return text;
}
int uri_code(const char* p){
 int code=-1,n=0,model=0,a=0,b=0,c=0;
 if(p&&std::sscanf(p,"/@mutable/p%d-%d-%d-%d.wav%n",&model,&a,&b,&c,&n)==4&&n>0&&p[n]==0&&model>=0&&model<model_count()&&a>=0&&a<=255&&b>=0&&b<=255&&c>=0&&c<=255)return custom_code(model,a,b,c);
 n=0;if(p&&std::sscanf(p,"/@mutable/%d.wav%n",&code,&n)==1&&n>0&&p[n]==0&&factory_code_valid(code))return code;return -1;
}
struct Synth::State {
 plaits::Voice plaits{}; braids::MacroOscillator braids{};
 rings::Part rings{}; elements::Part elements{}; extra::Voice additional{};
 alignas(8) char memory[65536]{};
 uint16_t reverbR[32768]{},reverbE[32768]{};
 float out[48]{}, aux[48]{},zero[48]{};
 plaits::Voice::Frame frames[24]{};
 uint8_t sync[48]{}; int16_t shorts[48]{};
 double cursor=24; int size=24; bool strike=false; int triggerBlocks=0;
 SynthParams params=default_synth_params();
 float lastOutput=0,transitionFrom=0,tone=0,braidsHeld=0;
 int transitionRemaining=0;float braidsHold=0;
 float parameter(int index,float authored) const{return params[index]<0?authored:params[index]/255.f;}
 void elements_controls(elements::Patch* p,int model){
  const auto ep=[&](int slot,float value){const int row=elements_row(model,slot);return row<0?value:parameter(row,value);};
  p->exciter_bow_level=ep(0,p->exciter_bow_level);p->exciter_bow_timbre=ep(1,p->exciter_bow_timbre);
  p->exciter_blow_level=ep(2,p->exciter_blow_level);p->exciter_blow_meta=ep(3,p->exciter_blow_meta);p->exciter_blow_timbre=ep(4,p->exciter_blow_timbre);
  p->exciter_strike_level=ep(5,p->exciter_strike_level);p->exciter_strike_meta=ep(6,p->exciter_strike_meta);p->exciter_strike_timbre=ep(7,p->exciter_strike_timbre);
  p->resonator_position=ep(8,p->resonator_position);p->space=ep(9,p->space);p->exciter_envelope_shape=ep(10,p->exciter_envelope_shape);
  const int rateRow=elements_row(model,12);
  p->resonator_modulation_frequency=rateRow<0||params[rateRow]<0?.5f/32000:(.05f+4.95f*parameter(rateRow,0))/32000;
  p->resonator_modulation_offset=ep(13,.2f)*.5f;
  p->reverb_diffusion=ep(14,.625f);p->reverb_lp=ep(15,.7f);
 }
 State(){stmlib::BufferAllocator a(memory,sizeof(memory));plaits.Init(&a);braids.Init();rings.Init(reverbR);rings.set_polyphony(1);elements.Init(reverbE);}
 void fill(int code,float hz){
  const int model=model_id(code),var=code&(1<<30)?0:preset_variation(code);
  float a[12],b[12],c[12];std::copy(std::begin(macroA),std::end(macroA),a);std::copy(std::begin(macroB),std::end(macroB),b);std::copy(std::begin(macroC),std::end(macroC),c);
  if(code&(1<<30)){a[0]=((code>>7)&127)/127.f;b[0]=((code>>14)&127)/127.f;c[0]=((code>>21)&127)/127.f;}
  else if(const auto* preset=named_preset(model,var)){const auto& m=preset->macros;a[var]=m[0]/255.f;b[var]=m[1]/255.f;c[var]=m[2]/255.f;}
  float note=69.f+12.f*std::log2(std::max(8.f,hz)/440.f);
  if(model<24){
   const bool lpg=plaits_lpg(model);
   plaits::Patch p{};p.engine=model;p.note=note;p.harmonics=a[var];p.timbre=b[var];p.morph=c[var];p.decay=lpg?parameter(0,.8f):.8f;p.lpg_colour=lpg?parameter(1,.5f):.5f;
   plaits::Modulations m{};m.trigger=strike?1.f:0.f;// FM velocity LUT interpolates index+1: keep the endpoint below 1.
   m.level=std::min(parameter(lpg?3:1,1.f),std::nextafter(1.f,0.f));
   // Keep oscillator models sustained; struck engines receive their trigger pulse.
   m.trigger_patched=model>=19||(model>=2&&model<=4);m.level_patched=true;
   if(triggerBlocks>0){m.trigger=triggerBlocks==7?0.f:1.f;--triggerBlocks;}
   plaits.Render(p,m,frames,24);size=24;
   const float auxMix=parameter(lpg?2:0,0);
   for(int i=0;i<size;++i)out[i]=(frames[i].out*(1-auxMix)+frames[i].aux*auxMix)/32768.f;
  }else if(model<24+braidsCount){
   braids.set_shape(static_cast<braids::MacroOscillatorShape>(model-24));
   braids.set_pitch(static_cast<int16_t>(std::clamp(note*128.f,0.f,16383.f)));
   braids.set_parameters(static_cast<int16_t>(a[var]*32767),static_cast<int16_t>(b[var]*32767));
   if(strike)braids.Strike();braids.Render(sync,shorts,24);size=24;
   for(int i=0;i<size;++i){
    float sample=shorts[i]/32768.f;
    if(params[0]>=0){const float levels=std::exp2(float(3+params[0]*12/255));sample=std::round(sample*levels)/levels;}
    if(params[1]>=0){if(braidsHold<=0){braidsHeld=sample;braidsHold+=96000.f/braids_rate(params[1]);}braidsHold-=1;sample=braidsHeld;}
    out[i]=sample;
   }
  }else if(model<24+braidsCount+6){
   rings.set_model(static_cast<rings::ResonatorModel>(model-24-braidsCount));
   const int poly=params[1]<0?1:rings_polyphony(params[1]);if(rings.polyphony()!=poly)rings.set_polyphony(poly);
   rings::Patch p{};p.structure=a[var];p.brightness=b[var];p.damping=c[var];p.position=parameter(0,.3f);
   rings::PerformanceState v{};v.note=note;v.strum=strike;v.internal_exciter=true;v.chord=params[3]<0?0:params[3]*10/255;
   rings.Process(v,p,zero,out,aux,24);size=24;
   const float auxMix=parameter(2,0);for(int i=0;i<size;++i)out[i]=out[i]*(1-auxMix)+aux[i]*auxMix;
  }else if(model<legacyModels){
   int mode=model-24-braidsCount-6;
   elements.set_resonator_model(static_cast<elements::ResonatorModel>(std::min(mode,2)));elements.set_easter_egg(mode==3);
   auto p=elements.mutable_patch();p->resonator_geometry=a[var];p->resonator_brightness=b[var];p->resonator_damping=c[var];p->resonator_position=.3f;
   p->exciter_strike_level=.65f;p->exciter_strike_meta=.5f;p->exciter_strike_timbre=b[var];p->exciter_bow_level=var==3?.7f:0.f;p->exciter_bow_timbre=.5f;p->exciter_blow_level=var==1?.5f:0.f;p->exciter_blow_timbre=.5f;p->exciter_envelope_shape=.5f;p->space=.3f;
   elements_controls(p,model);
   elements::PerformanceState v{};v.note=note;v.gate=triggerBlocks>0?(triggerBlocks<7):(var==3);if(triggerBlocks>0)--triggerBlocks;v.strength=parameter(11,.7f);
   elements.Process(v,zero,zero,out,aux,16);size=16;
  }else if(model<legacyModels+elementsProfiles){
   // These are additional authored exciter articulations of the three real Elements
   // resonators, not additional Mutable firmware models. A=geometry/exciter, B=colour,
   // C=damping. Sustaining articulations keep the exciter's gate open; the instrument
   // envelope and note-off still control the host voice amplitude.
   const int profile=model-legacyModels,mode=profile/4,articulation=profile%4;
   elements.set_resonator_model(static_cast<elements::ResonatorModel>(mode));elements.set_easter_egg(false);
   auto p=elements.mutable_patch();
   p->resonator_geometry=.04f+.92f*a[var];p->resonator_brightness=.15f+.8f*b[var];
   p->resonator_damping=.08f+.88f*c[var];p->resonator_position=.15f+.65f*a[var];
   p->exciter_bow_level=articulation==0?.95f:articulation==3?.6f:0.f;
   p->exciter_bow_timbre=.2f+.7f*b[var];
   p->exciter_blow_level=articulation==1?.9f:articulation==3?.45f:0.f;
   p->exciter_blow_meta=.05f+.9f*a[var];p->exciter_blow_timbre=.25f+.7f*b[var];
   p->exciter_strike_level=articulation==2?.78f:articulation==3?.4f:.11f;
   p->exciter_strike_meta=articulation==2?(mode==1?.64f:.47f+.09f*a[var]):.53f;
   p->exciter_strike_timbre=.2f+.75f*b[var];p->exciter_envelope_shape=articulation==2?.25f:.86f;
   p->space=.15f+.2f*c[var];
   elements_controls(p,model);
   elements::PerformanceState v{};v.note=note;v.strength=parameter(11,.8f);
   v.gate=triggerBlocks>0?(triggerBlocks<7):(articulation!=2);
   if(triggerBlocks>0)--triggerBlocks;
   elements.Process(v,zero,zero,out,aux,16);size=16;
   for(int i=0;i<size;++i)out[i]*=.7f;
  }else{
   const int extraModel=model-legacyModels-elementsProfiles;
   if(strike)additional.start(extraModel,a[var],b[var],c[var]);
   additional.update(a[var],b[var],c[var],params);
   if(is_gravity_model(model))additional.set_gravity_scale(params[2]<0?gravity_default_scale(model):gravity_scale_index(params[2]));
   size=24;for(int i=0;i<size;++i)out[i]=additional.next(hz,a[var],b[var],c[var]);
  }
  const int toneIndex=model>=24&&model<72?2:model>=94&&model<102?4:model>=102?0:-1;
  if(toneIndex>=0&&params[toneIndex]>=0&&params[toneIndex]<255){
   const float nativeRate=model<72?96000.f:48000.f;
   const float cutoff=120.f*std::pow((nativeRate*.45f)/120.f,parameter(toneIndex,1));
   const float coefficient=1-std::exp(-6.283185307f*cutoff/nativeRate);
   for(int i=0;i<size;++i){tone+=coefficient*(out[i]-tone);out[i]=tone;}
  }else tone=out[size-1];
  if(model>=102&&params[1]>0){const float drive=1+7*parameter(1,0);for(int i=0;i<size;++i)out[i]=std::tanh(out[i]*drive)/std::tanh(drive);}
  strike=false;
 }
};
Synth::Synth():s_(new State){}
Synth::~Synth()=default;
void Synth::reset(){s_->~State();new(s_.get()) State;code_=-1;}
void Synth::start(int code){code_=valid_code(code)?code:-1;s_->cursor=s_->size;s_->strike=true;s_->triggerBlocks=7;s_->transitionRemaining=0;s_->lastOutput=0;s_->params=default_synth_params();}
void Synth::update(int code,const SynthParams& params){
 if(!valid_code(code))return;
 if(code!=code_){
  if(code_<0||model_id(code)!=model_id(code_)){const float previous=s_->lastOutput;start(code);s_->transitionFrom=previous;s_->transitionRemaining=96;}
  else code_=code;
 }
 for(int i=0;i<kSynthParams;++i)s_->params[i]=std::clamp(params[i],-1,255);
}
float Synth::next(float hz,float sampleRate){
 if(code_<0||!std::isfinite(hz)||hz<=0||!std::isfinite(sampleRate)||sampleRate<8000)return 0;
 const int model=model_id(code_);
 const double rate=model<24?48000:(model<24+braidsCount?96000:(model<24+braidsCount+6?48000:(model<legacyModels+elementsProfiles?32000:48000)));
 // Native clock conversion. Interpolate 32 kHz models; average oversampled Braids reads.
 float sum=0;int count=0;
 double advance=rate/std::max(8000.f,sampleRate),remaining=advance;
 while(remaining>0){
  if(s_->cursor>=s_->size){s_->cursor-=s_->size;s_->fill(code_,hz);}
  int i=std::min(s_->size-1,static_cast<int>(s_->cursor));
  float f=static_cast<float>(s_->cursor-i);
  float value=s_->out[i]+f*(s_->out[std::min(i+1,s_->size-1)]-s_->out[i]);
  sum+=value;++count;
  double step=std::min(1.0,remaining);s_->cursor+=step;remaining-=step;
 }
 float output=std::isfinite(sum)?std::clamp(sum/std::max(1,count),-2.f,2.f):0.f;
 if(s_->transitionRemaining>0){const float mix=(97-s_->transitionRemaining)/96.f;output=s_->transitionFrom*(1-mix)+output*mix;--s_->transitionRemaining;}
 s_->lastOutput=output;return output;
}
}
