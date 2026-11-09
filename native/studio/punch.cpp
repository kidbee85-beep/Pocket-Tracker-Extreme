#include "punch.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>
namespace studio {
namespace {
constexpr float pi=3.14159265358979323846f;
constexpr int memory=192000;
struct Pad {const char* name;const char* detail;};
// IDs 1..24 retain beta.1's meanings. Banks are presentation order, never persistence IDs.
constexpr Pad pads[]={
 {"OFF","Unprocessed mix"},{"BEAT REPEAT","Capture and repeat a quarter-beat"},
 {"ROLL","Capture and roll an eighth-beat"},{"REVERSE","Reverse the captured quarter-beat"},
 {"LOW PASS","Warm low-pass cut"},{"BIT CRUSH","Five-bit amplitude quantization"},
 {"DELAY THROW","Quarter-beat feed-forward echo"},{"GATE","Tempo-locked square gate"},
 {"TAPE STOP","Captured audio slows to a stop"},{"HALF SPEED","Captured audio at half speed"},
 {"DOUBLE SPEED","Captured audio at double speed"},{"REVERSE ROLL","Reverse eighth-beat roll"},
 {"HIGH PASS","Remove lows and open the top"},{"BAND PASS","Focused band-pass tone"},
 {"DISTORT","Soft saturated drive"},{"WAVE FOLD","Sine wavefolding"},
 {"RING MOD","50 Hz bipolar ring modulation"},{"TREMOLO","Tempo-locked amplitude wave"},
 {"AUTO PAN","Tempo-locked left/right motion"},{"STEREO SWAP","Exchange left and right"},
 {"MONO","Sum the stereo image to mono"},{"WIDE","Double the side signal"},
 {"DUCK","Tempo-locked rising pump envelope"},{"MICRO STUTTER","Sixteenth-beat captured repeat"},
 {"DROP OUT","Momentary silence"},{"TRIPLET ROLL","Repeat a sixth-beat capture"},
 {"SWING GATE","Alternating long and short gates"},{"TAPE START","Accelerate a captured loop from rest"},
 {"WOW / FLUTTER","Slow and fast tape-speed modulation"},{"REWIND","Accelerating reverse tape"},
 {"NOTCH","Remove a narrow moving frequency band"},{"RESONANT SWEEP","Tempo-swept resonant low pass"},
 {"COMB +","Positive short-delay comb resonance"},{"COMB -","Negative short-delay comb notch"},
 {"VOWEL","Two resonant formants, swept together"},{"PING PONG","Alternating stereo echo taps"},
 {"DUB ECHO","Dark, saturating feedback echo"},{"SMALL ROOM","Four short reflected taps"},
 {"SHIMMER","Octave grains above an echo bed"},{"SPRING","Dispersed, resonant delay tank"},
 {"ECHO FREEZE","Captured beat fed through stereo echoes"},{"REVERSE ECHO","Backward capture plus delayed reflections"},
 {"SLICE JUMP","Deterministic quarter-slice rearrangement"},{"BUFFER SHUFFLE","Eight slices in a shuffled order"},
 {"TIME SKEW","Variable-rate loop with a slow-down contour"},{"BIT GATE","Rhythmic eight-bit bursts"},
 {"STEREO CHOP","Alternating left/right step cuts"},{"STEP DROP","One missing step in each group of four"},
 {"TIME STRETCH","Overlapping grains at quarter source speed"},{"CLOUD GRAINS","Four overlapping, drifting grains"},
 {"MICRO PITCH","Opposed, detuned stereo grains"},{"GRAIN REVERSE","Windowed backward grains"},
 {"GRAIN OCTAVE","Windowed grains one octave up"},{"GRAIN FIFTH","Windowed grains a fifth up"},
 {"SCATTER","Short grains scattered over the capture"},{"GRAIN FREEZE","Overlapping grains at a fixed position"},
 {"GRAIN COMB","Pitched grains through a short stereo comb"},{"CHORUS","Two slowly modulated delay voices"},
 {"FLANGER","Short swept feedback comb"},{"VIBRATO","Modulated delay with continuous pitch motion"},
 {"SAMPLE HOLD","Reduce sample rate independently of bit depth"},{"RECTIFY","Full-wave rectification and DC removal"},
 {"HARD CLIP","Symmetric hard clipping with output trim"},{"XOR CRUSH","Bitwise distortion of eight-bit samples"},
 {"BASS DRIVE","Asymmetric drive with DC removal"}
};
constexpr int banks[kPunchBanks][kPunchPads]={
 {1,2,23,25,7,26,17,22},{3,8,9,10,11,27,28,29},
 {4,12,13,30,31,32,33,34},{6,35,36,37,38,39,40,41},
 {42,43,44,45,46,47,48,24},{49,50,51,52,53,54,55,56},
 {16,18,19,20,21,57,58,59},{5,14,15,60,61,62,63,64}
};
bool capture_effect(int id){return id==1||id==2||id==3||(id>=8&&id<=11)||id==23||id==25||id==27||id==29||id==38||id==40||id==41||(id>=42&&id<=44)||id==48||(id>=49&&id<=56);}
float soft(float x){return x/(1.f+std::fabs(x));}
int wrap(int n){n%=memory;return n<0?n+memory:n;}
}
const char* punch_bank_name(int bank){static const char* names[]={"RHYTHM","TAPE","FILTER","DUB / SPACE","CUT / GLITCH","GRANULAR","MODULATION","DAMAGE"};return names[std::clamp(bank,0,kPunchBanks-1)];}
const char* punch_name(int id){return pads[std::clamp(id,0,kPunchCount)].name;}
const char* punch_detail(int id){return pads[std::clamp(id,0,kPunchCount)].detail;}
int punch_id(int bank,int pad){return banks[std::clamp(bank,0,kPunchBanks-1)][std::clamp(pad,0,kPunchPads-1)];}
int punch_bank(int id){for(int b=0;b<kPunchBanks;++b)for(int p=0;p<kPunchPads;++p)if(banks[b][p]==id)return b;return 0;}
int punch_pad(int id){for(int b=0;b<kPunchBanks;++b)for(int p=0;p<kPunchPads;++p)if(banks[b][p]==id)return p;return 0;}
struct PunchProcessor::State {
 float history[memory][2]{},snapshot[memory][2]{},delay[memory][2]{};
 int write=0,delayWrite=0,anchor=0,length=1,copied=0,active=0,lastRequest=0;uint64_t age=0;
 float wet=0,low[2]{},band[2]{},formLow[2]{},formBand[2]{},echoLow[2]{},held[2]{},dcIn[2]{},dcOut[2]{},last[2]{};
 double phase=0,tape=0;float speed=1;int transition=0;
 float read(float (*buf)[2],double position,int channel)const{
  const double floor=std::floor(position);const int index=wrap(static_cast<int>(floor));const float frac=static_cast<float>(position-floor);
  return buf[index][channel]+frac*(buf[(index+1)%memory][channel]-buf[index][channel]);
 }
 float captured(double position,int channel)const{
  position=std::fmod(position,length);if(position<0)position+=length;
  // Interpolation at the capture's wrap must stay inside the frozen segment.
  const int k=static_cast<int>(position),next=(k+1)%length;const float f=static_cast<float>(position-k);
  const float a=k<copied?snapshot[k][channel]:history[wrap(anchor-length+k)][channel];
  const float b=next<copied?snapshot[next][channel]:history[wrap(anchor-length+next)][channel];return a+f*(b-a);
 }
 float grain(int channel,int mode,float sr){
  const int size=std::max(64,static_cast<int>(sr*(mode==54?.023f:mode==55?.12f:.065f)));
  float sum=0,weight=0;
  const int voices=mode==49||mode==54?4:2;
  const double ratio=mode==51?-1:mode==52?2:mode==53?1.498307:mode==50?(channel?.985:1.015):mode==56?1.33484:1;
  for(int n=0;n<voices;++n){
   const double time=age+double(n*size)/voices;const int cycle=static_cast<int>(time/size);const float f=static_cast<float>(std::fmod(time,size)/size);
   const float window=.5f-.5f*std::cos(2*pi*f);
   double origin=mode==55?length*.51:mode==48?cycle*size*.25:cycle*size*.73;
   if(mode==54)origin=(static_cast<uint32_t>(cycle*1664525u+n*1013904223u)%static_cast<unsigned>(length));
   if(mode==49)origin+=n*length*.19+channel*size*.33;
   if(mode==50)origin+=channel*size*.25;
   sum+=window*captured(origin+ratio*f*size,channel);weight+=window;
  }
  return sum/std::max(.01f,weight);
 }
};
PunchProcessor::PunchProcessor():s_(new State){}
PunchProcessor::~PunchProcessor()=default;
void PunchProcessor::reset(){s_->~State();new(s_.get()) State;}
void PunchProcessor::process(float* out,int frames,float sr,int request,int bpm){
 if(!out||frames<=0||!std::isfinite(sr)||sr<8000)return;
 request=std::clamp(request,0,kPunchCount);bpm=std::clamp(bpm,20,999);auto& s=*s_;
 const int beat=std::clamp(static_cast<int>(sr*60.f/bpm),64,memory-2);
 if(request&&(request!=s.active||!s.lastRequest)){
  s.active=request;s.age=0;s.anchor=s.write;s.phase=0;s.tape=0;s.speed=1;s.transition=96;
  const int div=request==23?16:request==2||request==11?8:request==25?6:request==1||request==3||request==8||request==9||request==10?4:1;
  s.length=std::max(48,beat/div);s.copied=0;
 }
 const int id=s.active;const int quarter=std::max(16,beat/4);
 const float ramp=1-std::exp(-1.f/(sr*.002f));
 for(int i=0;i<frames;++i){const float l=out[2*i],r=out[2*i+1];float a=l,b=r;
  // Copy a snapshot incrementally, ahead of the live writer. Even a full-ring
  // capture stays valid: four old frames are preserved for every one overwritten.
  // Engage performs no bulk copy, and pre-roll stays current throughout long holds.
  if(capture_effect(id))for(int n=0;n<4&&s.copied<s.length;++n){
   const int from=wrap(s.anchor-s.length+s.copied);
   s.snapshot[s.copied][0]=s.history[from][0];s.snapshot[s.copied][1]=s.history[from][1];++s.copied;
  }
  s.history[s.write][0]=l;s.history[s.write][1]=r;s.write=(s.write+1)%memory;
  s.delay[s.delayWrite][0]=l;s.delay[s.delayWrite][1]=r;
  if((id&&s.wet>0)||request){
   const float wave=std::sin(2*pi*static_cast<float>(s.phase));
   switch(id){
   case 1:case 2:case 3:case 11:case 23:case 25:{const double p=(id==3||id==11)?s.length-1-s.age%s.length:s.age%s.length;a=s.captured(p,0);b=s.captured(p,1);break;}
   case 8:case 9:case 10:case 27:case 29:{
    const double p=id==29?s.length-1-s.tape:s.tape;a=s.captured(p,0);b=s.captured(p,1);
    double rate=id==9?.5:id==10?2:id==27?std::min(1.f,float(s.age)/(sr*.35f)):id==29?std::min(3.f,1.f+float(s.age)/(sr*.3f)):s.speed;
    s.tape=std::fmod(s.tape+rate,s.length);if(id==8)s.speed=std::max(0.f,s.speed-1.f/std::max(1,quarter));break;}
   case 4:case 12:case 13:case 30:case 31:case 34:{
    const float hz=id==4?270.f:id==12?350.f:id==13?900.f:id==30?800+600*wave:id==31?220+1700*(wave*.5f+.5f):500+260*wave;
    const float f=2*std::sin(pi*std::min(sr*.12f,hz)/sr),q=id==31?.38f:1.25f;
    float* dst[]={&a,&b};const float src[]={l,r};
    for(int c=0;c<2;++c){s.low[c]+=f*s.band[c];const float high=src[c]-s.low[c]-q*s.band[c];s.band[c]+=f*high;
     *dst[c]=id==4||id==31?s.low[c]:id==12?high:id==30?src[c]-s.band[c]*.8f:s.band[c];
     if(id==34){const float f2=2*std::sin(pi*std::min(sr*.12f,1400+450*wave)/sr);s.formLow[c]+=f2*s.formBand[c];const float h=src[c]-s.formLow[c]-.9f*s.formBand[c];s.formBand[c]+=f2*h;*dst[c]=.65f*(s.band[c]+s.formBand[c]);}
    }break;}
   case 5:a=std::round(l*16)/16;b=std::round(r*16)/16;break;
   case 6:a=l+.6f*s.read(s.delay,s.delayWrite-quarter,0);b=r+.6f*s.read(s.delay,s.delayWrite-quarter,1);break;
   case 7:case 26:{const int cycle=s.age%quarter;const float g=id==7?(cycle<quarter/2?1:.05f):(cycle<(s.age/quarter%2?quarter/3:quarter*2/3)?1:.03f);a=l*g;b=r*g;break;}
   case 14:a=std::tanh(l*6);b=std::tanh(r*6);break;
   case 15:a=std::sin(l*3*pi);b=std::sin(r*3*pi);break;
   case 16:{const float carrier=std::sin(2*pi*50*s.age/sr);a=l*carrier;b=r*carrier;break;}
   case 17:{const float g=.55f+.45f*wave;a=l*g;b=r*g;break;}
   case 18:{const float g=.5f+.5f*wave;a=l*g;b=r*(1-g);break;}
   case 19:a=r;b=l;break;
   case 20:a=b=.5f*(l+r);break;
   case 21:{const float mid=.5f*(l+r),side=l-r;a=mid+side;b=mid-side;break;}
   case 22:{const float g=.1f+.9f*static_cast<float>(s.age%quarter)/quarter;a=l*g;b=r*g;break;}
   case 24:a=b=0;break;
   case 28:{const double mod=sr*(.008+.0025*std::sin(2*pi*.7*s.age/sr)+.0004*std::sin(2*pi*6.7*s.age/sr));a=s.read(s.delay,s.delayWrite-mod,0);b=s.read(s.delay,s.delayWrite-mod*1.013,1);break;}
   case 32:case 33:case 58:{const float delay=sr*(id==58?.0025f+.002f*wave:.0041f);const float sign=id==33?-1:1;
    const float dl=s.read(s.delay,s.delayWrite-delay,0),dr=s.read(s.delay,s.delayWrite-delay*1.023,1);
    a=.7f*l+sign*.6f*dl;b=.7f*r+sign*.6f*dr;s.delay[s.delayWrite][0]=soft(l+dl*.4f);s.delay[s.delayWrite][1]=soft(r+dr*.4f);break;}
   case 35:case 36:case 37:case 38:case 39:case 40:case 41:{
    float dl=0,dr=0;
    if(id==37||id==39){const float times[]={.019f,.031f,.043f,.071f};for(int k=0;k<4;++k){dl+=s.read(s.delay,s.delayWrite-sr*times[k],k%2)*(.27f-k*.035f);dr+=s.read(s.delay,s.delayWrite-sr*times[3-k]*1.07,(k+1)%2)*(.27f-k*.035f);}}
    else{dl=s.read(s.delay,s.delayWrite-quarter*(id==35?2:3),id==35?1:0);dr=s.read(s.delay,s.delayWrite-quarter*(id==35?3:4),id==35?0:1);}
    if(id==36||id==39){for(int c=0;c<2;++c){const float tap=c?dr:dl;s.echoLow[c]+=.12f*(tap-s.echoLow[c]);s.delay[s.delayWrite][c]=soft((c?r:l)+s.echoLow[c]*(id==39?.61f:.7f));}dl=s.echoLow[0];dr=s.echoLow[1];}
    if(id==38){dl+=.5f*s.grain(0,52,sr);dr+=.5f*s.grain(1,52,sr);}
    if(id==40||id==41){a=s.captured(id==41?s.length-1-s.age%s.length:s.age%s.length,0);b=s.captured(id==41?s.length-1-s.age%s.length:s.age%s.length,1);a+=.5f*dl;b+=.5f*dr;}
    else{a=l+(id==37?.7f:.55f)*dl;b=r+(id==37?.7f:.55f)*dr;}break;}
   case 42:case 43:{const int slices=id==42?4:8,part=std::max(1,s.length/slices),step=(s.age/part)%slices;static constexpr int order[]={0,4,2,7,1,5,3,6};const int target=id==42?(step*3)%4:order[step];a=s.captured(target*part+s.age%part,0);b=s.captured(target*part+s.age%part,1);break;}
   case 44:{const float rate=.25f+.75f*(1-float(s.age%beat)/beat);a=s.captured(s.tape,0);b=s.captured(s.tape,1);s.tape=std::fmod(s.tape+rate,s.length);break;}
   case 45:{const float g=(s.age/(quarter/4))%3?1.f:0.f;a=g*std::round(l*128)/128;b=g*std::round(r*128)/128;break;}
   case 46:{const bool side=(s.age/(quarter/2))%2;a=side?l:0;b=side?0:r;break;}
   case 47:{const float g=(s.age/quarter)%4==3?0.f:1.f;a=l*g;b=r*g;break;}
   case 48:case 49:case 50:case 51:case 52:case 53:case 54:case 55:case 56:
    a=s.grain(0,id,sr);b=s.grain(1,id,sr);if(id==56){a=.7f*a+.4f*s.read(s.delay,s.delayWrite-sr*.0037f,1);b=.7f*b+.4f*s.read(s.delay,s.delayWrite-sr*.0051f,0);}break;
   case 57:{const double d=sr*(.015+.004*std::sin(2*pi*.35*s.age/sr));a=.65f*l+.5f*s.read(s.delay,s.delayWrite-d,0);b=.65f*r+.5f*s.read(s.delay,s.delayWrite-sr*.030+d*.45,1);break;}
   case 59:{const double d=sr*(.008+.002*std::sin(2*pi*5*s.age/sr));a=s.read(s.delay,s.delayWrite-d,0);b=s.read(s.delay,s.delayWrite-d,1);break;}
   case 60:if(s.age%12==0){s.held[0]=l;s.held[1]=r;}a=s.held[0];b=s.held[1];break;
   case 61:case 64:{const float src[]={l,r};float* dst[]={&a,&b};for(int c=0;c<2;++c){const float x=id==61?std::fabs(src[c])*1.8f:std::tanh(src[c]*5+.3f)*.85f;*dst[c]=x-s.dcIn[c]+.995f*s.dcOut[c];s.dcIn[c]=x;s.dcOut[c]=*dst[c];}break;}
   case 62:a=std::clamp(l*4,-.65f,.65f);b=std::clamp(r*4,-.65f,.65f);break;
   case 63:{const auto crush=[](float x){const int v=static_cast<int>(std::clamp(x,-1.f,1.f)*127);const int u=(v+128)^0x55;return (u-128)/160.f;};a=crush(l);b=crush(r);break;}
   }
   ++s.age;s.phase+=1.0/quarter;if(s.phase>=1)s.phase-=1;
  }
  s.wet+=((request?1.f:0.f)-s.wet)*ramp;if(!request&&s.wet<.00001f)s.wet=0;
  float values[]={l+s.wet*(a-l),r+s.wet*(b-r)};
  for(int c=0;c<2;++c){float value=values[c];if(s.transition>0)value=s.last[c]+(value-s.last[c])/s.transition;
   if(!std::isfinite(value))value=0;out[2*i+c]=s.wet==0?value:std::clamp(value,-2.f,2.f);s.last[c]=out[2*i+c];}
  if(s.transition>0)--s.transition;
  s.delayWrite=(s.delayWrite+1)%memory;
 }
 s.lastRequest=request;if(!request&&s.wet==0)s.active=0;
}
}
