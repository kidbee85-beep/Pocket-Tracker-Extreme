#include "studio/m8_import.h"
#include "songcore/project_io.h"
#include "songcore/effects.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <cstdio>
#include <string>
namespace {
std::string fixture(){
 std::string b(112326,static_cast<char>(255));
 b.replace(0,10,std::string("M8VERSION\0",10));b[10]=0x60;b[11]=6;
 float bpm=123.0f;std::memcpy(&b[143],&bpm,4);b.replace(148,12,std::string("IMPORT TEST\0",12));b[142]=0;b[206]=static_cast<char>(255);
 for(int i=0;i<8;++i)b[208+i]=static_cast<char>(255);
 for(int i=0;i<32;++i){b[238+i*16]=6;b[112294+i]=0;}
 b[750]=0;b[39518]=0;b[39519]=0;
 // Instrument00 is a standard WAV sampler; instrument01 is an unsupported FM synth.
 size_t a=80446;b[a]=2;b.replace(a+1,12,std::string("SAMPLER\0\0\0\0\0",12));b[a+13]=1;b[a+14]=3;b[a+15]=static_cast<char>(255);b[a+16]=0;b[a+17]=static_cast<char>(128);b[a+18]=0;b[a+19]=0;b[a+20]=0;b[a+21]=0;b[a+22]=static_cast<char>(255);b[a+24]=0;b[a+25]=0;b[a+26]=0;b[a+29]=static_cast<char>(128);b[a+32]=0;b[a+33]=0;b.replace(a+87,128,std::string("/Samples/test.wav\0",18)+std::string(110,'\0'));
 for(int m=0;m<4;++m)b[a+63+m*6]=0;
 b[a+215]=4;b.replace(a+216,12,std::string("UNAVAILABLE\0",12));
 size_t step=2798;b[step]=36;b[step+1]=127;b[step+2]=0;b[step+3]=3;b[step+4]=0;
 step+=9;b[step]=38;b[step+1]=static_cast<char>(255);b[step+2]=1;b[step+3]=0x0c;b[step+4]=2;
 step+=9;b[step]=static_cast<char>(0x81);b[step+1]=static_cast<char>(255);b[step+2]=static_cast<char>(255);
 // No explicit source instrument in phrase01: must not silently use sampler00.
 b[2798+144]=40;b[2798+144+1]=127;
 return b;
}
std::string unhex(const std::string& text){std::string r;for(size_t i=0;i<text.size();i+=2)r+=static_cast<char>(std::stoi(text.substr(i,2),nullptr,16));return r;}
}
int main(){
 const auto source=fixture();auto resolver=[](const std::string& path){return path=="/Samples/test.wav"?"/test/Samples/test.wav":std::string();};
 auto imported=studio::import_m8(source,resolver);assert(imported.ok);
 auto& p=imported.project;assert(p.name=="IMPORT TEST"&&p.tempo==123);assert(p.tracks[0].chainRefs[0]==0&&p.chains[0].phraseRefs[0]==0);
 assert(p.instruments[0].sampleFilePath=="/test/Samples/test.wav");assert(!p.instruments[1].sampleFilePath&&p.instruments[1].name==songcore::default_instrument_name(1));
 assert(p.phrases[0].steps[0].note==songcore::Note::C4());assert(p.phrases[0].steps[0].fx1Type==songcore::FX_GRV);
 assert(songcore::note_is_off(p.phrases[0].steps[1].note)&&p.phrases[0].steps[1].fx1Type==songcore::FX_NONE);
 assert(songcore::note_is_off(p.phrases[0].steps[2].note));assert(songcore::note_is_off(p.phrases[1].steps[0].note));
 assert(imported.report.find("Unsupported synth/MIDI/external instruments omitted: 1")!=std::string::npos);
 assert(imported.report.find("Unresolved cross-phrase instrument inheritance notes: 1")!=std::string::npos);
 assert(unhex(p.m8SourceHex)==source);
 auto saved=songcore::serialize_project(p);auto reloaded=songcore::parse_project(songcore::json::parse(saved));assert(unhex(reloaded.m8SourceHex)==source&&reloaded.m8ImportReport==imported.report);
 auto missing=studio::import_m8(source,[](const std::string&){return std::string();});assert(missing.ok&&!missing.project.instruments[0].sampleFilePath);assert(songcore::note_is_off(missing.project.phrases[0].steps[0].note));
 assert(!studio::import_m8(source.substr(0,112293),resolver).ok);
 auto bad=source;bad[11]=7;assert(!studio::import_m8(bad,resolver).ok);
 bad=source;bad.replace(0,2,"XX");assert(!studio::import_m8(bad,resolver).ok);
 bad=source;float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(&bad[143],&nan,4);assert(!studio::import_m8(bad,resolver).ok);
 bad=source;bad[80446+18]=9;auto mode=studio::import_m8(bad,resolver);assert(mode.ok&&!mode.project.instruments[0].sampleFilePath&&songcore::note_is_off(mode.project.phrases[0].steps[0].note));
 auto throwing=studio::import_m8(source,[](const std::string&)->std::string{throw 1;});assert(throwing.ok&&!throwing.project.instruments[0].sampleFilePath);
 puts("PASS: M8 v6 arrangement/sampler import, unsupported sound/effect omission, missing samples, OFF/inheritance, raw-source roundtrip, malformed/version/tempo guards");
}
