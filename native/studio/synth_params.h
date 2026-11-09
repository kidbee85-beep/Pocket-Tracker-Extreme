#pragma once
#include <array>
#include <string>
namespace studio {
inline constexpr int kSynthParams = 16;
using SynthParams = std::array<int, kSynthParams>;
inline SynthParams default_synth_params() { SynthParams p{};p.fill(-1);return p; }
// -1 retains the authored preset value. Explicit values are normalized bytes.
int synth_param_count(int model);
const char* synth_param_name(int model,int index);
int synth_param_default(int model,int index);
int synth_param_adjust(int model,int index,int value,int direction,bool coarse);
std::string synth_param_value(int model,int index,int value);
}
