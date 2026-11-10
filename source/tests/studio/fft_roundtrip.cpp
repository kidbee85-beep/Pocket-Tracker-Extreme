// Header-only regression for fixed/runtime ShyFFT. The runtime argument is log2(size), not size.
// --trace emits exact spectra and normalized inverse samples for a same-host baseline comparison.
#include "stmlib/fft/shy_fft.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace {
constexpr double pi = 3.1415926535897932384626433832795;
struct Metrics {
    size_t roundtrips = 0;
    double max_float_error = 0, max_double_error = 0, max_bin_error = 0, max_cross_path_error = 0;
} metrics;

template<typename T> struct Guarded {
    static constexpr size_t guard_size = 16;
    static constexpr T sentinel = static_cast<T>(12345.25);
    explicit Guarded(size_t n) : size(n), data(n + guard_size * 2, sentinel) {}
    T* samples() { return data.data() + guard_size; }
    void check() const {
        for(size_t i = 0; i < guard_size; ++i) {
            assert(data[i] == sentinel);
            assert(data[size + guard_size + i] == sentinel);
        }
    }
    size_t size;
    std::vector<T> data;
};

template<typename T, size_t N> std::vector<T> signal(int kind) {
    std::vector<T> result(N);
    uint32_t state = 0x72a3b19du ^ static_cast<uint32_t>(N);
    for(size_t i = 0; i < N; ++i) {
        state = state * 1664525u + 1013904223u;
        const double noise = (static_cast<double>(state >> 8) / 16777216.0 - .5) * 1.2;
        const double angle = 2 * pi * static_cast<double>(i) / N;
        double value = 0;
        if(kind == 0) value = (i == 0 ? .75 : 0) - (i == N / 3 ? .25 : 0);
        if(kind == 1) value = .55 * std::sin(3 * angle) + .18 * std::cos(5 * angle)
                             + .10 * std::sin((N / 2 - 1) * angle);
        if(kind == 2) value = noise;
        if(kind == 3) value = .10 + (i % 2 ? -.12 : .12) + .31 * std::sin(7 * angle)
                             + .17 * noise + (i == N / 5 ? .25 : 0);
        result[i] = static_cast<T>(value);
    }
    return result;
}

// ShyFFT packs real bins0..N/2, followed by positive-sine imaginary bins1..N/2-1.
// An independent direct DFT catches paired forward/inverse errors that a roundtrip could hide.
template<typename T, size_t N> void check_bins(const std::vector<T>& source, const T* spectrum) {
    const std::set<size_t> bins = {0, 1, 3, 5, 7, N / 4, N / 2 - 1, N / 2};
    const double tolerance = std::is_same<T,float>::value ? 8e-6 : 1e-7;
    for(size_t k : bins) {
        double real = 0, imaginary = 0;
        for(size_t i = 0; i < N; ++i) {
            const double angle = 2 * pi * static_cast<double>(k) * i / N;
            real += static_cast<double>(source[i]) * std::cos(angle);
            imaginary += static_cast<double>(source[i]) * std::sin(angle);
        }
        const double re_error = std::abs(static_cast<double>(spectrum[k]) - real) / N;
        metrics.max_bin_error = std::max(metrics.max_bin_error, re_error);
        assert(re_error < tolerance);
        if(k > 0 && k < N / 2) {
            const double im_error = std::abs(static_cast<double>(spectrum[N / 2 + k]) - imaginary) / N;
            metrics.max_bin_error = std::max(metrics.max_bin_error, im_error);
            assert(im_error < tolerance);
        }
    }
}

template<typename T> struct Result { std::vector<T> spectrum, reconstructed; };

template<typename T, size_t N, typename FFT>
Result<T> transform(FFT& fft, const std::vector<T>& source, bool runtime, bool trace,
                    int signal_kind, int phasor_kind) {
    Guarded<T> input(N), output(N);
    std::copy(source.begin(), source.end(), input.samples());
    if(runtime) fft.Direct(input.samples(), output.samples(), stmlib::Log2<N>::value);
    else fft.Direct(input.samples(), output.samples());
    input.check(); output.check();
    Result<T> result; result.spectrum.assign(output.samples(), output.samples() + N);
    for(T value : result.spectrum) assert(std::isfinite(value));
    check_bins<T,N>(source, output.samples());
    if(runtime) fft.Inverse(output.samples(), input.samples(), stmlib::Log2<N>::value);
    else fft.Inverse(output.samples(), input.samples());
    input.check(); output.check();
    result.reconstructed.resize(N);
    double max_error = 0, squared_error = 0;
    for(size_t i = 0; i < N; ++i) {
        const T normalized = input.samples()[i] / static_cast<T>(N);
        assert(std::isfinite(normalized));
        result.reconstructed[i] = normalized;
        const double error = std::abs(static_cast<double>(normalized) - source[i]);
        max_error = std::max(max_error, error); squared_error += error * error;
    }
    const double rms_error = std::sqrt(squared_error / N);
    if(std::is_same<T,float>::value) {
        metrics.max_float_error = std::max(metrics.max_float_error, max_error);
        assert(max_error < 4e-5 && rms_error < 1.5e-5);
    } else {
        metrics.max_double_error = std::max(metrics.max_double_error, max_error);
        assert(max_error < 1e-7 && rms_error < 5e-8);
    }
    ++metrics.roundtrips;
    if(trace) {
        const uint32_t tag[] = {static_cast<uint32_t>(sizeof(T)), static_cast<uint32_t>(N),
                               static_cast<uint32_t>(signal_kind), static_cast<uint32_t>(phasor_kind),
                               static_cast<uint32_t>(runtime)};
        std::cout.write(reinterpret_cast<const char*>(tag), sizeof(tag));
        std::cout.write(reinterpret_cast<const char*>(result.spectrum.data()), N * sizeof(T));
        std::cout.write(reinterpret_cast<const char*>(result.reconstructed.data()), N * sizeof(T));
    }
    return result;
}

template<typename T, size_t N, template<typename,size_t> class Phasor>
void size_suite(stmlib::ShyFFT<T,4096,Phasor>& runtime_fft, bool trace, int phasor_kind) {
    stmlib::ShyFFT<T,N,Phasor> fixed_fft; fixed_fft.Init();
    for(int kind = 0; kind < 4; ++kind) {
        const auto source = signal<T,N>(kind);
        const auto fixed = transform<T,N>(fixed_fft, source, false, trace, kind, phasor_kind);
        const auto runtime = transform<T,N>(runtime_fft, source, true, trace, kind, phasor_kind);
        // Constant-size and runtime loops can round near-zero cancellation differently.
        // Require tight numerical agreement here; same-path baseline preservation uses --trace.
        const double cross_tolerance = 32 * std::numeric_limits<T>::epsilon();
        for(size_t i = 0; i < N; ++i) {
            const double spectral = std::abs(double(fixed.spectrum[i]) - runtime.spectrum[i]) / N;
            const double reconstructed = std::abs(double(fixed.reconstructed[i]) - runtime.reconstructed[i]);
            metrics.max_cross_path_error = std::max(metrics.max_cross_path_error, std::max(spectral,reconstructed));
            assert(spectral < cross_tolerance && reconstructed < cross_tolerance);
        }
    }
}

template<typename T, template<typename,size_t> class Phasor>
void suite(bool trace, int phasor_kind) {
    stmlib::ShyFFT<T,4096,Phasor> runtime_fft; runtime_fft.Init();
    size_suite<T,16,Phasor>(runtime_fft,trace,phasor_kind);
    size_suite<T,4096,Phasor>(runtime_fft,trace,phasor_kind);
    size_suite<T,32,Phasor>(runtime_fft,trace,phasor_kind);
    size_suite<T,1024,Phasor>(runtime_fft,trace,phasor_kind);
    size_suite<T,256,Phasor>(runtime_fft,trace,phasor_kind);
}
}
int main(int argc,char**argv) {
    const bool trace = argc == 2 && std::string(argv[1]) == "--trace";
    assert(argc == 1 || trace);
    suite<float,stmlib::LutPhasor>(trace,0); suite<float,stmlib::RotationPhasor>(trace,1);
    suite<double,stmlib::LutPhasor>(trace,0); suite<double,stmlib::RotationPhasor>(trace,1);
    assert(metrics.roundtrips == 160);
    if(!trace) std::cout << std::setprecision(10) << "PASS: " << metrics.roundtrips
        << " fixed/runtime ShyFFT roundtrips, float/double, LUT/rotation,16..4096; guards and independent DFT bins. Max normalized errors float="
        << metrics.max_float_error << " double=" << metrics.max_double_error << " bin=" << metrics.max_bin_error << " cross_path=" << metrics.max_cross_path_error << '\n';
}
