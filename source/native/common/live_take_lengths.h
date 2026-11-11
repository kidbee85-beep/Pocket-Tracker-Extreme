#pragma once

#include <array>
#include <cstdint>

namespace pt::sources {
inline constexpr std::array<int, 6> live_take_lengths = {10, 20, 30, 40, 45, 60};

inline constexpr bool valid_live_take_length(int64_t seconds) {
    for (int supported : live_take_lengths) {
        if (seconds == supported) return true;
    }
    return false;
}
} // namespace pt::sources
