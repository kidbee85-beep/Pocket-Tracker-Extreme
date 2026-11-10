#include "audio-engine.h"
#include "songcore/project_io.h"
#include "ui/studio.h"
#include "ui/std_filesystem.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using namespace pt::ui;
using songcore::json;
namespace {
void u16(std::ofstream& f, uint16_t value) {
    const char bytes[] = {char(value), char(value >> 8)}; f.write(bytes, 2);
}
void u32(std::ofstream& f, uint32_t value) {
    const char bytes[] = {char(value), char(value >> 8), char(value >> 16), char(value >> 24)}; f.write(bytes, 4);
}
void wav(const std::filesystem::path& file, int rate, int channels, int frames, bool silence = false) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream f(file, std::ios::binary);
    const uint32_t bytes = static_cast<uint32_t>(frames * channels * 2);
    f.write("RIFF", 4); u32(f, 36 + bytes); f.write("WAVEfmt ", 8); u32(f, 16);
    u16(f, 1); u16(f, static_cast<uint16_t>(channels)); u32(f, static_cast<uint32_t>(rate));
    u32(f, static_cast<uint32_t>(rate * channels * 2)); u16(f, static_cast<uint16_t>(channels * 2)); u16(f, 16);
    f.write("data", 4); u32(f, bytes);
    for (int i = 0; i < frames; ++i) for (int ch = 0; ch < channels; ++ch) {
        const int16_t value = silence ? 0 : static_cast<int16_t>(std::sin(i * (ch ? .057 : .031)) * 9000);
        u16(f, static_cast<uint16_t>(value));
    }
    assert(f.good());
}
std::string bytes(const std::filesystem::path& file) {
    std::ifstream f(file, std::ios::binary); assert(f);
    return {std::istreambuf_iterator<char>(f), {}};
}
struct FakeCaptureBackend final : CaptureBackend {
    json inputs = json::array({
        {{"id", 0}, {"name", "System default"}, {"kind", "default"}, {"channels", {1,2}}, {"rates", {44100,48000}}},
        {{"id", 31}, {"name", "Built-in microphone"}, {"kind", "mic"}, {"channels", {1}}, {"rates", {48000}}},
        {{"id", 83}, {"name", "Class-compliant USB interface"}, {"kind", "usb"}, {"channels", {1,2}}, {"rates", {44100,48000}}}
    });
    json snapshot;
    std::string overrideJson, nextStartState = "starting";
    bool accepted = true;
    int starts = 0, stops = 0, polls = 0, listReads = 0;
    int device = -1, channels = -1, rate = -1;
    FakeCaptureBackend() { state("idle"); }
    void state(const std::string& value, const std::string& path = "", int64_t frames = 0,
               int sampleRate = 48000, int channelCount = 1) {
        overrideJson.clear();
        snapshot = {{"state", value}, {"message", "Fake input state: " + value}, {"path", path},
                    {"frames", frames}, {"rate", sampleRate}, {"channels", channelCount},
                    {"peakL", .3}, {"peakR", .2}, {"requestedId", device < 0 ? 0 : device},
                    {"routedId", device > 0 ? device : 31}, {"routedName", device == 83 ? "USB interface" : "Built-in microphone"}};
    }
    std::string inputs_json() override { ++listReads; return inputs.dump(); }
    bool start(int input, int count, int sampleRate) override {
        ++starts; device = input; channels = count; rate = sampleRate; state(nextStartState);
        return accepted;
    }
    void stop() override { ++stops; snapshot["state"] = "stopping"; }
    std::string state_json() override { ++polls; return overrideJson.empty() ? snapshot.dump() : overrideJson; }
};
class TestFileSystem final : public StdFileSystem {
public:
    using StdFileSystem::StdFileSystem;
    bool failCopy = false;
    int copies = 0;
    bool copy_file(const std::string& from, const std::string& to) override {
        ++copies; return !failCopy && StdFileSystem::copy_file(from, to);
    }
};
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto out = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(out / "private");
    auto engine = std::make_unique<AudioEngine>(); engine->setDeviceSampleRate(48000);
    songcore::SongcoreHost host(engine.get(), 48000);
    assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
    TestFileSystem fs((out / "app-data").string());
    FakeCaptureBackend fake;
    AppState app; app.project = &host.edit_project(); app.studioOpen = true; app.studioTab = 4;
    app.currentInstrument = 77; app.capture.targetInstrument=77;
    // The desktop/no-backend path must not mutate a project.
    app.studioRow = 4; studio_capture_action(app, host, *engine, fs);
    assert(app.studioMessage.find("unavailable") != std::string::npos);
    app.captureBackend = &fake; app.studioMessage.clear();
    studio_capture_refresh(app);
    assert(app.capture.inputs.size() == 3 && app.capture.inputs[2].id == 83);
    app.capture.selected = 1;
    std::swap(fake.inputs[0], fake.inputs[2]); studio_capture_refresh(app);
    assert(app.capture.inputs[app.capture.selected].id == 31);
    std::swap(fake.inputs[0], fake.inputs[2]); studio_capture_refresh(app); app.capture.selected = 0;

    uint64_t clock = 1000;
    auto poll = [&] { app.studioOpen=true;app.studioTab=4;clock += 125; assert(studio_poll_capture(app, clock)); };
    poll(); const int polls = fake.polls;
    assert(!studio_poll_capture(app, clock + 50) && fake.polls == polls);
    clock += 100; assert(studio_poll_capture(app, clock) && fake.polls == polls + 1);
    auto edit = [&](int row, Button button) {
        app.studioRow = row; ButtonMods mods; mods.a = true;
        assert(studio_button({button, ButtonAction::PRESSED, mods}, app, host, *engine, fs));
        assert(studio_button({Button::A, ButtonAction::RELEASED, {}}, app, host, *engine, fs));
    };
    auto action = [&](int row) {
        app.studioRow = row; studio_capture_action(app, host, *engine, fs);
    };
    edit(0, Button::DPAD_RIGHT); assert(app.capture.inputs[app.capture.selected].id == 31);
    edit(0, Button::DPAD_RIGHT); assert(app.capture.inputs[app.capture.selected].id == 83);
    edit(1, Button::DPAD_RIGHT); assert(app.capture.channels == 2);
    edit(2, Button::DPAD_LEFT); assert(app.capture.rate == 44100);
    edit(3, Button::DPAD_RIGHT); assert(app.capture.targetInstrument == 78 && app.currentInstrument==77);
    edit(3, Button::DPAD_LEFT); assert(app.capture.targetInstrument == 77 && app.currentInstrument==77);
    fake.nextStartState = "permission"; fake.accepted = false;
    action(4); assert(fake.starts == 1 && fake.device == 83 && fake.channels == 2 && fake.rate == 44100);
    assert(app.capture.state == "permission" && !app.capture.active());

    const auto stereo = out / "private" / "capture-stereo.wav";
    wav(stereo, 48000, 2, 2048);
    // Prefer the independently tested JVM writer fixture when this workspace has it.
    const auto repo = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    const auto jvm = repo.parent_path().parent_path() / "work/design-005/capture-check/wav/streamed.wav";
    bool usedJvmFixture = false; int stereoFrames = 2048;
    if (std::filesystem::exists(jvm)) {
        std::filesystem::copy_file(jvm, stereo, std::filesystem::copy_options::overwrite_existing);
        usedJvmFixture = true; stereoFrames = 262144;
    }
    // Even stale-looking status fields may not make permission/idle/active states importable.
    for (const char* state : {"permission", "idle", "starting", "recording", "stopping"}) {
        fake.state(state, stereo.string(), stereoFrames, 48000, 2); poll();
        const int copied = fs.copies; const auto before = host.project().instruments[77].sampleFilePath;
        action(5); assert(fs.copies == copied && host.project().instruments[77].sampleFilePath == before);
    }
    fake.state("idle"); poll(); fake.nextStartState = "starting"; fake.accepted = true;
    engine->studioRack.punch.store(5); action(4);
    assert(fake.starts == 2 && app.capture.active() && engine->studioRack.punch.load() == 0);
    const int selected = app.capture.selected, channels = app.capture.channels, rate = app.capture.rate, target = app.currentInstrument;
    for (const char* state : {"starting", "recording", "stopping"}) {
        fake.state(state); poll();
        for (int row = 0; row < 4; ++row) edit(row, Button::DPAD_LEFT);
        assert(app.capture.selected == selected && app.capture.channels == channels && app.capture.rate == rate && app.currentInstrument == target);
    }
    fake.state("recording"); poll();
    const int stopped = fake.stops; action(4);
    assert(fake.stops == stopped + 1 && app.capture.state == "stopping");
    fake.state("recording"); poll();
    assert(!studio_button({Button::START, ButtonAction::PRESSED, {}}, app, host, *engine, fs));
    assert(fake.stops == stopped + 1); // Playback transport remains independent of capture.
    ButtonMods page; page.b = true;
    studio_button({Button::DPAD_RIGHT, ButtonAction::PRESSED, page}, app, host, *engine, fs);
    assert(app.studioTab == 5 && fake.stops == stopped + 2);
    studio_button({Button::B, ButtonAction::RELEASED, {}}, app, host, *engine, fs);
    app.studioTab = 4; fake.state("recording"); poll();
    studio_button({Button::B, ButtonAction::RELEASED, {}}, app, host, *engine, fs);
    assert(!app.studioOpen && fake.stops == stopped + 3);
    app.studioOpen = true; app.studioTab = 4;

    auto& old = host.edit_project().instruments[77];
    old.instrumentType = songcore::InstrumentType::EXTERNAL; old.name = "OLD SYNTH";
    old.root = {0, 2}; old.filterType = "lp"; old.filterCut = 0; old.drive = 255;
    old.reverse = true; old.tableId = 12; old.modSlots[0].type = songcore::ModType::AHD;
    old.modSlots[0].dest = songcore::ModDest::VOLUME; old.modSlots[0].decay = 1;
    host.edit_project().instruments[0].name = "UNRELATED KEEP";
    app.capture.targetInstrument=78;
    fake.state("ready", stereo.string(), stereoFrames, 48000, 2); poll(); action(5);
    const auto imported = app.capture.importedPath;
    assert(!imported.empty() && fs.file_exists(imported) && fs.file_exists(stereo.string()));
    assert(std::filesystem::path(imported).parent_path() == out / "app-data/Samples/Recordings");
    assert(bytes(imported) == bytes(stereo));
    const auto& clean = host.project().instruments[78];
    assert(host.project().instruments[77].name=="OLD SYNTH" && host.project().instruments[77].instrumentType==songcore::InstrumentType::EXTERNAL);
    assert(!app.studioOpen && app.pendingSampleEditor && app.currentInstrument==78);
    assert(clean.id == 78 && clean.sampleId == 78 && clean.sampleFilePath == imported);
    assert(clean.instrumentType == songcore::InstrumentType::SAMPLER && !clean.soundfontPath);
    assert(clean.loopMode == "off" && clean.volume == 170 && !clean.reverse && clean.tableId == -1);
    assert(clean.filterType == "off" && clean.drive == 0 && songcore::note_to_midi(clean.root) == 60);
    for (const auto& mod : clean.modSlots) assert(mod.type == songcore::ModType::NONE);
    assert(engine->getSampleLength(78) == stereoFrames && engine->hasStereoData(78));
    assert(host.project().instruments[0].name == "UNRELATED KEEP");
    host.preview_instrument(78); float audio[1024]{}; double energy = 0;
    for (int block = 0; block < 6; ++block) {
        engine->processLiveBlock(audio, 512, 2, 48000);
        for (float value : audio) { assert(std::isfinite(value)); energy += value * value; }
    }
    host.stop_preview(); assert(energy > 1e-8);
    const int copied = fs.copies; app.capture.targetInstrument = 92; action(5);
    assert(fs.copies == copied && host.project().instruments[92].sampleFilePath == imported);

    // Starting another take immediately invalidates the previous export association.
    fake.state("ready", stereo.string(), stereoFrames, 48000, 2); poll(); action(4);
    assert(app.capture.importedPath.empty() && app.capture.path.empty());
    const auto mono = out / "private" / "second" / "capture-stereo.wav";
    wav(mono, 44100, 1, 1024);
    fake.state("error", mono.string(), 1024, 44100, 1); poll();
    app.capture.targetInstrument=93;fs.failCopy = true; const auto keep = host.project().instruments[92].sampleFilePath; action(5);
    assert(app.capture.importedPath.empty() && host.project().instruments[92].sampleFilePath == keep);
    assert(fs.file_exists(mono.string()) && app.studioMessage.find("original kept") != std::string::npos);
    fs.failCopy = false; action(5);
    assert(app.capture.importedPath != imported && bytes(imported) == bytes(stereo));
    assert(bytes(app.capture.importedPath) == bytes(mono));
    assert(engine->getSampleLength(93) == 1024 && !engine->hasStereoData(93));
    assert(app.capture.actualRate == 44100 && app.capture.routedId == 83);

    const auto silent = out / "private" / "silence.wav"; wav(silent, 48000, 1, 512, true);
    fake.state("ready", silent.string(), 512); poll(); app.capture.targetInstrument = 37; action(5);
    assert(engine->getSampleLength(37) == 512);
    float silentData[512]{}; engine->getSampleData(37, silentData);
    for (float value : silentData) assert(value == 0); // A deliberately silent take remains valid PCM.
    const auto invalid = out / "private" / "invalid.wav"; std::ofstream(invalid) << "broken WAV";
    const auto intact = host.project().instruments[37].sampleFilePath;
    fake.state("error", invalid.string(), 999); poll();app.capture.targetInstrument=38;action(5);
    assert(host.project().instruments[37].sampleFilePath == intact && engine->getSampleLength(37) == 512);
    assert(app.studioMessage.find("load failed") != std::string::npos && fs.file_exists(invalid.string()));

    fake.state("idle"); fake.snapshot["peakL"] = 9.; fake.snapshot["peakR"] = -2.; poll();
    assert(app.capture.peakL == 0.f && app.capture.peakR == 0.f);
    fake.overrideJson = "malformed JSON"; poll(); assert(app.capture.message == "Input status unavailable");
    fake.overrideJson.clear(); fake.inputs = json::array(); action(6);
    const int beforeEmpty = fake.starts; action(4);
    assert(fake.starts == beforeEmpty && app.studioMessage.find("No input")!=std::string::npos);
    const json report = {{"passed", true}, {"jvm_fixture", usedJvmFixture}, {"copy_attempts", fs.copies},
                         {"starts", fake.starts}, {"stops", fake.stops},
                         {"checks", "input IDs/edit locks/poll cache/state guards/exit stop/fresh sampler/import copy reuse/collision/partial error/silence/copy and decode failures"}};
    std::ofstream(out / "capture-check.json") << report.dump(2) << '\n';
    std::cout << "PASS: capture UI routing and guards, actual WAV imports, PCM playback, partial recovery, copy/decode preservation" << std::endl;
}
