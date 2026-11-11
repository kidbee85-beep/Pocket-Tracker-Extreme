// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real phrase scheduler and its audio-side control contract.
#include "songcore/scheduler.h"
#include "songcore/trace_writer.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace songcore;

namespace {
struct Capture : IMidiConsumer {
    std::vector<Event> events;
    void consume(const Event& e) override { events.push_back(e); }
    void on_play(const std::string&, const std::string&, int64_t, int, int) override {}
    void on_stop() override {}
};

Project score() {
    auto p = make_default_project();
    p.tempo = 120;
    p.tracks[0].chainRefs = {0};
    p.chains[0].phraseRefs[0] = 0;
    return p;
}

std::string legacy_trace() {
    auto p = score();
    auto& rows = p.phrases[0].steps;
    rows[0].note = Note::C4();
    rows[0].instrument = 0;
    rows[0].fx1Type = FX_CUT;
    rows[0].fx1Value = 0x65;
    rows[1].fx1Type = FX_RSEND;
    rows[1].fx1Value = 0x93;
    rows[2].note = Note::C4();
    rows[2].instrument = 0;
    rows[2].fx1Type = FX_LAT;
    rows[2].fx1Value = 2;
    rows[2].fx2Type = FX_PAN;
    rows[2].fx2Value = 0xD0;
    TraceWriter trace;
    std::string result;
    trace.begin(&result, "legacy-fixture");
    MidiRouter router(&trace);
    Sequencer seq(router, p, 48000);
    seq.seed_rng(0x2746);
    seq.scheduleSongRowRange(0, 0, nullptr);
    return result;
}

#ifndef GRANULAR_STEP_LEGACY_BASELINE
namespace g = studio::granular::step;

void shared_slot_overlays() {
    studio::granular::Parameters base;
    base.position = .1f;
    base.level = .81f;
    g::TrackOverlay a, b;
    g::SourceKey shared{20, 7, 21};
    a.bind(shared);
    b.bind(shared);
    g::Delta left{}, right{};
    assert(g::append(left, g::code(g::Position), 255));
    assert(g::append(right, g::code(g::Position), 0));
    assert(g::append(right, g::code(g::Freeze), 1));
    a.overlay.apply(left);
    b.overlay.apply(right);
    const auto aa = g::effective(base, a.overlay), bb = g::effective(base, b.overlay);
    assert(aa.position == 1.f && !aa.freeze && aa.level == .81f);
    assert(bb.position == 0.f && bb.freeze && bb.level == .81f);
    assert(base.position == .1f && !base.freeze);
    a.bind(shared);
    assert(a.overlay.has(g::Position));
    a.bind({21, 7, 21}); // distinct instrument with the exact same PCM slot
    assert(!a.overlay.mask && b.overlay.has(g::Position));
    b.bind({20, 7, 22}); // replaced PCM generation
    assert(!b.overlay.mask);
    b.overlay.apply(left);
    b.reset();
    assert(!b.source.valid() && !b.overlay.mask);
}

void command_boundaries_and_row_order() {
    using studio::granular::Parameters;
    const Parameters base;
    g::Overlay low, high;
    for (int fx = g::kFirstFx; fx < g::kLastFx; ++fx) {
        g::Delta a{}, b{};
        assert(std::strlen(effect_name(fx).c_str()) == 3);
        assert(g::append(a, fx, -9));
        assert(g::append(b, fx, 1000));
        low.apply(a);
        high.apply(b);
    }
    const auto l = g::effective(base, low), h = g::effective(base, high);
    assert(l.position == 0 && h.position == 1 && l.sizeMs == 2 && h.sizeMs == 1000);
    assert(l.densityHz == 0 && h.densityHz == 120 && l.sprayMs == 0 && h.sprayMs == 1000);
    assert(l.pitchSemitones == -48 && h.pitchSemitones == 48 && l.pitchSpraySemitones == 0 && h.pitchSpraySemitones == 24);
    assert(l.scanSpeed == -4 && h.scanSpeed == 4 && !l.freeze && h.freeze);
    assert(l.panSpread == 0 && h.panSpread == 1 && l.reverseProbability == 0 && h.reverseProbability == 1);
    assert(l.windowShape == 0 && h.windowShape == 1 && l.level == 0 && h.level == 2);
    assert(l.attackMs == 0 && h.attackMs == 5000 && l.releaseMs == 1 && h.releaseMs == 10000);
    PhraseStep row;
    step_set_fx(row, 1, g::code(g::Position), 0x70);
    step_set_fx(row, 2, g::code(g::Clear), 0);
    step_set_fx(row, 3, g::code(g::Pitch), 0x80);
    high.apply(resolve_step_params(row, 0, 1).granular);
    const auto cleared = g::effective(base, high);
    assert(high.mask == (g::Mask(1) << g::Pitch));
    assert(cleared.position == base.position && cleared.pitchSemitones == 0);
    step_set_fx(row, 2, g::code(g::Position), 0xA0);
    step_set_fx(row, 3, g::code(g::Position), 0xE0);
    high.apply(resolve_step_params(row, 0, 1).granular);
    assert(high.bytes[g::Position] == 0xE0);
}

void real_scheduler() {
    auto p = score();
    auto& rows = p.phrases[0].steps;
    rows[0].note = Note::C4();
    step_set_fx(rows[0], 1, g::code(g::Position), 0x21);
    step_set_fx(rows[0], 2, FX_LAT, 2);
    step_set_fx(rows[0], 3, FX_REPEAT, 0x30);
    step_set_fx(rows[1], 1, g::code(g::Scan), 0xB0);
    step_set_fx(rows[1], 2, g::code(g::Freeze), 1);
    step_set_fx(rows[2], 1, g::code(g::Clear), 0);
    step_set_fx(rows[2], 2, g::code(g::Size), 0x3A);
    rows[3].note = Note::C4();
    step_set_fx(rows[3], 1, g::code(g::Density), 0xA0);
    step_set_fx(rows[3], 2, FX_NTH, 0x20); // only on the second phrase pass
    Capture capture;
    TraceWriter trace;
    std::string emitted;
    trace.begin(&emitted, "granular-fixture");
    MidiRouter router(&capture);
    router.add_consumer(&trace);
    Sequencer seq(router, p, 48000);
    seq.seed_rng(0x2746);
    seq.scheduleSongRowRange(0, 0, nullptr);
    const auto step = frames_per_step(p.tempo, 48000);
    int withNote = 0, fxOnly = 0, repeated = 0;
    for (const auto& e : capture.events) {
        if (e.type == EV_NOTE_ON) {
            if (e.noteOn.granular.count) {
                assert(e.frame == 2 * (step / TICS_PER_STEP));
                assert(e.noteOn.granular.count == 1);
                assert(e.noteOn.granular.ops[0].field == g::Position);
                assert(e.noteOn.granular.ops[0].value == 0x21);
                ++withNote;
            } else {
                // RPT can continue onto subsequent rows. Its trigger must never
                // reapply the note's original delta or undo a later GCL.
                ++repeated;
            }
        } else if (e.type == EV_EXT_GRANULAR) {
            assert(e.track == 0 && e.instrument == INSTRUMENT_NONE);
            assert(e.extGranular.count == 2);
            if (e.frame == step) {
                assert(e.extGranular.ops[0].field == g::Scan && e.extGranular.ops[0].value == 0xB0);
                assert(e.extGranular.ops[1].field == g::Freeze && e.extGranular.ops[1].value == 1);
            } else {
                assert(e.frame == step * 2);
                assert(e.extGranular.ops[0].field == g::Clear);
                assert(e.extGranular.ops[1].field == g::Size && e.extGranular.ops[1].value == 0x3A);
            }
            ++fxOnly;
        }
    }
    assert(withNote == 1 && fxOnly == 2 && repeated > 0);
    assert(emitted.find("# schema=3 ") == 0);
    assert(emitted.find(" granular=60:21\n") != std::string::npos);
    assert(emitted.find(" 09 granular=6E:00,61:3A\n") != std::string::npos);
}
#endif
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--legacy-trace") == 0) {
        std::cout << legacy_trace();
        return 0;
    }
#ifndef GRANULAR_STEP_LEGACY_BASELINE
    shared_slot_overlays();
    command_boundaries_and_row_order();
    real_scheduler();
    std::cout << "Granular step registry, isolated overlays, row order, LAT/RPT/NTH scheduler and trace: PASS\n";
#endif
}
