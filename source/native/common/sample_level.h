#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace pt::sample_level {

inline constexpr std::array<int, 3> normalize_targets = {-6, -3, -1};
inline constexpr int min_gain_db = -36;
inline constexpr int max_gain_db = 36;
inline constexpr float silence_peak = 0.0001f;

inline float linear(float db) { return std::pow(10.f, db / 20.f); }
inline float db(float gain) { return 20.f * std::log10(gain); }

struct Plan {
    float factor = 1.f;
    bool limited = false;
};

inline Plan plan(float peak, bool normalize, int value_db) {
    if (peak <= 0.f || (normalize && peak < silence_peak)) return {};
    if (normalize) return {linear(static_cast<float>(value_db)) / peak, false};
    const float requested = linear(static_cast<float>(value_db));
    // A boost reserves 1 dB when possible. On a loud input the guard
    // limits the boost without turning it into an unrequested cut.
    const float ceiling = std::max(1.f, linear(-1.f) / peak);
    return {std::min(requested, ceiling), requested > ceiling};
}

} // namespace pt::sample_level
