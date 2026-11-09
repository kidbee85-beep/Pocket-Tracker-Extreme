#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <string>
namespace studio {
enum Control {ENABLE,FX,FX_MIX,COLOR,TEXTURE,FEEDBACK,DRIVE,THRESHOLD,RATIO,ATTACK,RELEASE,WIDTH,OUTPUT,CEILING,HIGH_PASS,COUNT};
inline constexpr std::array<int,COUNT> defaults={0,0,0,128,128,32,0,255,0,32,96,128,128,250,0};
const char* effect_name(int id);
int effect_count();
int effect_preset_count();
std::string effect_preset_name(int id);
std::array<int,COUNT> effect_preset(int id);
int master_preset_count();
const char* master_preset_name(int id);
std::array<int,COUNT> master_preset(int id);
class Rack {
public:
 Rack();~Rack();
 // Call with processing stopped: clears histories without allocating audio memory.
 void reset();
 std::array<std::atomic<int>,COUNT> control;
 std::atomic<int> punch{0}, tempo{120};
 void process(float* interleaved,int frames,int channels,float sampleRate);
private:
 struct State;std::unique_ptr<State> s_;
};
}
