#include "audio-engine.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <cstdio>

namespace {
void u16(std::ofstream& out, uint16_t value) {
    const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8)};
    out.write(bytes, 2);
}
void u32(std::ofstream& out, uint32_t value) {
    const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8),
                          static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
    out.write(bytes, 4);
}
void write_stereo(const std::filesystem::path& path, const std::vector<int16_t>& left,
                  const std::vector<int16_t>& right) {
    assert(left.size() == right.size());
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4); u32(out, 36 + static_cast<uint32_t>(left.size() * 4));
    out.write("WAVEfmt ", 8); u32(out, 16); u16(out, 1); u16(out, 2);
    u32(out, 48000); u32(out, 48000 * 4); u16(out, 4); u16(out, 16);
    out.write("data", 4); u32(out, static_cast<uint32_t>(left.size() * 4));
    for (size_t i = 0; i < left.size(); ++i) { u16(out, left[i]); u16(out, right[i]); }
}
void write_float_stereo(const std::filesystem::path& path, const std::vector<float>& left,
                        const std::vector<float>& right) {
    assert(left.size() == right.size());
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4); u32(out, 36 + static_cast<uint32_t>(left.size() * 8));
    out.write("WAVEfmt ", 8); u32(out, 16); u16(out, 3); u16(out, 2);
    u32(out, 48000); u32(out, 48000 * 8); u16(out, 8); u16(out, 32);
    out.write("data", 4); u32(out, static_cast<uint32_t>(left.size() * 8));
    for (size_t i = 0; i < left.size(); ++i) {
        uint32_t a, b;
        std::memcpy(&a, &left[i], sizeof(a)); std::memcpy(&b, &right[i], sizeof(b));
        u32(out, a); u32(out, b);
    }
}
bool close(float a, float b) { return std::fabs(a - b) < 1e-6f; }
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path folder(argv[1]);
    std::filesystem::create_directories(folder);
    const auto path = folder / "stereo-cleanup-test.wav";
    const std::vector<int16_t> left = {8192, 4096, 8192, 12288, 8192, 4096, 8192, 12288};
    const std::vector<int16_t> right = {-4096, -8192, -4096, 0, -4096, -8192, -4096, 0};
    write_stereo(path, left, right);
    auto engine = std::make_unique<AudioEngine>();
    assert(engine->loadSampleFromWavFile(0, path.c_str()) == 48000);
    assert(engine->hasStereoData(0));
    std::vector<float> l(left.size()), r(right.size());
    assert(engine->backupSample(0));
    engine->removeDcOffsetSample(0, 1, 5);
    engine->getSampleData(0, l.data()); engine->getSampleDataRight(0, r.data());
    float sumL = 0, sumR = 0;
    for (int i = 1; i < 5; ++i) { sumL += l[i]; sumR += r[i]; }
    assert(close(sumL, 0) && close(sumR, 0));
    assert(l[0] == left[0] / 32768.f && l[5] == left[5] / 32768.f);
    assert(r[0] == right[0] / 32768.f && r[5] == right[5] / 32768.f);
    engine->undoSample(0);
    engine->getSampleData(0, l.data()); engine->getSampleDataRight(0, r.data());
    for (size_t i = 0; i < l.size(); ++i) {
        assert(l[i] == left[i] / 32768.f && r[i] == right[i] / 32768.f);
    }
    engine->invertPolaritySample(0, 2, 6);
    engine->getSampleData(0, l.data()); engine->getSampleDataRight(0, r.data());
    for (int i = 0; i < 8; ++i) {
        const float sign = (i >= 2 && i < 6) ? -1.f : 1.f;
        assert(l[i] == sign * left[i] / 32768.f && r[i] == sign * right[i] / 32768.f);
    }
    engine->invertPolaritySample(0, 2, 6);
    engine->declickSample(0, 1, 7, 2);
    engine->getSampleData(0, l.data()); engine->getSampleDataRight(0, r.data());
    assert(l[1] == 0 && r[1] == 0 && l[6] == 0 && r[6] == 0);
    assert(close(l[2], left[2] / 65536.f) && close(r[2], right[2] / 65536.f));
    assert(l[3] == left[3] / 32768.f && r[3] == right[3] / 32768.f);
    assert(l[0] == left[0] / 32768.f && r[7] == right[7] / 32768.f);
    const auto unchanged = l;
    engine->removeDcOffsetSample(0, 8, -1);
    engine->invertPolaritySample(0, 7, 3);
    engine->declickSample(0, 7, 3, 4);
    engine->declickSample(0, -100, 100, 0);
    engine->getSampleData(0, l.data()); assert(l == unchanged);
    engine->declickSample(0, 3, 4, 100);
    engine->getSampleData(0, l.data()); engine->getSampleDataRight(0, r.data());
    assert(l[3] == 0 && r[3] == 0 && engine->getSampleLength(0) == 8);

    // Zooming beyond one frame per pixel must repeat the frame, never insert blank bins.
    const float mono[] = {.125f, .25f, .375f, .5f};
    assert(engine->loadSample(1, mono, 4));
    float bins[16]{};
    engine->getSampleWaveformRangeSource(1, 0, 4, bins, 8, 0);
    for (int i = 0; i < 8; ++i) assert(bins[2 * i + 1] == mono[i / 2]);
    engine->removeDcOffsetSample(1, -10, 50);
    float cleaned[4]{}; engine->getSampleData(1, cleaned);
    assert(close(cleaned[0] + cleaned[1] + cleaned[2] + cleaned[3], 0));
    engine->invertPolaritySample(-1, 0, 5);
    engine->removeDcOffsetSample(256, 0, 5);
    engine->declickSample(99, 0, 5, 2);

    // Undo preserves the actual float working buffer, not a clipped/quantised16-bit copy.
    const std::vector<float> precise = {1.25f, -.9876543f, .000012345f, -0.0f, -1.5f, .33333334f};
    const std::vector<float> preciseR = {-.23456789f, 1.125f, -.0000034567f, .0f, .7654321f, -1.25f};
    assert(engine->loadSample(2, precise.data(), static_cast<int>(precise.size())));
    const auto memoryBefore = engine->audio_memory_bytes();
    assert(engine->backupSample(2));
    assert(engine->audio_memory_bytes() - memoryBefore == static_cast<int64_t>(precise.size() * sizeof(float)));
    engine->silenceRegion(2, 0, static_cast<int>(precise.size()));
    assert(!engine->backupSample(-1) && !engine->backupSample(256) && !engine->backupSample(99));
    engine->undoSample(2);
    std::vector<float> restored(precise.size()); engine->getSampleData(2, restored.data());
    assert(std::memcmp(restored.data(), precise.data(), precise.size() * sizeof(float)) == 0);
    const auto floatPath = folder / "float32-undo-test.wav";
    write_float_stereo(floatPath, precise, preciseR);
    assert(engine->loadSampleFromWavFile(3, floatPath.c_str()) == 48000);
    assert(engine->backupSample(3));
    engine->removeDcOffsetSample(3, 0, static_cast<int>(precise.size()));
    engine->undoSample(3);
    std::vector<float> restoredR(precise.size());
    engine->getSampleData(3, restored.data()); engine->getSampleDataRight(3, restoredR.data());
    assert(std::memcmp(restored.data(), precise.data(), precise.size() * sizeof(float)) == 0);
    assert(std::memcmp(restoredR.data(), preciseR.data(), preciseR.size() * sizeof(float)) == 0);
    std::puts("PASS: stereo cleanup, selection bounds, exact float32 Undo, mono and sample-level waveform");
}
