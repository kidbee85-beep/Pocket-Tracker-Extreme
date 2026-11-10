#!/usr/bin/env python3
"""Compile the unchanged Android backend against deterministic Oboe route failures.

The fake's unsafe MMap query aborts, reproducing the pinned Oboe extension's
null-underlying-stream hazard without requiring an Android device.
"""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OBOE = r'''
#pragma once
#include <cstdlib>
#include <memory>
#include <vector>
namespace oboe {
enum class Result { OK, ErrorInternal };
enum class AudioApi { Unspecified, AAudio, OpenSLES };
enum class AudioFormat { Float };
enum class ChannelCount { Stereo=2 };
enum class PerformanceMode { None, LowLatency };
enum class SharingMode { Exclusive, Shared };
enum class StreamState { Started, Paused, Stopped, Closed };
enum class DataCallbackResult { Continue };
template<class T> struct ResultWithValue {
    bool valid=false; T data{};
    explicit operator bool() const { return valid; }
    T value() const { return data; }
};
template<class T> const char* convertToText(T) { return "fake"; }
class AudioStream;
struct AudioStreamDataCallback { virtual ~AudioStreamDataCallback()=default;
 virtual DataCallbackResult onAudioReady(AudioStream*,void*,int32_t)=0; };
struct AudioStreamErrorCallback { virtual ~AudioStreamErrorCallback()=default;
 virtual void onErrorAfterClose(AudioStream*,Result)=0; };
struct DefaultStreamValues { inline static int FramesPerBurst=0; };
struct Plan { Result opened; AudioApi actual; Result started=Result::OK; };
inline std::vector<Plan> plans;
inline std::vector<AudioApi> requests;
inline int closes=0, stops=0, starts=0;
inline bool mmapSupported=true, mmapEnabled=true;
class AudioStream {
public:
 AudioApi api; Result startResult; StreamState state=StreamState::Stopped;
 int buffer=384;
 AudioStream(Plan p):api(p.actual),startResult(p.started){}
 int getFramesPerBurst() { return 192; }
 void setBufferSizeInFrames(int n) { buffer=n; }
 int getBufferSizeInFrames() { return buffer; }
 int getSampleRate() { return 48000; }
 int getChannelCount() { return 2; }
 AudioApi getAudioApi() { return api; }
 PerformanceMode getPerformanceMode() { return PerformanceMode::LowLatency; }
 SharingMode getSharingMode() { return SharingMode::Shared; }
 Result requestStart() { ++starts; if(startResult==Result::OK)state=StreamState::Started; return startResult; }
 Result start() { return requestStart(); }
 Result stop() { ++stops; state=StreamState::Stopped; return Result::OK; }
 Result close() { ++closes; state=StreamState::Closed; return Result::OK; }
 StreamState getState() { return state; }
 ResultWithValue<double> calculateLatencyMillis() { return {}; }
};
struct OboeExtensions {
 static bool isMMapSupported() { return mmapSupported; }
 static void setMMapEnabled(bool b) { mmapEnabled=b; }
 static bool isMMapUsed(AudioStream*) { std::abort(); }
};
class AudioStreamBuilder {
 AudioApi requested=AudioApi::Unspecified;
public:
 void setDataCallback(AudioStreamDataCallback*){}
 void setErrorCallback(AudioStreamErrorCallback*){}
 void setFormat(AudioFormat){}
 void setChannelCount(ChannelCount){}
 void setSampleRate(int){}
 void setAudioApi(AudioApi a){requested=a;}
 void setPerformanceMode(PerformanceMode){}
 void setSharingMode(SharingMode){}
 Result openStream(std::shared_ptr<AudioStream>& stream) {
   requests.push_back(requested);
   if(plans.empty())return Result::ErrorInternal;
   const Plan p=plans.front();plans.erase(plans.begin());
   if(p.opened==Result::OK)stream=std::make_shared<AudioStream>(p);
   return p.opened;
 }
};
}
'''
CORE = r'''
#pragma once
class AudioEngine { public:
 static constexpr int SAMPLE_RATE=48000;
 int rate=0, calls=0;
 void setDeviceSampleRate(int r){rate=r;}
 void processLiveBlock(float*,int,int,float){++calls;}
};
'''
CHECK = r'''
#include "oboe-audio-engine.h"
#include "audio-engine.h"
#include <cassert>
#include <cstdio>
#include <fstream>
using namespace oboe;
void reset(){plans.clear();requests.clear();closes=stops=starts=0;mmapSupported=true;}
int main(int argc,char**argv){
 AudioEngine core;
 // OpenSL ES has no AAudio underlying pointer: the original unsafe query aborts.
 reset(); plans={{Result::OK,AudioApi::OpenSLES}};
 {OboeAudioEngine b(&core);assert(b.openStream());assert(core.rate==48000);
  assert(!b.deviceLost());assert(b.outputLatency().frames==384);
  float out[8]{}; AudioStream stream({Result::OK,AudioApi::OpenSLES});
  assert(b.onAudioReady(&stream,out,4)==DataCallbackResult::Continue);assert(core.calls==1);
  b.setPaused(true);b.setPaused(false);b.onErrorAfterClose(nullptr,Result::ErrorInternal);
  assert(b.deviceLost());b.closeStream();} assert(closes==1);
 // A route dies after opening: close it and try the explicit legacy backend once.
 reset();plans={{Result::OK,AudioApi::AAudio,Result::ErrorInternal},{Result::OK,AudioApi::OpenSLES}};
 {OboeAudioEngine b(&core);assert(b.openStream());assert(requests.size()==2);
  assert(requests[0]==AudioApi::Unspecified&&requests[1]==AudioApi::OpenSLES);assert(closes==1);}
 assert(closes==2);
 // A closed AAudio stream still has AAudio metadata. Never dereference its private pointer.
 reset();plans={{Result::OK,AudioApi::AAudio,Result::ErrorInternal},{Result::OK,AudioApi::OpenSLES,Result::ErrorInternal}};
 {OboeAudioEngine b(&core);assert(!b.openStream());assert(closes==2);assert(!b.deviceLost());}
 // All open variants fail: bounded failure, no stream/latency dereference, later retry works.
 reset();{OboeAudioEngine b(&core);assert(!b.openStream());assert(requests.size()==7);
  assert(b.outputLatency().frames==0);plans={{Result::OK,AudioApi::OpenSLES}};assert(b.openStream());}
 // Capture flips cause precisely one eligible AAudio reopen, no OpenSL ES reopen loop.
 reset();std::atomic<bool> captured{false};plans={{Result::OK,AudioApi::AAudio}};
 {OboeAudioEngine b(&core);b.setPlaybackCaptured(&captured);assert(b.openStream());
  assert(!b.deviceLost());captured=true;assert(b.deviceLost());b.closeStream();
  plans={{Result::OK,AudioApi::AAudio}};assert(b.openStream());assert(!b.deviceLost());assert(mmapEnabled);
  captured=false;assert(b.deviceLost());b.closeStream();plans={{Result::OK,AudioApi::OpenSLES}};
  assert(b.openStream());captured=true;assert(!b.deviceLost());captured=false;assert(!b.deviceLost());}
 // Existing slow-start guard deterministically selects OpenSL ES on the first attempt.
 reset();assert(argc==2);std::ofstream(argv[1])<<"guard";plans={{Result::OK,AudioApi::OpenSLES}};
 {OboeAudioEngine b(&core);b.setSlowOpenMarker(argv[1]);assert(b.openStream());
  assert(requests.size()==1&&requests[0]==AudioApi::OpenSLES);}
 std::remove(argv[1]);
 std::puts("PASS: 6 Android backend route/start/capture/error regression groups");
}
'''

with tempfile.TemporaryDirectory(prefix="pte-oboe-check-") as tmp:
    stage = Path(tmp)
    (stage / "oboe").mkdir()
    backend = ROOT / "native/oboe-audio-engine.cpp"
    shutil.copyfile(backend, stage / backend.name)
    (stage / "oboe/Oboe.h").write_text(OBOE)
    (stage / "audio-engine.h").write_text(CORE)
    (stage / "audio-defs.h").write_text("#define LOGD(...) ((void)0)\n#define LOGE(...) ((void)0)\n")
    (stage / "check.cpp").write_text(CHECK)
    subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-variable",
        "-I", str(stage), "-I", str(ROOT / "native"), str(stage/backend.name),
        str(stage/"check.cpp"), "-o", str(stage/"check")], check=True)
    subprocess.run([str(stage/"check"), str(stage/"slow-open-marker")], check=True)
    print("Actual backend SHA256:", hashlib.sha256(backend.read_bytes()).hexdigest())
