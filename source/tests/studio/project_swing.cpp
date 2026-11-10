// Standalone regression: link scheduler.cpp, ui/modules/project_editor.cpp and ui/canvas.cpp.
#include "songcore/scheduler.h"
#include "songcore/project_io.h"
#include "ui/modules/project_editor.h"
#include "ui/help_text.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

using namespace songcore;
namespace {
struct Capture : IMidiConsumer {
    std::vector<Event> events;
    void consume(const Event& e) override { events.push_back(e); }
    void on_play(const std::string&, const std::string&, int64_t, int, int) override {}
    void on_stop() override {}
    std::vector<int64_t> notes(int track = 0) const {
        std::vector<int64_t> result;
        for (const auto& e : events)
            if (e.type == EV_NOTE_ON && e.track == track) result.push_back(e.frame);
        return result;
    }
};

Project score(int tracks = 1, int phrases = 1) {
    auto p = make_default_project();
    normalize_and_migrate(p);
    p.tempo = 128;
    for (int t = 0; t < tracks; ++t) p.tracks[t].chainRefs = {0};
    for (int i = 0; i < phrases; ++i) {
        p.chains[0].phraseRefs[i] = i;
        for (auto& step : p.phrases[i].steps) step.note = Note::C4();
    }
    return p;
}

int64_t render(const Project& p, Capture& capture, int rate = 48000, int repeat = 1) {
    MidiRouter router(&capture);
    Sequencer seq(router, p, rate);
    seq.seed_rng(0x12345678);
    return seq.scheduleSongRowRange(0, 0, nullptr, repeat);
}

void save_load() {
    auto legacy = parse_project(json::parse("{\"tempo\":123,\"name\":\"OLD\"}"));
    normalize_and_migrate(legacy);
    assert(legacy.swing == 50);
    assert(!json::parse(serialize_project(legacy)).contains("swing"));
    for (int ratio = 50; ratio <= 75; ++ratio) {
        auto p = score(); p.swing = ratio;
        const auto saved = serialize_project(p);
        auto loaded = parse_project(json::parse(saved));
        normalize_and_migrate(loaded);
        assert(loaded.swing == ratio && serialize_project(loaded) == saved);
        Capture before, after;
        assert(render(p, before) == render(loaded, after));
        assert(before.notes() == after.notes());
    }
    for (const auto& item : std::vector<std::pair<const char*, int>>{
             {"-10", 50}, {"0", 50}, {"49", 50}, {"76", 75}, {"900", 75},
             {"null", 50}, {"\"bad\"", 50}}) {
        auto p = parse_project(json::parse(std::string("{\"swing\":") + item.first + "}"));
        assert(p.swing == item.second);
    }
}

void pair_and_song_duration() {
    for (int ratio = 50; ratio <= 75; ++ratio)
        for (int64_t frames : {int64_t(1), int64_t(53), int64_t(5167), int64_t(6000)}) {
            const auto longStep = swing_step_duration(frames, ratio, false);
            const auto shortStep = swing_step_duration(frames, ratio, true);
            assert(longStep >= frames && shortStep > 0 && longStep + shortStep == 2 * frames);
        }
    for (int rate : {44100, 48000, 96000}) for (int ratio : {50, 57, 67, 75}) {
        auto p = score(8, 2); p.swing = ratio;
        const auto f = frames_per_step(p.tempo, rate);
        const auto longStep = f + f * (ratio - 50) / 50;
        Capture c;
        assert(render(p, c, rate, 3) == 96 * f);
        for (int track = 0; track < 8; ++track) {
            const auto notes = c.notes(track);
            assert(notes.size() == 96);
            for (size_t i = 0; i < notes.size(); ++i)
                assert(notes[i] == static_cast<int64_t>(i / 2) * 2 * f + (i % 2 ? longStep : 0));
        }
    }
}

void grooves_take_priority() {
    for (const auto& ticks : std::vector<std::vector<int>>{
             {16, 8, -1}, {24, 24, -1}, {6, 6, -1}, {0, 24, -1}, {13, 10, 13, 12, -1}}) {
        auto p = score(); p.grooves[0].steps = ticks;
        Capture straight, swung;
        const auto length = render(p, straight);
        p.swing = 75;
        assert(render(p, swung) == length);
        assert(straight.events.size() == swung.events.size());
        for (size_t i = 0; i < straight.events.size(); ++i)
            assert(std::memcmp(&straight.events[i], &swung.events[i], sizeof(Event)) == 0);
    }
    // Straight grooves in all three rulers, and the empty groove, follow the global control.
    for (int ruler : {6, 12, 24}) for (bool empty : {false, true}) {
        auto p = score(); p.swing = 75;
        p.grooves[0].ticksPerStep = ruler;
        p.grooves[0].steps = empty ? std::vector<int>{-1} : std::vector<int>{ruler, ruler, -1};
        // Re-selecting GRV on every row must not reset the swing pair.
        for (auto& step : p.phrases[0].steps) { step.fx1Type = FX_GRV; step.fx1Value = 0; }
        Capture c; const auto f = frames_per_step(p.tempo, 48000);
        assert(render(p, c) == f * 16);
        const auto notes = c.notes();
        for (int i = 0; i < 16; ++i)
            assert(notes[i] == (i / 2) * 2 * f + (i % 2 ? f + f / 2 : 0));
    }
    auto p = score(); p.swing = 75; p.grooves[1].steps = {16, 8, -1};
    p.phrases[0].steps[0].fx1Type = FX_GRV; p.phrases[0].steps[0].fx1Value = 1;
    p.phrases[0].steps[2].fx1Type = FX_GRV; p.phrases[0].steps[2].fx1Value = 0;
    Capture c; const auto f = frames_per_step(p.tempo, 48000);
    assert(render(p, c) == 16 * f);
    const auto notes = c.notes();
    assert(notes[1] == f * 16 / 12 && notes[2] == 2 * f);
    assert(notes[3] == 3 * f + f / 2 && notes[4] == 4 * f);
}

void free_hops_keep_pair_phase() {
    auto p = score(1, 2); p.swing = 67;
    // One long step, a free HOP, then the destination must complete that pair with a short step.
    p.phrases[0].steps[1].fx1Type = FX_HOP; p.phrases[0].steps[1].fx1Value = 4;
    p.phrases[1].steps[4].fx1Type = FX_GRV; p.phrases[1].steps[4].fx1Value = 0;
    Capture c; const auto f = frames_per_step(p.tempo, 48000);
    const auto offset = f * 17 / 50;
    assert(render(p, c) == 13 * f + offset);
    const auto notes = c.notes(); assert(notes.size() == 13);
    for (size_t i = 0; i < notes.size(); ++i)
        assert(notes[i] == static_cast<int64_t>(i) * f + (i % 2 ? offset : 0));
}

void note_spans_and_effect_tics() {
    auto p = score(); p.tempo = 120; p.swing = 67;
    auto& steps = p.phrases[0].steps;
    for (auto& step : steps) step.note = Note::EMPTY();
    steps[0].note = Note::C4(); steps[0].fx1Type = FX_KILL; steps[0].fx1Value = 6;
    steps[1].note = Note::OFF();
    steps[2].note = Note::C4(); steps[2].fx1Type = FX_LAT; steps[2].fx1Value = 3;
    steps[2].fx2Type = FX_KILL; steps[2].fx2Value = 6;
    steps[3].note = Note::OFF();
    const auto f = frames_per_step(p.tempo, 48000);
    const auto longStep = f + f * 17 / 50;
    Capture c; assert(render(p, c) == 16 * f);
    assert((c.notes() == std::vector<int64_t>{0, 2 * f + 3 * (longStep / 12)}));
    std::vector<int64_t> off;
    for (const auto& e : c.events) if (e.type == EV_NOTE_OFF) off.push_back(e.frame);
    assert((off == std::vector<int64_t>{6 * (longStep / 12), longStep,
                                      2 * f + 9 * (longStep / 12), 2 * f + longStep}));
}

void live_edits_and_transport_reset() {
    for (const auto& change : std::vector<std::pair<int, int>>{{67, 50}, {50, 75}, {75, 57}}) {
        auto p = score(); p.swing = change.first;
        Capture c; MidiRouter router(&c); Sequencer seq(router, p, 48000);
        const auto f = frames_per_step(p.tempo, 48000);
        seq.set_follow(0, 1); seq.playPhrase(0);  // Only the first long step is scheduled.
        assert(c.notes().size() == 1);
        p.swing = change.second;
        seq.set_follow(0, -1); seq.finish_phrase_now();
        const auto notes = c.notes(); assert(notes.size() == 16);
        assert(notes[1] == f + f * (change.first - 50) / 50);
        assert(notes[2] == 2 * f);
        assert(notes[3] == 3 * f + f * (change.second - 50) / 50);
        assert(seq.buffer_head() == 16 * f);
        // A fresh transport never inherits a previous short-step or latched ratio.
        seq.stop(); c.events.clear(); seq.set_follow(0, 1); seq.set_start_frame(0); seq.playPhrase(0);
        seq.set_follow(0, -1); seq.finish_phrase_now();
        assert(c.notes()[1] == f + f * (change.second - 50) / 50);
    }
    // Starting at an odd sixteenth uses the short half, with the same nominal seek semantics.
    auto p = score(); p.swing = 75;
    Capture c; MidiRouter router(&c); Sequencer seq(router, p, 48000);
    const auto f = frames_per_step(p.tempo, 48000);
    seq.set_follow(0, 2 * f); seq.playSong(0, 1); seq.updatePlaybackBuffer();
    assert(c.notes().size() >= 2 && c.notes()[0] == 0 && c.notes()[1] == f - f / 2);
}

void menu_and_help() {
    using namespace pt::ui;
    auto p = score(); ProjectModule menu;
    const auto row = static_cast<int>(ProjectRow::SWING);
    ProjectState s{p}; s.cursorRow = row;
    const auto ctx = menu.cursor_context(s);
    assert(ctx.minValue == 50 && ctx.maxValue == 75 && ctx.largeStep == 5 && ctx.defaultValue == 50);
    assert(menu.handle_input(p, row, 1, InputAction::set_value(67)).modified && p.swing == 67);
    menu.handle_input(p, row, 1, InputAction::set_value(999)); assert(p.swing == 75);
    menu.handle_input(p, row, 1, InputAction::set_value(0)); assert(p.swing == 50);
    assert(pt::ui::detail::project_cell_topic(row, 1) == HelpTopic::PROJECT_SWING);
    for (bool midi : {false, true}) for (bool exit : {false, true}) {
        PlatformCaps caps; caps.midi = midi; caps.appExit = exit;
        assert(project_next_visible_row(static_cast<int>(ProjectRow::TEMPO), 1, caps) == row);
        // Adding the setting must fit the complete desktop/handheld project panel.
        assert(project_row_offset_y(project_last_row(caps), caps, 21) + 21 < ProjectModule::HEIGHT - 40);
    }
}

// The parent QA builds this same fixture against the unmodified scheduler too. Its default-swing
// event stream must stay byte-identical across all eight tracks, custom grooves and timed effects.
void legacy_trace() {
    for (int rate : {44100, 48000}) for (int tempo : {123, 128, 999}) {
        auto p = score(8, 2); p.tempo = tempo;
        p.grooves[1].steps = {13, 11, -1}; p.grooves[2].steps = {0, 24, -1};
        for (int i = 0; i < 16; ++i) {
            auto& step = p.phrases[0].steps[i];
            if (i % 3 == 0) { step.fx1Type = FX_LAT; step.fx1Value = 2; }
            if (i % 4 == 0) { step.fx2Type = FX_KILL; step.fx2Value = 8; }
            if (i == 4 || i == 8 || i == 12) { step.fx3Type = FX_GRV; step.fx3Value = i == 12 ? 0 : i / 4; }
        }
        p.phrases[1].steps[11].fx1Type = FX_HOP; p.phrases[1].steps[11].fx1Value = 3;
        Capture c; const auto duration = render(p, c, rate);
        std::cout.write(reinterpret_cast<const char*>(&duration), sizeof(duration));
        for (const auto& event : c.events)
            std::cout.write(reinterpret_cast<const char*>(&event), sizeof(event));
    }
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--legacy-trace") { legacy_trace(); return 0; }
    save_load(); pair_and_song_duration(); grooves_take_priority(); free_hops_keep_pair_phase(); note_spans_and_effect_tics();
    live_edits_and_transport_reset(); menu_and_help();
    if (argc == 3 && std::string(argv[1]) == "--menu-shot") {
        using namespace pt::ui;
        auto p = score(); p.swing = 67;
        ProjectState state{p}; state.cursorRow = static_cast<int>(ProjectRow::SWING);
        state.caps.midi = true; state.caps.appExit = true; state.caps.debug = true;
        state.sampleRamBytes = 1048576; state.freeRamBytes = 104857600;
        Canvas c; c.surface_rect(0, 0, 640, 480, state.theme.background);
        ProjectModule{}.draw(c, 65, 45, state);
        std::ofstream out(argv[2], std::ios::binary); out << "P6\n640 480\n255\n";
        for (int i = 0; i < 640 * 480; ++i) {
            const auto pixel = c.pixels()[i];
            const char rgb[] = {char(pixel >> 16), char(pixel >> 8), char(pixel)};
            out.write(rgb, 3);
        }
        assert(out.good());
    }
    std::cout << "PASS: project swing persistence, exact pair/song duration, all 8 tracks, custom groove priority, note spans/effect tics, live edits, reset/seek and menu/help\n";
}
