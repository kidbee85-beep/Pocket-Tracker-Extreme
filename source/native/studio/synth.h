#pragma once
#include <array>
#include <memory>
#include <string>
#include "synth_params.h"
namespace studio {
constexpr int variations = 4; // Legacy URI stride; never change.
int variation_count();
int preset_code(int model,int variation);
int preset_model(int code);
int preset_variation(int code);
std::array<int,3> preset_macros(int code);
// IDs 0..81 are the original Mutable models; 82..93 add Elements articulations,
// 94..101 synthetic granular oscillators, 102 onward original Studio algorithms.
inline constexpr int kOctaveOrbitModel = 112;
inline constexpr int kGravityFirstModel = 113;
inline constexpr int kGravityModelCount = 3;
inline constexpr bool is_gravity_model(int model) {
    return model >= kGravityFirstModel && model < kGravityFirstModel + kGravityModelCount;
}
int model_count();
const char* model_name(int model);
bool synth_needs_strike(int model);
// UI traits describe the controls actually consumed by each renderer. Invalid models/indices
// have zero controls / an empty label; URI parameter packing remains unchanged.
int macro_count(int model);
const char* macro_name(int model,int index);
std::string preset_name(int code);
int custom_code(int model,int a,int b,int c);
std::string custom_uri(int model,int a,int b,int c);
int uri_code(const char* path);
bool valid_code(int code);
std::string uri(int code);
class Synth {
public:
 Synth(); ~Synth();
 Synth(const Synth&)=delete; Synth& operator=(const Synth&)=delete;
 // Call with processing stopped, before a fresh project or offline render.
 void reset();
 void start(int code);
 // Audio thread only: updates never allocate and same-model edits retain oscillator/envelope state.
 void update(int code,const SynthParams& params);
 int code() const { return code_; }
 float next(float hz, float sampleRate);
 bool active() const { return code_>=0; }
private:
 struct State; std::unique_ptr<State> s_;
 int code_=-1;
};
}
