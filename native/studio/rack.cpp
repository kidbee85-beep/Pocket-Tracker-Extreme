#include "rack.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>
#include "rings/dsp/part.h"
#include "warps/dsp/modulator.h"
// Generated firmware resource names must not leak into the next module.
#undef LUT_XFADE_IN
#undef LUT_XFADE_IN_SIZE
#undef LUT_XFADE_OUT
#undef LUT_XFADE_OUT_SIZE
#include "clouds/dsp/granular_processor.h"
namespace studio {
const char* effect_name(int id){static const char* names[]={"BYPASS","R MODAL","R SYMPATHETIC","R STRING","R FM","R QUANTIZED","R STRING VERB","W CROSSFADE","W FOLD","W ANALOG RING","W DIGITAL RING","W XOR","W COMPARE","W VOCODER A","W VOCODER B","W VOCODER C","C GRANULAR","C STRETCH","C LOOP DELAY","C SPECTRAL"};return names[std::clamp(id,0,19)];}
int effect_count(){return 20;}
int effect_preset_count(){return effect_count()*8;}
std::string effect_preset_name(int id){static const char* names[]={"LIGHT","BALANCED","DEEP","FULL","OPEN","MUTED","FRAGMENTS","DRIFT"};id=std::clamp(id,0,effect_preset_count()-1);return std::string(effect_name(id/8))+" / "+names[id%8];}
std::array<int,COUNT> effect_preset(int id){
 id=std::clamp(id,0,effect_preset_count()-1);int fx=id/8,v=id%8;
 static constexpr int mix[]={64,128,192,255,102,160,220,140};
 static constexpr int color[]={48,96,144,192,224,24,176,112};
 static constexpr int texture[]={192,152,112,72,224,32,48,208};
 static constexpr int feedback[]={32,48,72,96,24,64,128,176};
 auto p=defaults;p[ENABLE]=1;p[FX]=fx;p[FX_MIX]=mix[v];p[COLOR]=color[v];p[TEXTURE]=texture[v];p[FEEDBACK]=feedback[v];return p;
}
int master_preset_count(){return 32;}
const char* master_preset_name(int id){static const char* n[]={"CLEAN","WARM GLUE","BOOM BAP","WARM DRIVE","DEEP HOUSE","DRUM BUS","AMBIENT WIDE","SUB CONTROL","WIDE CLEAN","PUNCHY MIX","SOFT SAT","MONO CHECK","NARROW WARM","LOUD PREVIEW","GENTLE LIMIT","HARD CRUSH","TAPE ROUND","VOCAL GLUE","BREAKBEAT","DUB LOW","CINEMA WIDE","PERCUSSION","ACOUSTIC SOFT","FM POLISH","DARK AMBIENT","ELECTRO BUS","LOFI MONO","AIRY SPACE","GENTLE PUMP","BASS FOCUS","TRANSIENT KEEP","PARALLEL COLOR"};return n[std::clamp(id,0,master_preset_count()-1)];}
std::array<int,COUNT> master_preset(int id){auto p=defaults;p[ENABLE]=1;p[THRESHOLD]=210;p[RATIO]=20;p[ATTACK]=80;p[RELEASE]=100;
 switch(id){case 0:p=defaults;break;case 1:p[DRIVE]=35;break;case 2:p[DRIVE]=65;p[THRESHOLD]=180;p[RATIO]=45;p[WIDTH]=110;break;case 3:p[DRIVE]=95;p[HIGH_PASS]=12;break;case 4:p[THRESHOLD]=160;p[RATIO]=45;p[ATTACK]=120;break;case 5:p[THRESHOLD]=140;p[RATIO]=90;p[ATTACK]=100;break;case 6:p[WIDTH]=180;p[THRESHOLD]=240;break;case 7:p[HIGH_PASS]=25;p[WIDTH]=100;break;case 8:p[HIGH_PASS]=15;p[WIDTH]=150;break;case 9:p[THRESHOLD]=155;p[RATIO]=65;p[ATTACK]=150;break;case 10:p[DRIVE]=125;break;case 11:p[WIDTH]=0;break;case 12:p[DRIVE]=30;p[WIDTH]=85;break;case 13:p[DRIVE]=45;p[OUTPUT]=155;p[THRESHOLD]=190;p[RATIO]=60;break;case 14:p[CEILING]=235;p[THRESHOLD]=245;break;case 15:p[THRESHOLD]=100;p[RATIO]=190;p[ATTACK]=10;p[OUTPUT]=150;break;
 case 16:p[DRIVE]=75;p[THRESHOLD]=200;p[RATIO]=30;p[WIDTH]=118;break;
 case 17:p[HIGH_PASS]=45;p[THRESHOLD]=175;p[RATIO]=35;p[ATTACK]=35;p[RELEASE]=80;break;
 case 18:p[DRIVE]=45;p[THRESHOLD]=165;p[RATIO]=75;p[ATTACK]=145;p[RELEASE]=50;break;
 case 19:p[HIGH_PASS]=8;p[THRESHOLD]=200;p[RATIO]=30;p[WIDTH]=95;break;
 case 20:p[WIDTH]=175;p[THRESHOLD]=225;p[RATIO]=15;p[RELEASE]=180;break;
 case 21:p[HIGH_PASS]=20;p[THRESHOLD]=185;p[RATIO]=45;p[ATTACK]=190;p[RELEASE]=35;break;
 case 22:p[DRIVE]=12;p[THRESHOLD]=220;p[RATIO]=12;p[ATTACK]=160;p[WIDTH]=120;break;
 case 23:p[DRIVE]=20;p[THRESHOLD]=195;p[RATIO]=28;p[ATTACK]=90;p[CEILING]=240;break;
 case 24:p[WIDTH]=165;p[THRESHOLD]=235;p[RATIO]=8;p[RELEASE]=220;p[OUTPUT]=120;break;
 case 25:p[DRIVE]=55;p[THRESHOLD]=180;p[RATIO]=55;p[ATTACK]=110;p[RELEASE]=70;break;
 case 26:p[WIDTH]=0;p[DRIVE]=110;p[THRESHOLD]=190;p[RATIO]=40;p[HIGH_PASS]=12;break;
 case 27:p[WIDTH]=190;p[THRESHOLD]=245;p[RATIO]=5;p[HIGH_PASS]=30;break;
 case 28:p[THRESHOLD]=145;p[RATIO]=38;p[ATTACK]=80;p[RELEASE]=55;p[OUTPUT]=138;break;
 case 29:p[WIDTH]=80;p[HIGH_PASS]=15;p[THRESHOLD]=170;p[RATIO]=50;p[ATTACK]=140;break;
 case 30:p[THRESHOLD]=205;p[RATIO]=25;p[ATTACK]=230;p[RELEASE]=90;break;
 case 31:p[DRIVE]=140;p[THRESHOLD]=210;p[RATIO]=20;p[OUTPUT]=110;p[WIDTH]=125;break;}
 return p;}
struct Rack::State {
 rings::Part rings{};uint16_t reverb[32768]{};
 warps::Modulator warps{};
 clouds::GranularProcessor clouds{};uint8_t large[118784]{},small[65408]{};
 float in[48][2]{},out[48][2]{},tmpL[48]{},tmpR[48]{},src[48]{};
 warps::ShortFrame wi[96]{},wo[96]{};clouds::ShortFrame ci[32]{},co[32]{};
 int pos=0,lastFx=-1;float envelope=0,prevIn[2]{},hp[2]{};
 float history[96000][2]{};float captured[48000][2]{};int capturedLength=1;double tapePos=0;float tapeRate=1;int write=0,play=0,lastPunch=0,lastRequested=0;float wetPunch=0,filter[2]{},band[2]{};
 State(){rings.Init(reverb);rings.set_polyphony(1);warps.Init(96000);clouds.Init(large,sizeof(large),small,sizeof(small));clouds.set_num_channels(2);clouds.set_low_fidelity(false);}
 void effect(const std::array<int,COUNT>& p){
  int fx=p[FX];float color=p[COLOR]/255.f,texture=p[TEXTURE]/255.f;
  if(fx>=1&&fx<=6){
   rings.set_model(static_cast<rings::ResonatorModel>(fx-1));
   rings::PerformanceState v{};v.note=48;v.internal_exciter=false;
   rings::Patch patch{};patch.structure=color;patch.brightness=texture;patch.damping=.6f;patch.position=.35f;
   for(int base=0;base<48;base+=24){for(int i=0;i<24;++i)src[i]=.5f*(in[base+i][0]+in[base+i][1]);rings.Process(v,patch,src,tmpL,tmpR,24);for(int i=0;i<24;++i){out[base+i][0]=tmpL[i];out[base+i][1]=tmpR[i];}}
  }else if(fx>=7&&fx<=15){
   auto v=warps.mutable_parameters();v->modulation_algorithm=(fx-7)/8.f;v->modulation_parameter=color;v->channel_drive[0]=.5f;v->channel_drive[1]=.5f;v->carrier_shape=1;v->note=36+texture*48;
   for(int i=0;i<96;++i){int k=i/2;wi[i].l=static_cast<int16_t>(std::clamp(in[k][0],-.999f,.999f)*32767);wi[i].r=static_cast<int16_t>(std::clamp(in[k][1],-.999f,.999f)*32767);}warps.Process(wi,wo,96);
   for(int i=0;i<48;++i){out[i][0]=(wo[2*i].l+wo[2*i+1].l)/65536.f;out[i][1]=(wo[2*i].r+wo[2*i+1].r)/65536.f;}
  }else if(fx>=16){
   if(lastFx!=fx){clouds.set_playback_mode(static_cast<clouds::PlaybackMode>(fx-16));clouds.Prepare();}
   auto v=clouds.mutable_parameters();v->position=color;v->size=texture;v->pitch=0;v->density=.6f;v->texture=texture;v->dry_wet=std::nextafter(1.f,0.f);v->stereo_spread=.7f;v->feedback=std::min(.85f,p[FEEDBACK]/255.f);v->reverb=.15f;v->freeze=false;v->trigger=false;v->gate=false;
   for(int i=0;i<32;++i){float x=i*1.5f;int k=static_cast<int>(x);float f=x-k;float l=in[k][0]+f*(in[std::min(k+1,47)][0]-in[k][0]);float r=in[k][1]+f*(in[std::min(k+1,47)][1]-in[k][1]);ci[i].l=static_cast<int16_t>(std::clamp(l,-.999f,.999f)*32767);ci[i].r=static_cast<int16_t>(std::clamp(r,-.999f,.999f)*32767);}clouds.Process(ci,co,32);
   for(int i=0;i<48;++i){float x=i*2.f/3;int k=static_cast<int>(x);float f=x-k;out[i][0]=(co[k].l+f*(co[std::min(k+1,31)].l-co[k].l))/32768.f;out[i][1]=(co[k].r+f*(co[std::min(k+1,31)].r-co[k].r))/32768.f;}
  }else{for(int i=0;i<48;++i){out[i][0]=in[i][0];out[i][1]=in[i][1];}}
  // Dry and wet share the 48-frame block delay.
  float mix=p[FX_MIX]/255.f;for(int i=0;i<48;++i)for(int c=0;c<2;++c)out[i][c]=in[i][c]+mix*(out[i][c]-in[i][c]);lastFx=fx;
 }
};
Rack::Rack():s_(new State){for(int i=0;i<COUNT;++i)control[i].store(defaults[i]);}
Rack::~Rack()=default;
void Rack::reset(){s_->~State();new(s_.get()) State;punch.store(0,std::memory_order_relaxed);}
void Rack::process(float* buf,int frames,int ch,float sr){
 if(!buf||ch!=2||frames<1||!std::isfinite(sr)||sr<8000)return;
 std::array<int,COUNT> p{};for(int i=0;i<COUNT;++i)p[i]=std::clamp(control[i].load(std::memory_order_relaxed),0,255);p[FX]=std::min(19,p[FX]);
 const bool enabled=p[ENABLE]!=0;const int requested=std::clamp(punch.load(),0,24);
 // Keep rendering the previous gesture while its wet gain returns to zero.
 // Replacing it with dry audio here would bypass the release ramp and click.
 const int effect=requested?requested:s_->lastPunch;
 if(!enabled&&!requested&&s_->wetPunch<.0001f){
  for(int i=0;i<frames;++i){s_->history[s_->write][0]=buf[2*i];s_->history[s_->write][1]=buf[2*i+1];s_->write=(s_->write+1)%96000;}
  s_->lastPunch=0;s_->lastRequested=0;return;
 }
 const float threshold=std::pow(10.f,(-48+48*p[THRESHOLD]/255.f)/20),ratio=1+19*p[RATIO]/255.f;
 const float attack=std::exp(-1.f/(sr*(.001f+.099f*p[ATTACK]/255.f))),release=std::exp(-1.f/(sr*(.02f+.98f*p[RELEASE]/255.f)));
 const float drive=1+7*p[DRIVE]/255.f,gain=std::pow(10.f,(p[OUTPUT]-128)/128.f),ceiling=std::pow(10.f,(-12+12*p[CEILING]/255.f)/20);
 const float high=std::exp(-6.2831853f*(10+190*p[HIGH_PASS]/255.f)/sr);
 int loop=std::clamp(static_cast<int>(sr*60.f/std::max(20,tempo.load())/(effect==23?16:(effect==2||effect==11?8:4))),48,48000);
 for(int i=0;i<frames;++i){float l=buf[2*i],r=buf[2*i+1];
  if(enabled){
   if(p[FX]&&p[FX_MIX]){float ol=s_->out[s_->pos][0],orr=s_->out[s_->pos][1];s_->in[s_->pos][0]=l;s_->in[s_->pos][1]=r;if(++s_->pos==48){s_->pos=0;s_->effect(p);}l=ol;r=orr;}
   float* x[2]={&l,&r};for(int c=0;c<2;++c){if(p[HIGH_PASS]){float y=high*(s_->hp[c]+*x[c]-s_->prevIn[c]);s_->prevIn[c]=*x[c];s_->hp[c]=y;*x[c]=y;}if(p[DRIVE])*x[c]=std::tanh(*x[c]*drive)/std::tanh(drive);}
   float peak=std::max(std::fabs(l),std::fabs(r));float coeff=peak>s_->envelope?attack:release;s_->envelope=coeff*s_->envelope+(1-coeff)*peak;
   float reduction=s_->envelope>threshold?std::pow(threshold/std::max(.000001f,s_->envelope),1-1/ratio):1;
   float mid=(l+r)*.5f,side=(l-r)*.5f*p[WIDTH]/128.f;l=(mid+side)*gain*reduction;r=(mid-side)*gain*reduction;
   l=std::clamp(l,-ceiling,ceiling);r=std::clamp(r,-ceiling,ceiling);
  }
  s_->history[s_->write][0]=l;s_->history[s_->write][1]=r;
  if(requested&&(requested!=s_->lastPunch||!s_->lastRequested)){
   s_->play=0;s_->tapePos=0;s_->tapeRate=1;s_->capturedLength=loop;
   if((effect>=1&&effect<=3)||(effect>=8&&effect<=11)||effect==23)for(int k=0;k<loop;++k){int from=(s_->write-loop+k+96000)%96000;s_->captured[k][0]=s_->history[from][0];s_->captured[k][1]=s_->history[from][1];}
   s_->lastPunch=effect;
  }
  s_->wetPunch+=((requested?1.f:0.f)-s_->wetPunch)*.01f;
  float a=l,b=r;
  switch(effect){case 1:case 2:case 3:case 11:case 23:{int k=(effect==3||effect==11)?s_->capturedLength-1-s_->play:s_->play;a=s_->captured[k][0];b=s_->captured[k][1];s_->play=(s_->play+1)%s_->capturedLength;break;}
   case 4:s_->filter[0]+=.035f*(l-s_->filter[0]);s_->filter[1]+=.035f*(r-s_->filter[1]);a=s_->filter[0];b=s_->filter[1];break;
   case 5:a=std::round(l*16)/16;b=std::round(r*16)/16;break;
   case 6:{int k=(s_->write-loop+96000)%96000;a=l+.6f*s_->history[k][0];b=r+.6f*s_->history[k][1];break;}
   case 7:{float g=(s_->write%(loop/2)<loop/4)?1.f:.05f;a=l*g;b=r*g;break;}
   case 8:case 9:case 10:{int k=static_cast<int>(s_->tapePos)%s_->capturedLength;int next=(k+1)%s_->capturedLength;float f=s_->tapePos-static_cast<int>(s_->tapePos);a=s_->captured[k][0]+f*(s_->captured[next][0]-s_->captured[k][0]);b=s_->captured[k][1]+f*(s_->captured[next][1]-s_->captured[k][1]);double rate=effect==9?.5:effect==10?2:s_->tapeRate;s_->tapePos+=rate;if(s_->tapePos>=s_->capturedLength)s_->tapePos-=s_->capturedLength;if(effect==8)s_->tapeRate=std::max(0.f,s_->tapeRate-1.f/std::max(1,loop));break;}
   case 12:case 13:{s_->filter[0]+=.045f*(l-s_->filter[0]);s_->filter[1]+=.045f*(r-s_->filter[1]);a=l-s_->filter[0];b=r-s_->filter[1];if(effect==13){s_->band[0]+=.18f*(a-s_->band[0]);s_->band[1]+=.18f*(b-s_->band[1]);a=s_->band[0];b=s_->band[1];}break;}
   case 14:a=std::tanh(l*6);b=std::tanh(r*6);break;
   case 15:a=std::sin(l*9.424778f);b=std::sin(r*9.424778f);break;
   case 16:{float carrier=std::sin(s_->write*6.2831853f*50/sr);a=l*carrier;b=r*carrier;break;}
   case 17:{float g=.55f+.45f*std::sin(s_->write*6.2831853f/std::max(1,loop));a=l*g;b=r*g;break;}
   case 18:{float g=.5f+.5f*std::sin(s_->write*6.2831853f/std::max(1,loop));a=l*g;b=r*(1-g);break;}
   case 19:a=r;b=l;break;
   case 20:a=b=.5f*(l+r);break;
   case 21:{float mid=.5f*(l+r),side=l-r;a=mid+side;b=mid-side;break;}
   case 22:{float g=.1f+.9f*static_cast<float>(s_->write%loop)/loop;a=l*g;b=r*g;break;}
   case 24:a=b=0;break;
  }
  buf[2*i]=l+s_->wetPunch*(a-l);buf[2*i+1]=r+s_->wetPunch*(b-r);
  if(!std::isfinite(buf[2*i]))buf[2*i]=0;if(!std::isfinite(buf[2*i+1]))buf[2*i+1]=0;
  s_->write=(s_->write+1)%96000;
 }
 s_->lastRequested=requested;
 if(!requested&&s_->wetPunch<.0001f){s_->wetPunch=0;s_->lastPunch=0;}
}
}
