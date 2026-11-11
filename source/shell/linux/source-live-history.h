#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace pt::platform::source_live {

// This buffer belongs to the source worker. The audio callback keeps reading
// the existing output FIFO, independently of history writes and WAV exports.
class StereoHistory {
public:
    void reset(int64_t capacity) {
        samples_.assign(size_t(capacity) * 2, 0);
        end_ = 0;
    }

    void clear() {
        std::vector<int16_t>().swap(samples_);
        end_ = 0;
    }

    int64_t capacity() const { return int64_t(samples_.size() / 2); }
    int64_t begin() const { return std::max<int64_t>(0, end_ - capacity()); }
    int64_t end() const { return end_; }

    void append(const int16_t* frames, int64_t count) {
        const auto skipped = std::max<int64_t>(0, count - capacity());
        end_ += skipped;
        frames += skipped * 2;
        count -= skipped;
        if (!count) return;
        const auto offset = end_ % capacity();
        const auto first = std::min(count, capacity() - offset);
        std::copy_n(frames, first * 2, samples_.data() + offset * 2);
        std::copy_n(frames + first * 2, (count - first) * 2, samples_.data());
        end_ += count;
    }

    bool contains(int64_t first, int64_t last) const {
        return first >= begin() && last > first && last <= end();
    }

    const int16_t* frame(int64_t at) const {
        return samples_.data() + (at % capacity()) * 2;
    }

private:
    std::vector<int16_t> samples_;
    int64_t end_ = 0;
};

} // namespace pt::platform::source_live
