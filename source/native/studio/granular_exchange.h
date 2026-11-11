// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace studio::granular {
// One publisher, one reader. Every data word is atomic32: no POD data race, no 64-bit atomic
// fallback locks on ARM32. Sequential consistency deliberately favors a simple auditable contract.
// A reader makes at most four attempts and retains its previous copy if publication overlaps.
template<class T> class Exchange32 {
    static_assert(std::is_trivially_copyable<T>::value, "POD publication only");
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "Requires lock-free atomic32");
    static constexpr size_t words = (sizeof(T) + 3) / 4;
public:
    void publish(const T& value) noexcept {
        std::array<uint32_t, words> raw{};
        std::memcpy(raw.data(), &value, sizeof(T));
        const uint32_t before = sequence_.load();
        sequence_.store(before + 1);
        for (size_t i = 0; i < words; ++i) data_[i].store(raw[i]);
        sequence_.store(before + 2);
    }
    bool read(T& value, uint32_t& seen) const noexcept {
        for (int attempt = 0; attempt < 4; ++attempt) {
            const uint32_t before = sequence_.load();
            if (before & 1u) continue;
            if (before == seen) return false;
            std::array<uint32_t, words> raw{};
            for (size_t i = 0; i < words; ++i) raw[i] = data_[i].load();
            if (sequence_.load() != before) continue;
            std::memcpy(&value, raw.data(), sizeof(T));
            seen = before;
            return true;
        }
        return false;
    }
private:
    std::atomic<uint32_t> sequence_{0};
    std::array<std::atomic<uint32_t>, words> data_{};
};
} // namespace studio::granular
