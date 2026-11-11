// SPDX-License-Identifier: GPL-3.0-or-later
// Real PCM tests for per-track Granular automation, at the actual render seam.
#include "audio-engine.h"
#include "songcore/engine_consumer.h"
#include "songcore/project_io.h"
#include "songcore/scheduler.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <new>
#include <vector>

namespace {
thread_local bool insideCallback = false;
std::atomic<unsigned> callbackAllocations{0};
}
void* operator new(std::size_t n) {
    if (insideCallback) ++callbackAllocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
namespace g = studio::granular::step;
using songcore::f32_bits;
constexpr int kRate = 48000;

void u16(std::ostream& out, uint16_t n) { const char b[]{char(n), char(n >> 8)}; out.write(b, 2); }
void u32(std::ostream& out, uint32_t n) { u16(out, uint16_t(n)); u16(out, uint16_t(n >> 16)); }
void make_wav(const std::string& path) {
    constexpr int n = kRate * 2;
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4); u32(out, 36 + n * 2); out.write("WAVEfmt ", 8); u32(out, 16);
    u16(out, 1); u16(out, 1); u32(out, kRate); u32(out, kRate * 2); u16(out, 2); u16(out, 16);
    out.write("data", 4); u32(out, n * 2);
    for (int i = 0; i < n; ++i) {
        const float amp = i < n / 2 ? 8000.f : 14000.f;
        const float x = amp * std::sin(i * (i < n / 2 ? .0628f : .0942f));
        u16(out, uint16_t(int16_t(std::lround(x))));
    }
    assert(out.good());
}

g::Delta command(g::Field field, int value) {
    g::Delta delta{};
    assert(g::append(delta, g::code(field), value));
    return delta;
}

struct Fixture {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    studio::granular::Parameters parameters{};
    std::string source;

    explicit Fixture(std::string path) : source(std::move(path)) {
        engine->setDeviceSampleRate(kRate);
        assert(engine->loadSampleFromWavFile(0, source.c_str()) == kRate);
        parameters.position = .37f;
        parameters.attackMs = 0;
        parameters.releaseMs = 12;
        parameters.densityHz = 100;
        parameters.sizeMs = 36;
        parameters.level = .75f;
        engine->setSampleGranular(0, true, parameters, kRate, 73);
        songcore::Program program;
        program.hasSample = true;
        program.sampleId = 0;
        for (int i = 0; i < 2; ++i) engine->setProgram(i, program, nullptr, 0);
    }

    void note(int track, g::Delta delta = {}, int64_t when = 0, int instrument = 0) {
        songcore::NoteOnPayload n{};
        n.note = 60; n.velocity = 127; n.velGainBits = f32_bits(1.f);
        n.volGainBits = f32_bits(.8f); n.panBits = f32_bits(.5f);
        n.start = n.slice = n.tableId = n.tableRow = -1;
        n.granular = delta;
        engine->scheduleProgramNote(engine->getCurrentFrame() + when, track, instrument, n, 120);
    }
    void step(int track, g::Field field, int value, int64_t when = 0) {
        engine->scheduleGranularStep(engine->getCurrentFrame() + when, track, command(field, value));
    }
    std::vector<float> render(int frames, bool offline = false) {
        std::vector<float> result(size_t(frames) * 2);
        insideCallback = true;
        if (offline) engine->renderOffline(frames, result.data(), kRate);
        else engine->processLiveBlock(result.data(), frames, 2, kRate);
        insideCallback = false;
        for (auto value : result) assert(std::isfinite(value));
        return result;
    }
    studio::granular::Snapshot listen(int track) {
        engine->sampleGranularSnapshot(0, track);
        render(2048);
        const auto view = engine->sampleGranularSnapshot(0, track);
        assert(view.active && view.trackId == track && view.activeGrains > 0);
        return view;
    }
};

double energy(const std::vector<float>& pcm, size_t skipFrames = 0) {
    double result = 0;
    for (size_t i = skipFrames * 2; i < pcm.size(); ++i) result += double(pcm[i]) * pcm[i];
    return result;
}

void exact_frames_and_preview(const std::string& wav) {
    Fixture changed(wav), control(wav);
    changed.note(0); control.note(0);
    assert(changed.render(2048) == control.render(2048));
    changed.step(0, g::Level, 0, 113);
    const auto altered = changed.render(256), original = control.render(256);
    assert(std::memcmp(altered.data(), original.data(), 113 * 2 * sizeof(float)) == 0);
    assert(!std::equal(altered.begin() + 113 * 2, altered.end(), original.begin() + 113 * 2));
    // 10 ms of gain smoothing is intentional. Measure after it has settled.
    assert(energy(changed.render(4096), 2048) < energy(control.render(4096), 2048) * .01);
    changed.note(0); // next note of the same source inherits the muted level
    assert(energy(changed.render(4096)) < .001);
    changed.step(0, g::Clear, 0);
    assert(energy(changed.render(4096)) > .01);

    Fixture shared(wav);
    auto left = command(g::Position, 0x20), right = command(g::Position, 0xE0);
    assert(g::append(left, g::code(g::Freeze), 1));
    assert(g::append(right, g::code(g::Freeze), 1));
    shared.note(0, left);
    shared.note(1, right);
    shared.note(AudioEngine::PREVIEW_LANE, left);  // even a forged delta cannot bind the preview lane
    const auto a = shared.listen(0), b = shared.listen(1), preview = shared.listen(AudioEngine::PREVIEW_LANE);
    assert(a.frozen && b.frozen && !preview.frozen);
    assert(a.scanSourceFrame < a.sourceFrames * .2 && b.scanSourceFrame > b.sourceFrames * .8);
    assert(preview.scanSourceFrame > preview.sourceFrames * .3 && preview.scanSourceFrame < preview.sourceFrames * .45);
    shared.step(0, g::Position, 0x40);
    const auto newA = shared.listen(0), stillB = shared.listen(1);
    assert(newA.scanSourceFrame > a.scanSourceFrame && newA.renderedFrames > a.renderedFrames);
    assert(stillB.scanSourceFrame == b.scanSourceFrame && stillB.frozen);
    assert(shared.engine->granularStepOverruns() == 0);
    std::cout << "PASS exact in-block PCM boundary, same-source notes, GCL, two isolated tracks and preview\n";
}

void resets_and_render(const std::string& wav) {
    Fixture f(wav);
    f.step(0, g::Level, 0, 3); // no current source: cannot pre-arm a different note
    f.note(0, {}, 10);
    assert(energy(f.render(4096)) > .01);
    f.step(0, g::Level, 0);
    assert(energy(f.render(4096), 2048) < .001);
    f.note(0, {}, 0, 1); // second instrument, same PCM slot
    assert(energy(f.render(4096)) > .01);
    f.step(0, g::Level, 0);
    f.render(4096);
    f.engine->clearScheduledNotesFrom(f.engine->getCurrentFrame() + 96, 0);
    f.note(0, {}, 96, 1); // same frame: reset must precede new trigger
    assert(energy(f.render(4096)) > .01);
    f.step(0, g::Level, 0);
    f.render(4096);
    assert(f.engine->loadSampleFromWavFile(0, wav.c_str()) == kRate);
    f.note(0, {}, 0, 1);
    assert(energy(f.render(4096)) > .01);
    f.step(0, g::Level, 0);
    f.render(4096);
    f.engine->setSampleGranular(0, false, f.parameters, kRate, 73);
    f.engine->setSampleGranular(0, true, f.parameters, kRate, 73);
    f.note(0, {}, 0, 1);
    assert(energy(f.render(4096)) > .01);
    f.step(0, g::Level, 0);
    f.render(4096);
    f.engine->clearScheduledNotes();
    f.engine->stopAllRamped();
    f.engine->startTake();
    f.note(0, {}, 0, 1);
    assert(energy(f.render(4096)) > .01);

    Fixture live(wav), offline(wav);
    for (auto* e : {&live, &offline}) {
        e->note(0, command(g::Position, 0x20));
        e->step(0, g::Level, 0, 1120);
        e->step(0, g::Clear, 0, 1900);
    }
    assert(live.render(4096) == offline.render(4096, true));
    Fixture late(wav);
    late.render(1024);
    late.note(0, {}, -90, 0);
    late.step(0, g::Level, 0, -60);
    late.note(0, {}, -30, 1);
    assert(energy(late.render(4096), 2048) > .01); // overdue Gxx must not mute instrument 1
    std::cout << "PASS no pre-arm, shared-PCM instrument change, LIVE/STOP/sample/mode reset and live/offline parity\n";
}

void real_tracker_and_project(const std::string& wav) {
    Fixture f(wav);
    auto p = songcore::make_default_project();
    songcore::normalize_and_migrate(p);
    p.tempo = 120;
    auto& i = p.instruments[0];
    i.sampleFilePath = wav;
    i.granularEnabled = true;
    i.granularParams = f.parameters;
    i.granularSeed = 73;
    p.tracks[0].chainRefs = {0};
    p.chains[0].phraseRefs[0] = 0;
    auto& rows = p.phrases[0].steps;
    rows[0].note = songcore::Note::C4();
    songcore::step_set_fx(rows[0], 1, g::code(g::Position), 0xC0);
    songcore::step_set_fx(rows[0], 2, g::code(g::Freeze), 1);
    songcore::step_set_fx(rows[1], 1, g::code(g::Position), 0x20);
    songcore::step_set_fx(rows[2], 1, g::code(g::Clear), 0);
    const auto saved = songcore::serialize_project(p);
    auto loaded = songcore::parse_project(songcore::json::parse(saved));
    songcore::normalize_and_migrate(loaded);
    assert(songcore::serialize_project(loaded) == saved);
    songcore::Routing routing;
    routing.reset();
    songcore::EngineConsumer consume(f.engine.get(), &p, &routing);
    songcore::MidiRouter router(&consume);
    songcore::Sequencer seq(router, p, kRate);
    seq.seed_rng(1234);
    seq.scheduleSongRowRange(0, 0, nullptr);
    const auto first = f.listen(0);
    assert(first.frozen && first.scanSourceFrame > first.sourceFrames * .7);
    f.render(4300);
    const auto second = f.listen(0);
    assert(second.frozen && second.scanSourceFrame < second.sourceFrames * .2);
    f.render(3900);
    const auto third = f.listen(0);
    assert(!third.frozen && third.scanSourceFrame > third.sourceFrames * .3);
    assert(third.renderedFrames > second.renderedFrames && second.renderedFrames > first.renderedFrames);
    assert(songcore::serialize_project(p) == saved);
    std::cout << "PASS real tracker→EngineConsumer→PCM path; Gxx .ptp roundtrip and unchanged authored preset\n";
}
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto folder = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(folder);
    const auto wav = (folder / "step-material.wav").string();
    make_wav(wav);
    exact_frames_and_preview(wav);
    resets_and_render(wav);
    real_tracker_and_project(wav);
    assert(callbackAllocations == 0);
    std::cout << "ALL GRANULAR STEP ENGINE CHECKS PASS; no callback C++ allocations\n";
}
