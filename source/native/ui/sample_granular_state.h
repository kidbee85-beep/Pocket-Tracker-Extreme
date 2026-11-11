// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "studio/sample_granular.h"
#include "ui/screen.h"
#include "ui/sample_granular_recording.h"
#include <array>
#include <cstdint>
#include <string>
namespace songcore { struct Project; }
namespace pt::ui {
inline constexpr int kSampleGranularWaveBins=128;
inline constexpr int kGranularWaveRow=8;
inline constexpr int kGranularCompareAction=kGranularActionCount;
inline constexpr int kGranularWorkflowActionCount=kGranularCompareAction+1;
enum class SampleGranularRequest { NONE, LOAD_SOURCE, CROP_SOURCE, LOAD_PRESET, USE_RECORDED_TAKE };
enum class GranularWaveformMode { View, Position };
enum class GranularPresetShortcut { None, Favorites, Mark };
struct GranularPresetPicker {
 const songcore::Project* project=nullptr;
 int instrument=-1,bank=0,item=0;
 bool banksFocused=false,chooseArmed=false;
 int returnBank=0,returnItem=0;
 GranularPresetShortcut shortcut=GranularPresetShortcut::None;
};
struct GranularMorph {
 studio::granular::Parameters a{},b{};
 int amount=-1; // Percent toward B. Negative while only the ordinary A/B comparison is active.
};
struct SampleGranularUIState {
 bool open=false,publisherFocused=false,custom=true,waveformReady=false,waitingForSource=false;
 int instrument=-1,factoryPreset=0,action=0,userSlot=0;
 studio::granular::Parameters parameters{};
 uint32_t seed=1;
 studio::granular::Snapshot snapshot{};
 std::array<float,kSampleGranularWaveBins*2> waveform{};
 uint32_t waveformGeneration=0,replacedGeneration=0;
 int waveformInstrument=-1;
 int64_t waveformFirst=0,waveformEnd=0;
 int waveformZoom=0;
 double waveformCenter=.5; // Normalized within the current playable region.
 GranularWaveformMode waveformMode=GranularWaveformMode::View;
 GranularPresetPicker presetPicker;
 studio::granular::Parameters compareParameters{};
 bool compareAvailable=false,compareLiveB=false;
 GranularMorph compareMorph;
 uint64_t nextPollMs=0;
 uint16_t auditionReleaseMask=0,returnReleaseMask=0; // A=1, B=2; the incoming modal owns its release.
 std::string sourcePath,presetName,presetDirectory;
 SampleGranularRequest request=SampleGranularRequest::NONE;
 bool browserReturn=false,browserFromEditor=false,cropReturn=false;
 ScreenType browserEditorReturn=ScreenType::PHRASE;
 int browserInstrument=-1,cropInstrument=-1;
 const songcore::Project* cropProject=nullptr;
 SampleGranularRecordingView recording;
};
} // namespace pt::ui
