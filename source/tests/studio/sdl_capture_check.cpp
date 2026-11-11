#include "linux/sample-recorder.h"
#include "vendor/nlohmann/json.hpp"
#include <SDL.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <unistd.h>

using Json = nlohmann::json;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool active(const Json& s) {
    const std::string v = s.value("state", "");
    return v == "starting" || v == "recording" || v == "stopping";
}
template<class F> Json waitFor(ptshell::SdlSampleRecorder& recorder, F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    Json value;
    do {
        value = Json::parse(recorder.state_json());
        if (predicate(value)) return value;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Capture wait timed out: " + value.dump());
}
uint32_t le32(const std::vector<unsigned char>& b, size_t p) {
    return b.at(p) | (uint32_t(b.at(p + 1)) << 8) | (uint32_t(b.at(p + 2)) << 16) | (uint32_t(b.at(p + 3)) << 24);
}
void checkWav(const Json& state, int channels, int rate) {
    require(state.value("state", "") == "ready", "capture did not become ready");
    require(state.value("frames", uint64_t(0)) > 0, "capture has no frames");
    const std::filesystem::path path(state.at("path").get<std::string>());
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    require(bytes.size() >= 44, "missing WAV header");
    require(std::memcmp(bytes.data(), "RIFF", 4) == 0 && std::memcmp(bytes.data() + 8, "WAVEfmt ", 8) == 0,
            "invalid RIFF/fmt header");
    require(bytes.at(20) == 1 && bytes.at(21) == 0, "not PCM");
    require(bytes.at(22) == channels && bytes.at(23) == 0, "channel mismatch");
    require(bytes.at(34) == 16 && bytes.at(35) == 0, "not 16-bit");
    require(le32(bytes, 24) == uint32_t(rate), "sample rate mismatch");
    require(le32(bytes, 28) == uint32_t(rate * channels * 2), "byte rate mismatch");
    const uint64_t expected = state.at("frames").get<uint64_t>() * channels * 2;
    require(le32(bytes, 40) == expected && bytes.size() == expected + 44, "data size mismatch");
    require(le32(bytes, 4) == bytes.size() - 8, "RIFF size mismatch");
    require(path.parent_path().filename() == "Recordings", "recording escaped folder");
}
}

int main() {
    std::filesystem::path root;
    try {
        std::string pattern = (std::filesystem::temp_directory_path() / "pt-sdl-capture-XXXXXX").string();
        std::vector<char> name(pattern.begin(), pattern.end()); name.push_back(0);
        const auto created = mkdtemp(name.data());
        require(created, "cannot create test directory"); root = created;
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        require(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) == 0, SDL_GetError());
        {
            ptshell::SdlSampleRecorder recorder(root.string());
            require(!recorder.start(0, 3, 48000), "invalid channel count accepted");
            require(!recorder.start(0, 1, 12345), "invalid sample rate accepted");
            auto inputs = Json::parse(recorder.inputs_json());
            require(inputs.is_array() && !inputs.empty(), "dummy capture input not found");
            require(!recorder.start(999999, 1, 48000), "unknown input silently used default");
            require(Json::parse(recorder.state_json()).value("message", "").find("no longer connected") != std::string::npos,
                    "missing-device error did not explain selected input");
            require(recorder.start(0, 1, 48000), "mono capture refused");
            require(!recorder.start(0, 2, 44100), "second recording started concurrently");
            auto live = waitFor(recorder, [](const Json& s) { return s.value("frames", uint64_t(0)) >= 2048 || !active(s); });
            require(live.value("state", "") == "recording", "mono capture did not receive queued PCM");
            require(live.at("peakL").get<float>() == 0.f, "dummy driver should produce silence");
            recorder.stop();
            checkWav(waitFor(recorder, [](const Json& s) { return !active(s); }), 1, 48000);

            // Discovery is intentionally asynchronous; do not silently turn the
            // named-device integration test into another default-route test.
            const auto inputsDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (inputs.size() < 2 && std::chrono::steady_clock::now() < inputsDeadline) {
                inputs = Json::parse(recorder.inputs_json());
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            require(inputs.size() > 1, "dummy named input was not discovered asynchronously");
            const int named = inputs.at(1).at("id").get<int>();
            require(recorder.start(named, 2, 44100), "stereo capture refused");
            live = waitFor(recorder, [](const Json& s) { return s.value("frames", uint64_t(0)) >= 2048 || !active(s); });
            require(live.value("state", "") == "recording", "stereo capture did not receive PCM");
            require(live.at("routedId").get<int>() == named, "named input ID was replaced");
            recorder.stop();
            checkWav(waitFor(recorder, [](const Json& s) { return !active(s); }), 2, 44100);
            for (int i = 0; i < 12; ++i) {
                require(recorder.start(0, 1, 48000), "rapid capture restart refused");
                recorder.stop();
                const auto stopped = waitFor(recorder, [](const Json& s) { return !active(s); });
                require(stopped.value("state", "") == "idle" || stopped.value("state", "") == "ready",
                        "rapid stop produced an error");
            }
        }
        {
            std::ofstream(root / "not-a-folder") << "fixture";
            ptshell::SdlSampleRecorder unavailable((root / "not-a-folder").string());
            require(unavailable.start(0, 1, 48000), "directory validation should run on worker");
            const auto failed = waitFor(unavailable, [](const Json& s) { return !active(s); });
            require(failed.value("state", "") == "error" && failed.value("path", "").empty(),
                    "unwritable destination did not fail safely");
        }
        {
            ptshell::SdlSampleRecorder closing(root.string());
            require(closing.start(0, 1, 48000), "destructor test did not start");
        }
        SDL_Quit();
        {
            ptshell::SdlSampleRecorder notInitialized(root.string());
            require(!notInitialized.start(0, 1, 48000), "capture started after SDL_Quit");
        }
        std::filesystem::remove_all(root);
        std::cout << "PASS SDL dummy capture: mono48k/stereo44.1k WAV, named device, invalid/stale ID, rapid stop, directory failure and lifecycle. Virtual input only.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL SDL capture: " << e.what() << '\n';
        SDL_Quit();
        if (!root.empty()) std::filesystem::remove_all(root);
        return 1;
    }
}
