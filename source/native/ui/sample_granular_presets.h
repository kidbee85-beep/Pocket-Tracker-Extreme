// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "studio/sample_granular.h"
#include <cstdint>
#include <string>

namespace pt::ui {
inline constexpr int kSampleGranularCategories=8,kSampleGranularPerCategory=16;
inline constexpr int kSampleGranularPickerColumns=4;
// Favorites persist bank * slots + slot, so keep the existing factory IDs stable.
inline constexpr int kSampleGranularFactoryCount=kSampleGranularCategories*kSampleGranularPerCategory;
inline constexpr int kSampleGranularFavoritesBank=kSampleGranularCategories;
inline constexpr const char* kSampleGranularPresetExtension="ptgrain";
struct SampleGranularPreset {
 const char* name=nullptr;
 const char* description=nullptr;
 int category=0;
 studio::granular::Parameters parameters{};
};
const SampleGranularPreset& sample_granular_factory_preset(int index);
const char* sample_granular_category(int category);
const char* sample_granular_category_short(int category);
struct SampleGranularPresetFile {
 studio::granular::Parameters parameters{};
 std::string name,sampleReference;
 uint32_t seed=1;
 bool hasSample=false;
};
bool sample_granular_relative_reference(const std::string& path);
bool parse_sample_granular_preset(const std::string& text,SampleGranularPresetFile& output,std::string& error);
std::string serialize_sample_granular_preset(const SampleGranularPresetFile& preset);
std::string sample_granular_factory_json(int index);
} // namespace pt::ui
