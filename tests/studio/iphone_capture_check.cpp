#include "linux/iphone-recorder.h"
#include "linux/capture-router.h"
#include "vendor/nlohmann/json.hpp"
#include <SDL.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
using Clock = std::chrono::steady_clock;
constexpr int ACTION = ptshell::IPHONE_CAPTURE_ACTION;
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
bool active(const Json& state) {
    const auto s = state.value("state", ""); return s == "starting" || s == "recording" || s == "stopping";
}
template<class F> Json waitState(pt::ui::CaptureBackend& recorder, F condition, int seconds = 7) {
    const auto deadline = Clock::now() + std::chrono::seconds(seconds); Json state;
    do {
        state = Json::parse(recorder.state_json()); if (condition(state)) return state;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (Clock::now() < deadline);
    throw std::runtime_error("Wait timed out: " + state.dump());
}
Json finished(pt::ui::CaptureBackend& recorder, int seconds = 7) { return waitState(recorder, [](const Json& s) { return !active(s); }, seconds); }
std::string text(const fs::path& path) { std::ifstream in(path); return {std::istreambuf_iterator<char>(in), {}}; }
struct Fixture {
    fs::path folder, tools, app; Json config;
    Fixture(const fs::path& base, int n, const fs::path& binary): folder(base / std::to_string(n)), tools(folder / "tools $() & ' premium"), app(folder / "app $() & ' ; ` premium") {
        fs::create_directories(tools); fs::create_directories(app);
        // Keep tiny /tmp filesystems usable even with an unstripped fixture.
        fs::create_symlink(binary, tools / "pte-iphone-usb");
        setenv("PTE_IPHONE_HELPER", (tools / "pte-iphone-usb").c_str(), 1);
        unsetenv("PTE_IPHONE_PAIR_HELPER"); unsetenv("PTE_IPHONE_MUX_HELPER");
        config = {{"devices", {{{"id", "00008030-TESTA1"}, {"name", "Test iPhone"}}}}, {"record", "good"}};
        save();
    }
    void save() { { std::ofstream out(tools / "fixture.new"); out << config.dump(); } fs::rename(tools / "fixture.new", tools / "fixture.json"); }
    void pairing(const fs::path& binary) {
        for (const auto* name : {"pte-usbmuxd", "pte-iphone-pair"}) {
            fs::create_symlink(binary, tools / name);
        }
        config["require_pair"] = true; save();
    }
    Json calls(const std::string& call) const {
        Json found = Json::array(); std::ifstream in(tools / "calls.jsonl"); std::string line;
        while (std::getline(in, line)) { auto row = Json::parse(line); if (row.value("call", "") == call) found.push_back(row); }
        return found;
    }
};
void validTake(const Json& state, const Fixture& f, const std::string& expectedState = "ready") {
    require(state.value("state", "") == expectedState, "Unexpected take state: " + state.dump());
    require(state.value("frames", 0) == 128, "Adapter failed to validate frame count");
    const fs::path output = state.at("path").get<std::string>();
    require(output.parent_path() == f.app / "Recordings", "Recording escaped its app folder");
    require(fs::file_size(output) == uint64_t(44 + 128 * state.at("channels").get<int>() * 2), "Invalid PCM file size");
    require(state.value("peakL", 1.0) == 0.0 && state.value("peakR", 1.0) == 0.0, "Meters were left active after capture");
}
Json observedInputs(ptshell::IphoneSampleRecorder& recorder, size_t count) {
    const auto deadline = Clock::now() + std::chrono::seconds(6); Json rows;
    do {
        rows = Json::parse(recorder.inputs_json());
        if (rows.size() == count && rows.front().value("kind", "") == "iphone") {
            require(rows.front().value("connected", false), "Detected phone was not marked connected");
            require(!rows.front().value("detail", "").empty(), "Discovery diagnostics were hidden");
            return rows;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (Clock::now() < deadline);
    throw std::runtime_error("Phone discovery did not complete: " + rows.dump());
}
}
int main(int argc, char** argv) {
    fs::path base;
    try {
        require(argc >= 2, "Pass the compiled iphone_capture_fixture path");
        const fs::path binary = fs::absolute(argv[1]);
        char pattern[] = "/tmp/pte-adapter-tests-XXXXXX"; const auto made = mkdtemp(pattern);
        require(made, "Cannot create test directory"); base = made; int n = 0;
        {
            Fixture f(base, ++n, binary); ptshell::IphoneSampleRecorder recorder(f.app.string());
            require(!recorder.start(ACTION, 3, 48000), "Invalid channels accepted");
            require(!recorder.start(ACTION, 2, 96000), "Invalid rate accepted");
            require(!recorder.start(ACTION + 9999, 2, 48000), "Unknown phone ID accepted");
            const auto rows = observedInputs(recorder, 1); const auto id = rows[0]["id"].get<int>();
            require(recorder.start(id, 1, 44100), "Explicit mono capture refused");
            require(!recorder.start(ACTION, 2, 48000), "Concurrent raw capture accepted");
            auto initial = Json::parse(recorder.state_json());
            require(initial.value("frames", 1) == 0 && initial.value("state", "") == "starting", "Recording claimed before audio existed");
            auto live = waitState(recorder, [](const Json& s) { return s.value("frames", 0) > 0 || !active(s); });
            require(live.value("state", "") == "recording" && live.value("peakL", 0.0) == 0.25, "Real helper meter/progress missing");
            auto take = finished(recorder); validTake(take, f);
            require(take.value("routedId", 0) == id, "Stable routed ID was not retained");
            require(recorder.start(ACTION, 2, 48000), "Manual stereo capture refused");
            auto second = finished(recorder); validTake(second, f);
            require(second.at("path") != take.at("path"), "Unique take was overwritten");
            const auto calls = f.calls("record");
            require(calls.size() == 2 && calls[0]["udid"] == "00008030-TESTA1", "Wrong serial sent to helper");
            require(calls[0]["path"] == take["path"], "Shell characters changed the capture output argument");
            std::cout << "PASS real process pipes, stereo48k/mono44.1k PCM, stable identity, metering, unique paths and argv\n";
        }
        {
            Fixture f(base, ++n, binary); f.config["devices"] = Json::array(); f.save();
            ptshell::IphoneSampleRecorder recorder(f.app.string());
            const auto rows = Json::parse(recorder.inputs_json());
            require(rows.size() == 1 && rows[0].value("connected", true) == false, "Fallback falsely claims a connected device");
            require(recorder.start(ACTION, 2, 48000), "Manual discovery refused");
            require(finished(recorder).value("message", "").find("No iPhone") != std::string::npos, "No-device error unclear");
            require(f.calls("record").empty(), "No-device discovery activated audio");
        }
        {
            Fixture f(base, ++n, binary); f.config["devices"].push_back({{"id", "00008030-TESTB2"}, {"name", "Second iPhone"}}); f.save();
            ptshell::IphoneSampleRecorder recorder(f.app.string());
            auto rows = observedInputs(recorder, 2); const int first = rows[0]["id"], second = rows[1]["id"];
            require(first != second, "Two phones share an input ID");
            require(recorder.start(ACTION, 2, 48000), "Manual multi-device discovery refused");
            require(finished(recorder).value("message", "").find("Multiple") != std::string::npos, "Ambiguous phone was silently selected");
            require(f.calls("record").empty(), "Ambiguous device activated audio");
            f.config["devices"].erase(0); f.save();
            require(recorder.start(first, 2, 48000), "Stale device check refused");
            require(finished(recorder).value("message", "").find("disconnected") != std::string::npos, "Stale phone ID silently rerouted");
            require(f.calls("record").empty(), "Stale phone recorded the wrong attached device");
            require(recorder.start(second, 2, 48000), "Second explicit device refused"); validTake(finished(recorder), f);
            require(f.calls("record")[0]["udid"] == "00008030-TESTB2", "Explicit second phone serial changed");
            std::cout << "PASS no-device/manual action, multi-device ambiguity and stale-ID protection\n";
        }
        for (const auto* mode : {"zero", "malformed", "longline", "flood", "corrupt", "oversize", "wrongformat", "symlink"}) {
            Fixture f(base, ++n, binary); f.config["record"] = mode; f.save();
            std::ofstream(f.tools / "outside") << "untouched target";
            ptshell::IphoneSampleRecorder recorder(f.app.string()); require(recorder.start(ACTION, 2, 48000), "Failure fixture refused");
            const auto end = finished(recorder);
            require(end.value("state", "") == "error" && end.value("path", "").empty() && end.value("frames", 1) == 0,
                    std::string("Invalid output became importable: ") + mode + " " + end.dump());
            require(text(f.tools / "outside") == "untouched target", "Symlink target changed during cleanup");
            const auto recordings = f.app / "Recordings";
            require(!fs::exists(recordings) || fs::is_empty(recordings), "Invalid take left behind");
        }
        for (const auto* mode : {"partial_error", "lie", "mismatch", "backwards"}) {
            Fixture f(base, ++n, binary); f.config["record"] = mode; f.save();
            ptshell::IphoneSampleRecorder recorder(f.app.string()); require(recorder.start(ACTION, 2, 48000), "Partial fixture refused");
            validTake(finished(recorder), f, "error");
        }
        std::cout << "PASS invalid/empty/oversized/symlink/malformed WAV rejection and valid recoverable partial takes\n";
        {
            Fixture f(base, ++n, binary); f.config["record"] = "partial_cancel"; f.save();
            ptshell::IphoneSampleRecorder recorder(f.app.string()); require(recorder.start(ACTION, 2, 48000), "Partial cancel refused");
            waitState(recorder, [](const Json& s) { return s.value("frames", 0) > 0; }); recorder.stop(); validTake(finished(recorder), f);
            f.config["record"] = "hold"; f.save();
            for (int i = 0; i < 12; ++i) {
                require(recorder.start(ACTION, 2, 48000), "Rapid restart refused"); recorder.stop();
                require(finished(recorder).value("state", "") == "idle", "Empty rapid cancel became importable");
            }
        }
        {
            Fixture f(base, ++n, binary); f.config["record"] = "ignore"; f.save();
            ptshell::IphoneSampleRecorder recorder(f.app.string()); require(recorder.start(ACTION, 2, 48000), "Hung child fixture refused");
            const auto deadline = Clock::now() + std::chrono::seconds(5);
            while (!fs::exists(f.tools / "heartbeat") && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            require(fs::exists(f.tools / "heartbeat"), "Grandchild fixture did not start");
            const auto began = Clock::now(); recorder.stop(); require(finished(recorder).value("state", "") == "idle", "Hung child cancellation failed");
            require(Clock::now() - began < std::chrono::seconds(5), "Helper cancellation was unbounded");
            std::this_thread::sleep_for(std::chrono::milliseconds(60)); const auto before = text(f.tools / "heartbeat");
            std::this_thread::sleep_for(std::chrono::milliseconds(60)); require(before == text(f.tools / "heartbeat"), "Helper grandchild survived process-group cleanup");
        }
        std::cout << "PASS graceful partial finalization, rapid cancellation and SIGKILL of hung helper descendants\n";
        {
            Fixture f(base, ++n, binary); f.pairing(binary);
            ptshell::IphoneSampleRecorder recorder(f.app.string()); require(recorder.start(ACTION, 2, 48000), "Paired capture refused");
            validTake(finished(recorder), f);
            const auto verified=Json::parse(recorder.state_json());
            require(verified.value("phoneDetected",false)&&verified.value("phoneTrusted",false), "Verified Trust status was not exposed separately from audio frames");
            const auto mux = f.calls("mux"); require(mux.size() == 1 && f.calls("pair").size() == 1, "Pairing sequence skipped");
            const fs::path socket = mux[0]["socket"].get<std::string>();
            require(!fs::exists(socket.parent_path()), "Private runtime session was not removed");
            struct stat info{}; require(!lstat((f.app / "private/iphone-pairing").c_str(), &info) && (info.st_mode & 0777) == 0700, "Persistent pair records are not private");
            require(fs::exists(f.tools / "mux-stopped"), "Mux daemon was not stopped before raw capture");
        }
        for (const auto* mode : {"reject", "silent", "malformed", "wait"}) {
            Fixture f(base, ++n, binary); f.pairing(binary); f.config["pair"] = mode; f.save();
            ptshell::IphoneSampleRecorder recorder(f.app.string()); require(recorder.start(ACTION, 2, 48000), "Pair failure fixture refused");
            if (std::string(mode) == "wait") {
                waitState(recorder, [](const Json& s) { return s.value("message", "").find("test phone") != std::string::npos; });
                recorder.stop(); require(finished(recorder).value("state", "") == "idle", "Pairing cancellation failed");
            } else require(finished(recorder).value("state", "") == "error", "Unverified pairing accepted");
            require(f.calls("record").empty(), "Raw USB activated after unverified/cancelled pairing");
            const auto calls = f.calls("mux"); require(calls.size() == 1, "Scoped daemon was not started");
            const fs::path socket = calls[0]["socket"].get<std::string>(); require(!fs::exists(socket.parent_path()), "Failed pairing leaked runtime directory");
            require(fs::exists(f.tools / "mux-stopped"), "Failed pairing leaked daemon");
        }
        {
            Fixture f(base, ++n, binary); f.pairing(binary);
            // Represents a card that cannot provide a private pairing directory.
            std::ofstream(f.app / "private") << "not a private directory";
            ptshell::IphoneSampleRecorder recorder(f.app.string()); require(recorder.start(ACTION, 2, 48000), "Temporary pairing capture refused");
            validTake(finished(recorder), f);
            const auto call = f.calls("mux").at(0);
            const fs::path socket = call["socket"].get<std::string>(), records = call["pairdir"].get<std::string>();
            require(records.parent_path() == socket.parent_path(), "Pairing keys escaped the private temporary session");
            require(!fs::exists(records) && !fs::exists(socket.parent_path()), "Temporary pairing keys survived session cleanup");
            require(text(f.app / "private") == "not a private directory", "Fallback modified the unavailable private folder");
        }
        std::cout << "PASS scoped private pairing, Trust/rejection/cancel flow, verified session and daemon cleanup\n";
        {
            Fixture f(base, ++n, binary); f.config["list"] = "hang"; f.save();
            ptshell::IphoneSampleRecorder recorder(f.app.string());
            const auto began = Clock::now(); (void)recorder.inputs_json();
            require(Clock::now() - began < std::chrono::milliseconds(100), "Discovery blocked the input UI");
            waitState(recorder, [](const Json& s) { return s.value("message", "").find("timed out") != std::string::npos; });
        }
        {
            Fixture f(base, ++n, binary); fs::remove(f.tools / "pte-iphone-usb");
            ptshell::IphoneSampleRecorder recorder(f.app.string()); require(!recorder.start(ACTION, 2, 48000), "Missing helper accepted");
            require(Json::parse(recorder.state_json()).value("message", "").find("missing") != std::string::npos, "Missing helper error unclear");
            // Discovery errors must remain visible before Record is pressed,
            // including when the router's active backend is still SDL.
            ptshell::CaptureRouter router(f.app.string());const auto began=Clock::now();bool visible=false;
            while(Clock::now()-began<std::chrono::seconds(2)){
                const auto rows=Json::parse(router.inputs_json());
                for(const auto& row:rows)if(row.value("kind","")=="iphone-action"){
                    require(!row.value("connected",true),"Missing helper claimed a connected phone");
                    visible=row.value("detail","").find("missing")!=std::string::npos;
                }
                if(visible)break;std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            require(visible,"Router hid asynchronous iPhone helper discovery failure");
            require(f.calls("record").empty(),"Discovery implicitly activated iPhone audio");
        }
        {
            Fixture f(base, ++n, binary); f.config["record"] = "hold"; f.save();
            SDL_setenv("SDL_AUDIODRIVER", "dummy", 1); require(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) == 0, SDL_GetError());
            {
                ptshell::CaptureRouter router(f.app.string()); const auto rows = Json::parse(router.inputs_json());
                require(rows.size() >= 2 && rows[0].value("id", -1) == 0, "Router lost SDL capture inputs");
                require(router.start(ACTION, 2, 48000), "Router refused iPhone mode");
                require(!router.start(0, 2, 48000), "Router allowed concurrent SDL capture"); router.stop(); finished(router);
                require(router.start(0, 1, 48000), "Router lost microphone path after cancellation");
                waitState(router, [](const Json& s) { return s.value("frames", 0) >= 2048; }); router.stop();
                require(finished(router).value("state", "") == "ready", "Router microphone WAV failed");
            }
            SDL_Quit();
        }
        std::cout << "PASS asynchronous discovery timeout, missing helper, preserved SDL path and cross-backend exclusion\n";
        for (const auto* id : {"bad;serial", "bad\nserial", "00008030-TESTA1"}) {
            Fixture f(base, ++n, binary);
            f.config["devices"].push_back({{"id", id}, {"name", "Ambiguous input"}}); f.save();
            ptshell::IphoneSampleRecorder recorder(f.app.string()); require(recorder.start(ACTION, 2, 48000), "Invalid discovery fixture refused");
            const auto end = finished(recorder);
            require(end.value("state", "") == "error" && f.calls("record").empty(), "Invalid/duplicate USB identity activated audio");
        }
        if (argc > 2 && std::string(argv[2]) == "--audio-timeout") {
            Fixture f(base, ++n, binary); f.config["record"] = "timeout"; f.save();
            ptshell::IphoneSampleRecorder recorder(f.app.string()); const auto began = Clock::now(); require(recorder.start(ACTION, 2, 48000), "Audio timeout fixture refused");
            const auto end = finished(recorder, 30);
            require(end.value("state", "") == "error" && end.value("path", "").empty() && end.value("message", "").find("no audio frames") != std::string::npos, "Missing-audio deadline failed");
            require(Clock::now() - began >= std::chrono::milliseconds(25300), "Audio timeout did not allow 25 seconds after the access phase");
            std::cout << "PASS real 25-second missing-audio deadline\n";
        }
        fs::remove_all(base);
        std::cout << "PASS iPhone adapter/router fixture suite. Virtual devices only; no physical iPhone tested.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL iPhone capture: " << e.what() << " (fixtures " << base << ")\n"; SDL_Quit(); return 1;
    }
}
