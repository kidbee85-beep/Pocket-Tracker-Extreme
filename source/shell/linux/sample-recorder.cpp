#include "linux/sample-recorder.h"

#include <SDL.h>
#include "vendor/nlohmann/json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include <unistd.h>

namespace ptshell {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
constexpr uint64_t kDiskReserve = 2u * 1024u * 1024u;
constexpr uint64_t kMaxSeconds = 600;
constexpr auto kDriverWarningAfter = std::chrono::seconds(5);
struct Cancelled {};

struct Status {
    std::string state = "idle";
    std::string message = "Select an input and press Record; its availability is checked when recording starts";
    std::string path, routedName;
    uint64_t frames = 0;
    int rate = 48000, channels = 1, requestedId = 0, routedId = 0;
    float peakL = 0, peakR = 0;
};

Json encode(const Status& s) {
    return {{"state", s.state}, {"message", s.message}, {"path", s.path},
            {"frames", s.frames}, {"rate", s.rate}, {"channels", s.channels},
            {"peakL", s.peakL}, {"peakR", s.peakR},
            {"requestedId", s.requestedId}, {"routedId", s.routedId},
            {"routedName", s.routedName}};
}

void le16(unsigned char* p, uint16_t v) {
    p[0] = static_cast<unsigned char>(v); p[1] = static_cast<unsigned char>(v >> 8);
}
void le32(unsigned char* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
}

// A failed/partial write contributes only complete interleaved frames. Finalize
// truncates any trailing partial frame before rewriting a valid RIFF header.
class WavWriter {
public:
    WavWriter(const std::filesystem::path& dir, int rate, int channels)
        : rate_(rate), channels_(channels) {
        std::string pattern = (dir / "capture_XXXXXX.wav").string();
        std::vector<char> name(pattern.begin(), pattern.end()); name.push_back(0);
        const int fd = mkstemps(name.data(), 4);
        if (fd < 0) throw std::runtime_error("Cannot create a recording file");
        path = name.data();
        file_ = fdopen(fd, "wb");
        if (!file_) { ::close(fd); std::filesystem::remove(path); throw std::runtime_error("Cannot open WAV file"); }
        unsigned char header[44]{};
        if (std::fwrite(header, 1, sizeof header, file_) != sizeof header) {
            std::fclose(file_); file_ = nullptr; std::filesystem::remove(path);
            throw std::runtime_error("Cannot write WAV header");
        }
    }
    ~WavWriter() { if (file_) std::fclose(file_); }
    void append(const unsigned char* bytes, size_t count) {
        const size_t frameBytes = static_cast<size_t>(channels_) * 2;
        const size_t wanted = count / frameBytes;
        const size_t written = std::fwrite(bytes, frameBytes, wanted, file_);
        frames += written;
        if (written != wanted) throw std::runtime_error("Recording write failed; partial take kept if possible");
    }
    bool finish() {
        if (!file_) return false;
        const uint32_t bytes = static_cast<uint32_t>(frames * channels_ * 2);
        unsigned char h[44]{};
        std::copy_n(reinterpret_cast<const unsigned char*>("RIFF"), 4, h);
        le32(h + 4, 36 + bytes);
        std::copy_n(reinterpret_cast<const unsigned char*>("WAVEfmt "), 8, h + 8);
        le32(h + 16, 16); le16(h + 20, 1); le16(h + 22, static_cast<uint16_t>(channels_));
        le32(h + 24, static_cast<uint32_t>(rate_));
        le32(h + 28, static_cast<uint32_t>(rate_ * channels_ * 2));
        le16(h + 32, static_cast<uint16_t>(channels_ * 2)); le16(h + 34, 16);
        std::copy_n(reinterpret_cast<const unsigned char*>("data"), 4, h + 36);
        le32(h + 40, bytes);
        bool ok = std::fflush(file_) == 0;
        if (ok) ok = ::ftruncate(fileno(file_), static_cast<off_t>(44u + bytes)) == 0;
        if (ok) ok = std::fseek(file_, 0, SEEK_SET) == 0;
        if (ok) ok = std::fwrite(h, 1, sizeof h, file_) == sizeof h;
        if (std::fclose(file_) != 0) ok = false;
        file_ = nullptr;
        return ok;
    }
    std::string path;
    uint64_t frames = 0;
private:
    std::FILE* file_ = nullptr;
    int rate_, channels_;
};

bool hasSpace(const std::filesystem::path& dir) {
    std::error_code ec;
    const auto info = std::filesystem::space(dir, ec);
    if (ec) throw std::runtime_error("Cannot check free recording space");
    return info.available >= kDiskReserve;
}

std::string sdlError(const char* prefix) {
    const char* error = SDL_GetError();
    return std::string(prefix) + (error && *error ? std::string(": ") + error : "");
}

std::string inputKind(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    // A label hint only: SDL names do not certify hardware or driver support.
    if (name.find("usb") != std::string::npos) return "usb";
    if (name.find("mic") != std::string::npos) return "mic";
    return "input";
}
} // namespace

struct SdlSampleRecorder::Impl {
    explicit Impl(std::string root) : directory(std::filesystem::path(std::move(root)) / "Recordings") {
        // Idle until an explicit input-list request. Internal resampling must
        // not probe the USB/audio driver merely by constructing this backend.
        inputWorker = std::thread([this] { discoverLoop(); });
    }
    ~Impl() {
        std::fprintf(stderr, "capture: shutdown begin recordingPending=%d\n", running.load() ? 1 : 0);
        stopRequested.store(true);
        { std::lock_guard<std::mutex> lock(inputMutex); inputShutdown = true; }
        inputWake.notify_all();
        if (worker.joinable()) worker.join();
        std::fprintf(stderr, "capture: shutdown recording worker returned\n");
        if (inputWorker.joinable()) inputWorker.join();
        std::fprintf(stderr, "capture: shutdown discovery worker returned\n");
    }
    std::filesystem::path directory;
    std::mutex lifecycle, stateMutex, inputMutex, probeMutex;
    std::condition_variable inputWake;
    Status status;
    std::thread worker, inputWorker;
    std::atomic<bool> running{false}, stopRequested{false}, driverTimedOut{false};
    std::map<std::string, int> ids;
    std::map<int, std::string> currentInputs;
    Json cachedInputs = Json::array();
    bool haveInputs = false, inputRequested = false, inputShutdown = false;
    bool discovering = false, discoveryFailed = false, discoveryTimeoutLogged = false;
    Clock::time_point discoveryStarted{};
    std::string driverPhase;
    Clock::time_point driverStarted{};
    bool driverTimeoutLogged = false;
    int nextId = 1;

    template<class F> void update(F f) {
        std::lock_guard<std::mutex> lock(stateMutex); f(status);
    }
    void fail(const std::string& message) {
        update([&](Status& s) { s = Status{}; s.state = "error"; s.message = message; });
    }
    static Json defaultInput() {
        return {{"id", 0}, {"name", "System default (check on Record)"}, {"kind", "default"},
                {"detail", "SDL default route; capture availability is checked when Record is pressed. USB charging alone is not audio."},
                {"channels", {1, 2}}, {"rates", {44100, 48000}}};
    }
    void beginDriver(const char* operation, int id, bool log = true) {
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            driverPhase = operation; driverStarted = Clock::now(); driverTimeoutLogged = false;
        }
        if (log) std::fprintf(stderr, "capture: %s begin requestedId=%d\n", operation, id);
    }
    void endDriver(const char* operation, int id, bool log = true) {
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (Clock::now() - driverStarted >= kDriverWarningAfter) {
                driverTimedOut.store(true); stopRequested.store(true);
            }
            driverPhase.clear();
        }
        if (log) std::fprintf(stderr, "capture: %s end requestedId=%d\n", operation, id);
    }
    Json snapshot() {
        Json result;
        std::string logPhase;
        int logId = 0;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            const bool pending = !driverPhase.empty();
            if (pending && Clock::now() - driverStarted >= kDriverWarningAfter) {
                driverTimedOut.store(true); stopRequested.store(true);
                status.state = "stopping";
                status.message = "Input driver is not responding (" + driverPhase + "). Stop requested; waiting for the driver. Internal resampling is separate.";
                if (!driverTimeoutLogged) {
                    driverTimeoutLogged = true; logPhase = driverPhase; logId = status.requestedId;
                }
            }
            result = encode(status);
            result["driverPending"] = pending;
            result["driverPhase"] = driverPhase;
            result["driverTimedOut"] = driverTimedOut.load();
        }
        if (!logPhase.empty()) std::fprintf(stderr, "capture: %s timeout requestedId=%d; stop requested, driver still pending\n", logPhase.c_str(), logId);
        return result;
    }
    // Never recycle an ID when devices change: a stale selected USB ID must not
    // silently turn into a different microphone after hotplug.
    // Only background workers call SDL discovery. Never hold a UI-facing mutex
    // while entering a driver or waiting for another probe to finish.
    Json probeInputs() {
        std::lock_guard<std::mutex> probe(probeMutex);
        Json result = Json::array();
        std::vector<std::string> names;
        const bool initialized = (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0;
        const int count = initialized ? SDL_GetNumAudioDevices(SDL_TRUE) : 0;
        // Enumeration is not an open test. Some drivers expose a default
        // capture route without listing named devices; only an explicit Record
        // request may probe it. Never substitute it for a missing named input.
        if (initialized) result.push_back(defaultInput());
        std::set<std::string> seen;
        for (int i = 0; i < count; ++i) {
            const char* raw = SDL_GetAudioDeviceName(i, SDL_TRUE);
            if (!raw || !*raw || !seen.insert(raw).second) continue;
            names.emplace_back(raw);
        }
        std::lock_guard<std::mutex> lock(inputMutex);
        currentInputs.clear();
        for (const auto& name : names) {
            const auto inserted = ids.emplace(name, nextId);
            if (inserted.second) ++nextId;
            const int id = inserted.first->second;
            currentInputs.emplace(id, name);
            result.push_back({{"id", id}, {"name", name}, {"kind", inputKind(name)},
                              {"channels", {1, 2}}, {"rates", {44100, 48000}}});
        }
        cachedInputs = result; haveInputs = true; discoveryFailed = false;
        return result;
    }
    void discoverLoop() {
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(inputMutex);
                inputWake.wait(lock, [&] { return inputShutdown || inputRequested; });
                if (inputShutdown) return;
                inputRequested = false; discovering = true;
                discoveryStarted = Clock::now(); discoveryTimeoutLogged = false;
            }
            std::fprintf(stderr, "capture: discovery begin requestedId=-1\n");
            try { probeInputs(); }
            catch (...) { std::lock_guard<std::mutex> lock(inputMutex); discoveryFailed = true; }
            {
                std::lock_guard<std::mutex> lock(inputMutex); discovering = false;
            }
            std::fprintf(stderr, "capture: discovery end requestedId=-1\n");
        }
    }
    Json inputs() {
        // SDL_WasInit reads initialized subsystem flags; it does not enumerate
        // or open hardware. Preserve the pre-existing unavailable-audio guard.
        if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) return Json::array();
        std::unique_lock<std::mutex> lock(inputMutex);
        if (!discovering && !inputShutdown) { inputRequested = true; inputWake.notify_one(); }
        Json result = haveInputs ? cachedInputs : Json::array({defaultInput()});
        const bool timedOut = discovering && Clock::now() - discoveryStarted >= kDriverWarningAfter;
        const bool logTimeout = timedOut && !discoveryTimeoutLogged;
        if (logTimeout) discoveryTimeoutLogged = true;
        for (auto& row : result) {
            if (timedOut || discoveryFailed) {
                row["connected"] = false;
                row["detail"] = timedOut ? "Input discovery is not responding. Cached input only; wait for the driver or use internal resampling."
                                          : "Input discovery failed. Refresh inputs before recording.";
            } else if (discovering || inputRequested) {
                row["detail"] = row.value("detail", std::string()) + " Refreshing inputs in background; selected input is checked again before recording.";
            }
        }
        lock.unlock();
        if (logTimeout) std::fprintf(stderr, "capture: discovery timeout requestedId=-1; returning cached inputs\n");
        return result;
    }
    bool resolveInput(int id, std::string& name) {
        probeInputs();
        if (id == 0) return true;
        std::lock_guard<std::mutex> lock(inputMutex);
        const auto it = currentInputs.find(id);
        if (it == currentInputs.end()) return false;
        name = it->second;
        return true;
    }
    bool stillPresent(int id, const std::string& name) {
        if (id == 0) return true; // The default route belongs to the OS, not SDL.
        probeInputs();
        std::lock_guard<std::mutex> lock(inputMutex);
        const auto it = currentInputs.find(id);
        return it != currentInputs.end() && it->second == name;
    }
    void record(int requestedId, int channels, int rate) {
        SDL_AudioDeviceID device = 0;
        std::unique_ptr<WavWriter> writer;
        std::string name;
        std::string failure, finishMessage = "Recording ready; no input monitoring";
        try {
            if (!stopRequested.load()) {
                std::error_code ec;
                std::filesystem::create_directories(directory, ec);
                if (ec || !std::filesystem::is_directory(directory))
                    throw std::runtime_error("Cannot create Recordings folder");
                if (!hasSpace(directory)) throw std::runtime_error("Not enough free recording space");
                beginDriver("resolve", requestedId);
                const bool found = resolveInput(requestedId, name);
                endDriver("resolve", requestedId);
                if (stopRequested.load()) throw Cancelled{};
                if (!found) throw std::runtime_error("Selected input is no longer connected; refresh inputs (USB requires OS support)");
                SDL_AudioSpec want{}, got{};
                want.freq = rate; want.format = AUDIO_S16LSB;
                want.channels = static_cast<Uint8>(channels); want.samples = 1024;
                beginDriver("open", requestedId);
                device = SDL_OpenAudioDevice(name.empty() ? nullptr : name.c_str(), SDL_TRUE, &want, &got, 0);
                endDriver("open", requestedId);
                if (!device) throw std::runtime_error(sdlError("Cannot open capture input; check OS/USB driver"));
                if (got.freq != rate || got.channels != channels || got.format != AUDIO_S16LSB)
                    throw std::runtime_error("Input did not provide the requested PCM16 format");
                if (!stopRequested.load()) {
                    beginDriver("activate", requestedId); SDL_PauseAudioDevice(device, 0); endDriver("activate", requestedId);
                }
                const auto started = Clock::now();
                auto lastFramesAt = started, lastCheck = started;
                const size_t frameBytes = static_cast<size_t>(channels) * 2;
                std::vector<unsigned char> buffer(32768 * frameBytes);
                while (!stopRequested.load()) {
                    const auto now = Clock::now();
                    // Track an outstanding read without logging per audio poll.
                    beginDriver("read", requestedId, false);
                    if (SDL_GetAudioDeviceStatus(device) != SDL_AUDIO_PLAYING)
                        throw std::runtime_error("Input stopped or disconnected; partial take kept");
                    const Uint32 queued = SDL_GetQueuedAudioSize(device);
                    if (queued > static_cast<Uint32>(rate * frameBytes * 2))
                        throw std::runtime_error("Input overrun; partial take kept");
                    const size_t gotBytes = SDL_DequeueAudio(device, buffer.data(), static_cast<Uint32>(buffer.size()));
                    endDriver("read", requestedId, false);
                    const size_t aligned = gotBytes - gotBytes % frameBytes;
                    if (aligned) {
                        lastFramesAt = now;
                        if (!writer) writer = std::make_unique<WavWriter>(directory, rate, channels);
                        const uint64_t left = static_cast<uint64_t>(rate) * kMaxSeconds - writer->frames;
                        const size_t used = static_cast<size_t>(std::min<uint64_t>(aligned, left * frameBytes));
                        writer->append(buffer.data(), used);
                        float peakL = 0, peakR = 0;
                        for (size_t i = 0; i < used / 2; ++i) {
                            int value = buffer[i * 2] | (static_cast<int>(buffer[i * 2 + 1]) << 8);
                            if (value >= 32768) value -= 65536;
                            const float peak = static_cast<float>(std::abs(value)) / 32768.f;
                            if (channels == 1 || i % channels == 0) peakL = std::max(peakL, peak);
                            else peakR = std::max(peakR, peak);
                        }
                        update([&](Status& s) {
                            s.state = stopRequested.load() ? "stopping" : "recording";
                            s.message = "Recording PCM16; software monitoring off";
                            s.path = writer->path; s.frames = writer->frames;
                            s.routedId = requestedId;
                            s.routedName = name.empty() ? "System default (route managed by SDL)" : name;
                            s.peakL = peakL; s.peakR = channels == 1 ? peakL : peakR;
                        });
                        if (writer->frames >= static_cast<uint64_t>(rate) * kMaxSeconds) {
                            finishMessage = "10 minute limit reached; recording kept"; break;
                        }
                    } else {
                        if (now - lastFramesAt > std::chrono::seconds(2))
                            throw std::runtime_error("Input delivered no frames; check capture device and OS mixer");
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    if (now - lastCheck >= std::chrono::seconds(1)) {
                        lastCheck = now;
                        beginDriver("hotplug-check", requestedId);
                        const bool present = stillPresent(requestedId, name);
                        endDriver("hotplug-check", requestedId);
                        if (!present)
                            throw std::runtime_error("Selected input disconnected; partial take kept");
                        if (!hasSpace(directory)) { finishMessage = "Low free space; recording stopped and kept"; break; }
                        if (now - started >= std::chrono::seconds(kMaxSeconds)) {
                            finishMessage = "10 minute limit reached; recording kept"; break;
                        }
                    }
                }
            }
        } catch (const Cancelled&) { /* A normal Stop during discovery is not a driver failure. */ }
        catch (const std::exception& e) { failure = e.what(); }
        catch (...) { failure = "Audio input failed"; }
        // Preserve a valid file before any driver-close operation that might
        // stall. Do not publish completion or allow another start until the
        // old handle has actually finished closing.
        const uint64_t frames = writer ? writer->frames : 0;
        const std::string path = writer ? writer->path : std::string();
        update([](Status& s) { s.state = "stopping"; s.message = "Finalizing take and closing input; waiting for the driver"; });
        bool finalized = true;
        if (writer) finalized = writer->finish();
        if (!finalized) failure = "Cannot finalize WAV; file kept in Recordings for recovery";
        if (writer && frames == 0) { std::error_code ec; std::filesystem::remove(path, ec); }
        if (device) {
            beginDriver("close", requestedId);
            SDL_PauseAudioDevice(device, 1); SDL_ClearQueuedAudio(device); SDL_CloseAudioDevice(device);
            endDriver("close", requestedId);
        }
        if (driverTimedOut.load() && failure.empty()) failure = "Input driver timed out; recording stopped and any valid partial take was kept";
        {
            // Publishing a completed take and allowing the next start are one
            // transition. A UI poll must not see READY then have start refused
            // because this worker has not yet cleared its running flag.
            std::lock_guard<std::mutex> done(lifecycle);
            update([&](Status& s) {
                s.state = !failure.empty() ? "error" : frames > 0 ? "ready" : "idle";
                s.message = !failure.empty() ? failure : frames > 0 ? finishMessage : "Recording cancelled; no audio captured";
                s.frames = finalized ? frames : 0;
                s.path = frames > 0 && finalized ? path : std::string();
                s.peakL = s.peakR = 0;
            });
            running.store(false);
            { std::lock_guard<std::mutex> lock(stateMutex); driverPhase.clear(); }
        }
    }
};

SdlSampleRecorder::SdlSampleRecorder(std::string appRoot) : impl_(std::make_unique<Impl>(std::move(appRoot))) {}
SdlSampleRecorder::~SdlSampleRecorder() = default;

std::string SdlSampleRecorder::inputs_json() { return impl_->inputs().dump(); }
std::string SdlSampleRecorder::state_json() {
    return impl_->snapshot().dump();
}
bool SdlSampleRecorder::start(int id, int channels, int rate) {
    auto& s = *impl_;
    std::lock_guard<std::mutex> lifecycle(s.lifecycle);
    if (s.running.load()) return false;
    if (s.worker.joinable()) s.worker.join();
    if (id < 0 || (channels != 1 && channels != 2) || (rate != 44100 && rate != 48000)) {
        s.fail("Choose mono/stereo and 44.1 or 48 kHz"); return false;
    }
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
        s.fail("Audio input subsystem is unavailable"); return false;
    }
    if (id != 0) {
        std::lock_guard<std::mutex> lock(s.inputMutex);
        if (s.currentInputs.find(id) == s.currentInputs.end()) {
            s.fail("Selected input is no longer connected; refresh inputs (USB requires OS driver support)"); return false;
        }
    }
    s.stopRequested.store(false);
    s.driverTimedOut.store(false);
    s.update([&](Status& status) {
        status = Status{}; status.state = "starting";
        status.message = "Checking selected input in background; software monitoring off";
        status.rate = rate; status.channels = channels; status.requestedId = id;
    });
    s.running.store(true);
    try { s.worker = std::thread([&s, id, channels, rate] { s.record(id, channels, rate); }); }
    catch (...) { s.running.store(false); s.fail("Cannot start recording worker"); return false; }
    return true;
}
void SdlSampleRecorder::stop() {
    auto& s = *impl_;
    std::lock_guard<std::mutex> lifecycle(s.lifecycle);
    if (!s.running.load()) return;
    s.stopRequested.store(true);
    s.update([](Status& status) {
        if (status.state == "starting" || status.state == "recording") {
            status.state = "stopping"; status.message = "Finishing WAV recording";
        }
    });
}
} // namespace ptshell
