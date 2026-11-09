#pragma once
#include <memory>
namespace studio {
inline constexpr int kPunchBanks=8, kPunchPads=8, kPunchCount=64;
const char* punch_bank_name(int bank);
const char* punch_name(int id);
const char* punch_detail(int id);
int punch_id(int bank,int pad);
int punch_bank(int id);
int punch_pad(int id);
// The audio thread owns all histories. Controls are sampled by Rack once per block.
// Construction/reset happen with audio stopped; process never allocates or locks.
class PunchProcessor {
public:
 PunchProcessor();~PunchProcessor();
 void reset();
 void process(float* stereo,int frames,float sampleRate,int requested,int bpm);
private:
 struct State;std::unique_ptr<State> s_;
};
}
