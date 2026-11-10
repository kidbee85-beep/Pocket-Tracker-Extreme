// GPL-3.0-or-later. Independent format reader, no upstream parser code copied.
// Layout references (v6): Twinside/m8-file-parser src/{songs,version}.rs and
// src/instruments/{mod,sampler,common}.rs. This migrates supported data, not firmware.
#include "m8_import.h"
#include "songcore/effects.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
namespace studio {
M8Import import_m8(const std::string& bytes,const std::function<std::string(const std::string&)>& samplePath){
 using namespace songcore;
 M8Import result;
 // Includes all v6 pools. Later optional metadata is retained in the source archive.
 constexpr size_t minimumSize=112294,maximumSize=2097152;
 if(bytes.size()<minimumSize||bytes.size()>maximumSize||bytes.compare(0,10,std::string("M8VERSION\0",10))!=0){result.error="INVALID / TRUNCATED M8 SONG";return result;}
 const auto u=[&](size_t i){return static_cast<int>(static_cast<unsigned char>(bytes[i]));};
 const int major=u(11)&15,minor=u(10)>>4,patch=u(10)&15;
 if(major!=6||minor>6){result.error="M8 VERSION NOT SUPPORTED (6.0-6.6 ONLY)";return result;}
 uint32_t bits=static_cast<uint32_t>(u(143))|(static_cast<uint32_t>(u(144))<<8)|(static_cast<uint32_t>(u(145))<<16)|(static_cast<uint32_t>(u(146))<<24);
 float tempo;std::memcpy(&tempo,&bits,4);
 if(!std::isfinite(tempo)||tempo<20||tempo>999){result.error="INVALID M8 TEMPO";return result;}
 result.project=make_default_project();auto& p=result.project;
 const auto text=[&](size_t start,size_t count){std::string out;for(size_t i=0;i<count&&u(start+i);++i)out+=u(start+i)<32?'?':bytes[start+i];return out;};
 p.name=text(148,12);if(p.name.empty())p.name="M8 IMPORT";
 p.tempo=static_cast<int>(std::round(tempo));p.transpose=static_cast<int8_t>(u(142));p.masterVolume=u(206);
 std::array<bool,128> playable{};
 int samplers=0,missing=0,unsupported=0,mods=0,inherit=0,omittedNotes=0,highTables=0,unsupportedModes=0;
 std::map<int,int> skipped;
 std::vector<std::string> missingPaths;
 for(int t=0;t<8;++t){p.tracks[t].chainRefs.resize(256,-1);p.tracks[t].volume=u(208+t);for(int row=0;row<256;++row){int c=u(750+row*8+t);p.tracks[t].chainRefs[row]=c==255?-1:c;}}
 for(int c=0;c<255;++c)for(int row=0;row<16;++row){size_t at=39518+c*32+row*2;int ref=u(at);p.chains[c].phraseRefs[row]=ref==255?-1:ref;p.chains[c].transposeValues[row]=static_cast<int8_t>(u(at+1));}
 for(int i=0;i<128;++i){
  size_t at=80446+i*215;int type=u(at);if(type==255)continue;
  // Native Mutable models are useful locally, but are not faithful M8 synth mappings.
  // Leave unsupported slots empty; no synthetic substitute or external MIDI is activated.
  if(type!=2){++unsupported;continue;}
  const int mode=u(at+18),slice=u(at+19);
  if(mode>5||(slice!=0&&slice!=255)){++unsupportedModes;continue;}
  const std::string path=text(at+87,128);std::string resolved;
  try{if(!path.empty()&&samplePath)resolved=samplePath(path);}catch(...){resolved.clear();}
  if(resolved.empty()){++missing;missingPaths.push_back("Instrument "+std::to_string(i)+": "+(path.empty()?"(no sample path)":path));continue;}
  auto& ins=p.instruments[i];playable[i]=true;++samplers;
  ins.name=text(at+1,12);if(ins.name.empty())ins.name="M8 SAMPLER "+std::to_string(i);
  ins.sampleFilePath=resolved;ins.volume=u(at+15);ins.detune=u(at+17);
  ins.transposeEnabled=(u(at+13)&1)!=0;
  ins.tableTicRate=std::clamp(std::max(1,u(at+14))*2,1,255);ins.tableId=i;
  ins.pan=u(at+29);
  // M8 mixer algorithms are unavailable here: do not silently substitute local sends.
  if(u(at+31))++skipped[0x8f];if(u(at+32))++skipped[0x90];if(u(at+33))++skipped[0x91];
  ins.reverbSend=0;ins.delaySend=0;
  ins.sampleStart=u(at+20);ins.sampleEnd=std::max(ins.sampleStart+1,std::min(255,ins.sampleStart+u(at+22)));
  ins.sampleStart=std::min(ins.sampleStart,254);ins.sampleEnd=std::min(ins.sampleEnd,255);ins.loopStart=u(at+21);
  ins.reverse=mode==1||mode==3||mode==5;ins.loopMode=mode>=4?"png":mode>=2?"fwd":"off";
  ins.root=note_from_midi(std::clamp(60-static_cast<int8_t>(u(at+16)),0,127));
  int filt=u(at+24);ins.filterType=filt==1?"lp":filt==2?"hp":filt==3?"bp":"off";ins.filterCut=u(at+25);ins.filterRes=u(at+26);
  if(filt>3)++skipped[0x200+filt];
  // The source envelopes have different rate/curve and destination semantics.
  // Keep them only in the raw source until an explicit equivalent is implemented.
  for(int m=0;m<4;++m)if((u(at+63+m*6)&15)!=0)++mods;
 }
 auto fx=[&](int key,int value,bool sampler){
  switch(key){
   case 0xff:return FX_NONE;
   case 3:if(value<32)return FX_GRV;break;
   case 4:if(value<16||value==255)return FX_HOP;break;
   case 5:return FX_KILL;
   case 0x14:if(value<128)return FX_TBL;++highTables;break;
   case 0x15:if(value<16)return FX_THO;break;
   case 0x16:return FX_TIC;
   case 0x80:if(sampler)return FX_VOLUME;break;
   case 0x89:if(sampler)return FX_CUT;break;
   case 0x8a:if(sampler)return FX_RES;break;
   case 0x8d:if(sampler)return FX_PAN;break;
  }
  ++skipped[key];return FX_NONE;
 };
 for(int ph=0;ph<255;++ph){
  int instrument=-1,volume=127;
  for(int row=0;row<16;++row){
   size_t at=2798+ph*144+row*9;auto& step=p.phrases[ph].steps[row];const int n=u(at),rawIns=u(at+2);
   if(rawIns<128)instrument=rawIns;else if(rawIns!=255)instrument=-1;
   if(u(at+1)<128)volume=u(at+1);
   const bool available=instrument>=0&&playable[instrument];
   // Notes >=80 are OFF in M8 v6. Unsupported note-ons release a previous voice,
   // rather than accidentally replaying instrument 00 or extending its sustain.
   step.note=n==255?Note::EMPTY():n>=128?Note::OFF():available&&n<=103?Note{n%12,1+n/12}:Note::OFF();
   if(n<128&&(!available||n>103)){++omittedNotes;if(instrument<0)++inherit;}
   step.instrument=available?instrument:0;step.volume=volume;
   for(int j=0;j<3;++j){int key=u(at+3+j*2),v=u(at+4+j*2),mapped=fx(key,v,available);step_set_fx(step,j+1,mapped,mapped==FX_NONE?0:v);}
  }
 }
 for(int t=0;t<128;++t)for(int row=0;row<16;++row){
  size_t at=47678+t*128+row*8;auto& step=p.tables[t].rows[row];step.transpose=u(at);step.volume=u(at+1)==255?-1:std::min(255,u(at+1)*2);
  int* types[]={&step.fx1Type,&step.fx2Type,&step.fx3Type};int* values[]={&step.fx1Value,&step.fx2Value,&step.fx3Value};
  for(int j=0;j<3;++j){int value=u(at+3+j*2);*types[j]=fx(u(at+2+j*2),value,playable[t]);*values[j]=*types[j]==FX_NONE?0:value;}
 }
 for(int g=0;g<32;++g){
  p.grooves[g].steps.clear();int ppqn=minor>=5&&bytes.size()>=112326?std::min(3,u(112294+g)):0;
  for(int i=0;i<16&&u(238+g*16+i)!=255;++i)p.grooves[g].steps.push_back(std::max(1,static_cast<int>(std::round(u(238+g*16+i)*2.f/(1<<ppqn)))));
  if(p.grooves[g].steps.empty())p.grooves[g].steps={12};
 }
 int skippedCount=0;for(auto item:skipped)skippedCount+=item.second;
 std::ostringstream report;
 report<<"M8 supported-data import from "<<major<<'.'<<minor<<'.'<<patch<<"\n"
 <<"Original .m8s is never written; all source bytes are preserved in the new .ptp.\n"
 <<"Arrangement, chains, phrases, basic sampler parameters, 128 tables and grooves copied.\n"
 <<"Available samplers: "<<samplers<<"; missing samples: "<<missing<<"\n"
 <<"Unsupported synth/MIDI/external instruments omitted: "<<unsupported<<"\n"
 <<"Unsupported sampler modes/slicing omitted: "<<unsupportedModes<<"\n"
 <<"Unavailable note-ons converted to note-off: "<<omittedNotes<<"\n"
 <<"Unresolved cross-phrase instrument inheritance notes: "<<inherit<<"\n"
 <<"Unsupported modulation slots omitted: "<<mods<<"\n"
 <<"Unsupported effects omitted: "<<skippedCount<<"; table references >=128: "<<highTables<<"\n"
 <<"M8 EQ, mixer effects, scale data, MIDI, envelope and pitch-FX mappings are not imported.\n"
 <<"Sampler root pitch, gain, trim/loop points and groove timing need an original M8 comparison.\n";
 for(int i=0;i<128;++i){size_t at=80446+i*215;if(u(at)!=255&&!playable[i])report<<"Omitted instrument "<<i<<": "<<text(at+1,12)<<" (source type "<<u(at)<<")\n";}
 for(const auto& path:missingPaths)report<<"Missing sample "<<path<<"\n";
 for(auto item:skipped)report<<"Omitted command/mode 0x"<<std::hex<<item.first<<std::dec<<": "<<item.second<<" occurrence(s)\n";
 result.report=report.str();
 result.summary={"M8 IMPORT - SUPPORTED DATA ONLY",std::to_string(samplers)+" samplers ready / "+std::to_string(missing)+" missing samples",std::to_string(unsupported+unsupportedModes)+" unsupported instruments omitted",std::to_string(omittedNotes)+" unavailable notes silenced",std::to_string(skippedCount)+" effects / "+std::to_string(mods)+" modulators omitted","Full details saved in Import Reports.","Original source bytes retained in the copy.","SAVE creates a PocketTracker .ptp copy.","A: IMPORT COPY     B: KEEP CURRENT PROJECT"};
 p.m8ImportReport=result.report;static const char hex[]="0123456789abcdef";
 p.m8SourceHex.reserve(bytes.size()*2);for(unsigned char byte:bytes){p.m8SourceHex+=hex[byte>>4];p.m8SourceHex+=hex[byte&15];}
 result.ok=true;return result;
}
}
