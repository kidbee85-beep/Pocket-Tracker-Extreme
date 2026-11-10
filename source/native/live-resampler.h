#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

// One control thread, one audio thread. All memory is allocated/touched before
// capture starts. The callback only converts into that bounded, owned buffer.
class LiveResampler {
public:
    static constexpr int MAX_SECONDS = 60;
    ~LiveResampler() { stop(); }

    bool start(int rate, int frameLimit = 0) {
        if (recording()) return false;
        stop();
        if (rate < 8000 || rate > 192000) return false;
        const int limit = frameLimit > 0 ? std::min(frameLimit, rate * MAX_SECONDS)
                                        : rate * MAX_SECONDS;
        if (limit <= 0) return false;
        try { pcm_.assign(static_cast<size_t>(limit) * 2, 0); }
        catch (...) { return false; }
        limit_ = limit;
        rate_ = rate;
        frames_.store(0);
        rateChanged_.store(false);
        enabled_.store(true);
        return true;
    }

    void stop() {
        // SC ordering also closes the race where a callback begins between this
        // store and the busy read: it must then observe disabled before PCM access.
        enabled_.store(false);
        while (writing_.load()) std::this_thread::yield();
    }

    void append(const float* audio, int count, int channels, int rate) {
        writing_.store(true);
        if (!enabled_.load()) { writing_.store(false); return; }
        if (!audio || count <= 0 || channels <= 0) { writing_.store(false); return; }
        if (rate != rate_) {
            rateChanged_.store(true);
            enabled_.store(false);
            writing_.store(false);
            return;
        }
        const int from = frames_.load(std::memory_order_relaxed);
        const int take = std::min(count, limit_ - from);
        for (int i = 0; i < take; ++i) {
            pcm_[static_cast<size_t>(from + i) * 2] = quantize(audio[i * channels]);
            pcm_[static_cast<size_t>(from + i) * 2 + 1] =
                quantize(audio[i * channels + (channels > 1 ? 1 : 0)]);
        }
        frames_.store(from + take, std::memory_order_release);
        if (from + take == limit_) enabled_.store(false);
        writing_.store(false);
    }

    bool recording() const { return enabled_.load(); }
    int frames() const { return frames_.load(std::memory_order_acquire); }
    int rate() const { return rate_; } // control thread; stable throughout a take
    int limit() const { return limit_; }
    bool rate_changed() const { return rateChanged_.load(); }
    // Call stop() before reading the PCM, including after an automatic stop.
    const std::vector<int16_t>& pcm() const { return pcm_; }
    std::vector<int16_t> take_pcm() { stop(); return std::move(pcm_); }

private:
    static int16_t quantize(float value) {
        if (!std::isfinite(value)) return 0;
        value = std::clamp(value, -1.0f, 1.0f);
        const int q = static_cast<int>(std::lround(value * 32768.0f));
        return static_cast<int16_t>(std::clamp(q, -32768, 32767));
    }
    std::vector<int16_t> pcm_;
    std::atomic<bool> enabled_{false}, writing_{false}, rateChanged_{false};
    std::atomic<int> frames_{0};
    int rate_ = 48000, limit_ = 0;
};
