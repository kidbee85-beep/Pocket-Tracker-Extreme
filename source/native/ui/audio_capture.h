#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace pt::ui {
class CaptureBackend {
public:
    virtual ~CaptureBackend()=default;
    virtual std::string inputs_json()=0;
    virtual bool start(int deviceId,int channels,int rate)=0;
    virtual void stop()=0;
    virtual std::string state_json()=0;
};
struct CaptureInput {int id=0;std::string name,kind,detail;bool connected=true;};
struct CaptureView {
    std::vector<CaptureInput> inputs;
    int selected=0,channels=1,rate=48000,requestedId=0,routedId=0,actualChannels=1,actualRate=48000;
    int64_t frames=0;
    float peakL=0,peakR=0;
    std::string state="idle",message,path,routedName,importedPath;
    std::string inputError;
    int targetInstrument=-1;
    bool phoneDetected=false,phoneTrusted=false;
    uint64_t nextPoll=0,nextInputsPoll=0;
    bool active()const{return state=="starting"||state=="recording"||state=="stopping";}
    bool receiving()const{return state=="recording"&&frames>0;}
};
}
