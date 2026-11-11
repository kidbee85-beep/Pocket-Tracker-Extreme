/* Spectral-flux onset suggestions for the sample editor. All reads are on the editor thread. */
#include "audio-engine.h"
#include "common/load_progress.h"
#include "kissfft/kiss_fftr.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {
constexpr int kFftSize = 512;
constexpr int kBins = kFftSize / 2 + 1;
constexpr int kMaxHops = 8192;

struct ScanSignal {
    const float* left;
    const float* right;
    int mode;
    int length;

    float sample(int frame, int plane) const {
        if (frame < 0 || frame >= length) return 0.f;
        if (mode == 3) return (left[frame] + right[frame]) * .5f;
        return (mode == 1 || plane == 1) ? right[frame] : left[frame];
    }

    float activity(int frame) const {
        const auto activityOf = [&](int plane) {
            const float value = sample(frame, plane);
            return std::abs(value) + .35f * std::abs(value - sample(frame - 1, plane));
        };
        return mode == 2 ? std::max(activityOf(0), activityOf(1)) : activityOf(0);
    }
};

// A Hann-window peak can precede the actual attack. Find the sharpest rise in
// 1 ms bins nearby, then locate the leading edge in that bin at source resolution.
int refine_onset(const ScanSignal& signal, int center, int radius, int rate, int first, int end) {
    const int step = std::max(1, rate / 1000);
    const int lo = std::max(first, center - radius);
    const int hi = static_cast<int>(std::min<int64_t>(end, int64_t(center) + radius));
    int bestStart = lo, bestEnd = lo;
    float strongest = 0.f, bestBefore = 0.f;
    const auto average = [&](int a, int b) {
        float sum = 0.f;
        for (int at = a; at < b; ++at) sum += signal.activity(at);
        return sum / (b - a);
    };
    float previous = average(lo - step, lo);
    for (int a = lo; a < hi;) {
        const int b = a + std::min(step, hi - a);
        const float level = average(a, b);
        const float rise = level - previous;
        if (rise > strongest) {
            strongest = rise;
            bestBefore = previous;
            bestStart = a;
            bestEnd = b;
        }
        previous = level;
        a = b;
    }
    if (strongest < .00005f || bestEnd <= bestStart) return -1;
    const float threshold = bestBefore + .20f * strongest;
    for (int at = bestStart; at < bestEnd; ++at)
        if (signal.activity(at) > threshold) return at;
    return bestStart;
}
} // namespace

int AudioEngine::detectTransients(int id, int sensitivity, int sourceRate, const TransientScan& scan,
                                  int* outMarkers, int maxMarkers) {
    if (id < 0 || id >= 256 || !samples[id] || !outMarkers || maxMarkers <= 0 || sourceRate <= 0)
        return 0;
    setFlushToZeroForCurrentThread();
    const int length = sampleLengths[id];
    const int first = std::clamp(scan.first, 0, length);
    const int end = scan.end < 0 ? length : std::clamp(scan.end, first, length);
    const int rate = std::clamp(sourceRate, 4000, 384000);
    const int stride = std::max(1, rate / 22050);
    const int radius = kFftSize * stride / 2;
    // Fixed upper bound on FFTs and their memory, even for a whole long source.
    const int hop = std::max(rate / 200, static_cast<int>((int64_t(end - first) + kMaxHops - 2) / (kMaxHops - 1)));
    const int hops = 1 + (end - first) / hop;
    if (hops < 3) return 0;

    // Audition FX modifies PCM until the next action. Analyze the clean edited
    // sample in that case, just as Save and TO INST do.
    const bool preview = fxPreviewBackupId == id && fxPreviewBackup && fxPreviewBackupLen == length;
    const float* left = preview ? fxPreviewBackup : samples[id].load();
    const float* right = preview ? fxPreviewBackupRight : samplesRight[id].load();
    const int mode = right ? std::clamp(scan.sourceMode, 0, 3) : 0;
    const ScanSignal signal{left, right, mode, length};

    std::array<float, kFftSize> window{}, input{};
    std::array<kiss_fft_cpx, kBins> spectrum{};
    std::array<std::array<float, kBins>, 2> previous{};
    std::vector<float> flux(hops), magnitude(hops);
    for (int i = 0; i < kFftSize; ++i)
        window[i] = .5f - .5f * std::cos(6.28318530718f * i / kFftSize);
    kiss_fftr_cfg fft = kiss_fftr_alloc(kFftSize, 0, nullptr, nullptr);
    if (!fft) return 0;

    bool cancelled = false;
    for (int h = 0; h < hops; ++h) {
        if (scan.cancellable && h % 32 == 0 && !pt::load_tick(.90f * h / hops)) {
            cancelled = true;
            break;
        }
        const int center = first + h * hop;
        const int64_t offset = int64_t(center) - radius;
        for (int p = 0; p < (mode == 2 ? 2 : 1); ++p) {
            for (int i = 0; i < kFftSize; ++i) {
                const int64_t frame = offset + i * stride;
                input[i] = frame >= 0 && frame < length ? signal.sample(static_cast<int>(frame), p) * window[i] : 0.f;
            }
            kiss_fftr(fft, input.data(), spectrum.data());
            for (int b = 1; b < kBins; ++b) {
                const auto& bin = spectrum[b];
                const float mag = std::sqrt(bin.r * bin.r + bin.i * bin.i);
                flux[h] += std::max(0.f, mag - previous[p][b]);
                magnitude[h] += mag;
                previous[p][b] = mag;
            }
        }
    }
    kiss_fftr_free(fft);
    if (cancelled) return 0;

    float mean = 0.f, loudest = 0.f;
    for (int h = 1; h < hops; ++h) {
        mean += flux[h];
        loudest = std::max(loudest, magnitude[h]);
    }
    if (loudest < .001f) return 0;
    const float sens = std::clamp(sensitivity, 0, 255) / 255.f;
    const float threshold = std::max(mean / (hops - 1) * (3.f - 2.65f * sens), loudest * (.025f - .02f * sens));
    const float relative = .11f - .075f * sens;
    const int spacing = std::max(1, rate * 35 / 1000);
    int count = 0;
    for (int h = 1; h < hops && count < maxMarkers; ++h) {
        if (flux[h] < threshold || flux[h] < magnitude[h] * relative || flux[h] <= flux[h - 1] ||
            (h + 1 < hops && flux[h] < flux[h + 1])) continue;
        const int center = first + h * hop;
        int frame = refine_onset(signal, center, std::max(radius, hop), rate, first, end);
        if (frame <= first || frame >= end) continue;
        // Zero padding can spread a sustained tone across the FFT bins as the
        // file ends. In that last window require a real rise in the PCM too.
        if (center >= length - radius) {
            const int span = std::min({std::max(1, rate / 125), frame, length - frame});
            float before = 0.f, after = 0.f;
            for (int i = 0; i < span; ++i) {
                before += signal.activity(frame - span + i);
                after += signal.activity(frame + i);
            }
            if (after <= before * 1.25f + .00005f * span) continue;
        }
        if (scan.snap && !preview) {
            const int snapped = findZeroCrossing(id, frame, -1, std::max(1, rate * 2 / 1000), mode);
            if (snapped > first && snapped < end) frame = snapped;
        }
        if (count > 0 && frame - outMarkers[count - 1] < spacing) continue;
        outMarkers[count++] = frame;
    }
    if (scan.cancellable && !pt::load_tick(1.f)) return 0;
    return count;
}
