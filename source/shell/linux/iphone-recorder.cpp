#include "linux/iphone-recorder.h"
#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ptshell {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
constexpr uint64_t MAX_SECONDS = 600;
constexpr uint64_t MAX_WAV_BYTES = 44 + MAX_SECONDS * 48000 * 2 * 2 + 1024 * 1024;
constexpr uint64_t DISK_RESERVE = 2 * 1024 * 1024;
constexpr size_t LINE_LIMIT = 65536, ENUM_LIMIT = 1024 * 1024, RECORD_JSON_LIMIT = 8 * 1024 * 1024;
struct Cancelled {};
struct Phone { std::string id, name; int inputId = 0; };

std::string boundedText(const Json& j, const char* key, const std::string& fallback = {}) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return fallback;
    auto text = it->get<std::string>();
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return c < 32 && c != '\t'; }), text.end());
    return text.substr(0, 512);
}
bool validSerial(const std::string& value) {
    if (value.empty() || value.size() > 128) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}
Json idleState() {
    return {{"state", "idle"}, {"message", "iPhone USB is experimental. Connect to USB HOST, unlock and refresh."},
            {"path", ""}, {"frames", 0}, {"rate", 48000}, {"channels", 2},
            {"requestedId", IPHONE_CAPTURE_ACTION}, {"routedId", 0}, {"routedName", ""},
            {"peakL", 0.0}, {"peakR", 0.0}, {"phoneDetected", false}, {"phoneTrusted", false}};
}
bool executable(const fs::path& path) {
    std::error_code error; return fs::is_regular_file(path, error) && access(path.c_str(), X_OK) == 0;
}
std::string findHelper(const fs::path& root) {
    if (const char* override = std::getenv("PTE_IPHONE_HELPER")) return fs::absolute(override).string();
    for (const auto& p : {root / "../tools/pte-iphone-usb", root / "tools/pte-iphone-usb"})
        if (executable(p)) return p.lexically_normal().string();
    return (root / "../tools/pte-iphone-usb").lexically_normal().string();
}
void closeFd(int& fd) { if (fd >= 0) close(fd); fd = -1; }
void terminateGroup(pid_t child, int signal, bool fallback = true) {
    if (child <= 1) return;
    if (kill(-child, signal) != 0 && errno == ESRCH && fallback) kill(child, signal);
}

// All argv storage is prepared before fork. The child uses only async-signal-safe
// operations until exec; it cannot run shell substitutions from names or paths.
class Child {
public:
    pid_t pid = -1; int out = -1, err = -1, status = -1; bool reaped = false, cleaned = false;
    int graceMs;
    Child(const std::string& program, const std::vector<std::string>& arguments, int grace = 1500): graceMs(grace) {
        std::vector<char*> argv; argv.push_back(const_cast<char*>(program.c_str()));
        for (const auto& value : arguments) argv.push_back(const_cast<char*>(value.c_str()));
        argv.push_back(nullptr);
        int stdoutPipe[2] = {-1, -1}, stderrPipe[2] = {-1, -1};
        if (pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0) {
            for (int fd : stdoutPipe) if (fd >= 0) close(fd);
            for (int fd : stderrPipe) if (fd >= 0) close(fd);
            throw std::runtime_error("Cannot create iPhone helper pipes");
        }
        for (int fd : {stdoutPipe[0], stdoutPipe[1], stderrPipe[0], stderrPipe[1]}) fcntl(fd, F_SETFD, FD_CLOEXEC);
        const long configuredMax = sysconf(_SC_OPEN_MAX);
        const long maxFd = configuredMax > 0 ? std::max<long>(4, std::min<long>(4096, configuredMax)) : 4096;
        pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            struct sigaction action{}; action.sa_handler = SIG_DFL; sigemptyset(&action.sa_mask);
            sigaction(SIGTERM, &action, nullptr); sigaction(SIGINT, &action, nullptr); sigaction(SIGHUP, &action, nullptr);
            dup2(stdoutPipe[1], STDOUT_FILENO); dup2(stderrPipe[1], STDERR_FILENO);
            const int devnull = open("/dev/null", O_RDONLY); if (devnull >= 0) dup2(devnull, STDIN_FILENO);
            for (int fd = 3; fd < maxFd; ++fd) close(fd);
            execv(program.c_str(), argv.data()); _exit(127);
        }
        close(stdoutPipe[1]); close(stderrPipe[1]); out = stdoutPipe[0]; err = stderrPipe[0];
        if (pid < 0) { closeFd(out); closeFd(err); throw std::runtime_error("Cannot start iPhone helper"); }
        setpgid(pid, pid);
        fcntl(out, F_SETFL, fcntl(out, F_GETFL) | O_NONBLOCK);
        fcntl(err, F_SETFL, fcntl(err, F_GETFL) | O_NONBLOCK);
    }
    ~Child() { stop(); closeFd(out); closeFd(err); }
    bool poll() {
        if (reaped) return true;
        const auto result = waitpid(pid, &status, WNOHANG);
        if (result == pid || (result < 0 && errno == ECHILD)) reaped = true;
        return reaped;
    }
    void stop() {
        if (pid <= 1 || cleaned) return;
        if (!reaped) {
            terminateGroup(pid, SIGTERM);
            const auto deadline = Clock::now() + std::chrono::milliseconds(graceMs);
            while (!poll() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (!reaped) { terminateGroup(pid, SIGKILL); while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} reaped = true; }
        }
        // A helper may have left descendants after it exited on SIGTERM.
        // Once reaped, never signal the positive PID: it can already be reused.
        terminateGroup(pid, SIGKILL, false); cleaned = true;
    }
    bool success() const { return reaped && status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0; }
};
size_t readAvailable(int& fd, std::string& text, size_t maxBytes, bool truncate = false) {
    if (fd < 0) return 0;
    std::array<char, 4096> data{}; size_t budget = 256 * 1024;
    size_t consumed = 0;
    while (budget) {
        const auto count = read(fd, data.data(), std::min(data.size(), budget));
        if (count > 0) {
            consumed += static_cast<size_t>(count);
            budget -= static_cast<size_t>(count);
            if (text.size() + static_cast<size_t>(count) > maxBytes) {
                if (!truncate) throw std::runtime_error("iPhone helper response exceeds the size limit");
                const size_t left = maxBytes > text.size() ? maxBytes - text.size() : 0;
                text.append(data.data(), left);
            } else text.append(data.data(), static_cast<size_t>(count));
        } else if (count == 0) { closeFd(fd); break; }
        else if (errno == EINTR) continue;
        else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        else throw std::runtime_error("Cannot read iPhone helper status");
    }
    return consumed;
}
class JsonLines {
    std::string buffer;
    size_t total = 0, limit;
public:
    explicit JsonLines(size_t maxBytes): limit(maxBytes) {}
    template<class F> void drain(int& fd, F handle) {
        total += readAvailable(fd, buffer, buffer.size() + (limit - total));
        size_t newline;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            if (newline > LINE_LIMIT) throw std::runtime_error("iPhone status line exceeds the size limit");
            auto line = buffer.substr(0, newline); buffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) handle(line);
        }
        if (buffer.size() > LINE_LIMIT) throw std::runtime_error("iPhone status line exceeds the size limit");
    }
    template<class F> void finish(int& fd, F handle) {
        // A finished child can still have more than one read-budget in its pipe.
        // Bound the total independently of the size of each buffered line.
        while (fd >= 0) {
            const auto previous = total; drain(fd, handle);
            if (previous == total) break;
        }
        if (!buffer.empty()) { handle(buffer); buffer.clear(); }
    }
};
std::vector<Phone> listPhones(const std::string& helper, const std::function<bool()>& cancel) {
    if (!executable(helper)) throw std::runtime_error("iPhone USB helper missing. Reinstall the complete Brick package.");
    Child child(helper, {"list"}); std::string raw, error;
    const auto deadline = Clock::now() + std::chrono::seconds(4);
    while (true) {
        if (cancel()) { child.stop(); throw Cancelled{}; }
        readAvailable(child.out, raw, ENUM_LIMIT); readAvailable(child.err, error, 8192, true);
        if (child.poll()) { readAvailable(child.out, raw, ENUM_LIMIT); break; }
        if (Clock::now() >= deadline) { child.stop(); throw std::runtime_error("iPhone discovery timed out. Reconnect, unlock and refresh."); }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!child.success()) throw std::runtime_error("iPhone discovery failed. Reconnect, unlock and refresh.");
    auto value = Json::parse(raw, nullptr, false);
    if (!value.is_object() || value.value("protocol_version", 0) != 1 || !value.contains("devices") || !value["devices"].is_array())
        throw std::runtime_error("iPhone helper returned an unsupported discovery response");
    const auto failure = boundedText(value, "error"); if (!failure.empty()) throw std::runtime_error(failure);
    std::vector<Phone> phones; std::set<std::string> seen;
    for (const auto& row : value["devices"]) {
        if (!row.is_object() || phones.size() == 16) throw std::runtime_error("Invalid iPhone device list");
        const auto serial = row.contains("id") && row["id"].is_string() ? row["id"].get<std::string>() : std::string();
        if (!validSerial(serial) || !seen.insert(serial).second) throw std::runtime_error("Invalid or ambiguous iPhone USB identity");
        auto name = boundedText(row, "name", "iPhone / iPad").substr(0, 120);
        phones.push_back({serial, name.empty() ? "iPhone / iPad" : name, 0});
    }
    return phones;
}
uint16_t u16(const unsigned char* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
struct Wave { uint64_t frames = 0; int rate = 0, channels = 0; };
Wave validateWave(const fs::path& path, int rate, int channels) {
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) throw std::runtime_error("No completed iPhone WAV is available");
    struct Guard { int fd; ~Guard() { close(fd); } } guard{fd};
    struct stat info{};
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 44 || uint64_t(info.st_size) > MAX_WAV_BYTES)
        throw std::runtime_error("iPhone recording has an invalid file size");
    const uint64_t size = uint64_t(info.st_size); std::array<unsigned char, 40> header{};
    if (pread(fd, header.data(), 12, 0) != 12 || std::memcmp(header.data(), "RIFF", 4) || std::memcmp(header.data() + 8, "WAVE", 4) || uint64_t(u32(header.data() + 4)) + 8 != size)
        throw std::runtime_error("iPhone recording was not finalized as a valid WAV");
    bool format = false, data = false; uint64_t dataBytes = 0, cursor = 12;
    while (cursor + 8 <= size) {
        if (pread(fd, header.data(), 8, static_cast<off_t>(cursor)) != 8) throw std::runtime_error("Truncated iPhone WAV chunk");
        const uint64_t bytes = u32(header.data() + 4), payload = cursor + 8;
        if (bytes > size - payload) throw std::runtime_error("Truncated iPhone WAV payload");
        if (!std::memcmp(header.data(), "fmt ", 4)) {
            if (format || bytes < 16 || bytes > 4096 || pread(fd, header.data(), 16, static_cast<off_t>(payload)) != 16)
                throw std::runtime_error("Invalid iPhone WAV format chunk");
            if (u16(header.data()) != 1 || u16(header.data() + 2) != channels || u32(header.data() + 4) != uint32_t(rate) || u16(header.data() + 14) != 16 || u16(header.data() + 12) != channels * 2 || u32(header.data() + 8) != uint32_t(rate * channels * 2))
                throw std::runtime_error("iPhone recording is not the requested PCM16 mono/stereo format");
            format = true;
        } else if (!std::memcmp(header.data(), "data", 4)) {
            if (data) throw std::runtime_error("Multiple iPhone WAV data chunks are unsupported");
            data = true; dataBytes = bytes;
        }
        cursor = payload + bytes + (bytes & 1);
    }
    if (!format || !data || cursor != size || !dataBytes || dataBytes % uint64_t(channels * 2) || dataBytes / (channels * 2) > uint64_t(rate) * MAX_SECONDS)
        throw std::runtime_error("iPhone recording contains no complete bounded audio frames");
    return {dataBytes / uint64_t(channels * 2), rate, channels};
}
std::string reservePath(const fs::path& directory) {
    std::error_code error; fs::create_directories(directory, error);
    if (error || !fs::is_directory(directory)) throw std::runtime_error("Cannot create the Recordings folder");
    const auto space = fs::space(directory, error);
    if (error || space.available < DISK_RESERVE) throw std::runtime_error("Not enough free space for iPhone recording");
    auto pattern = (directory / "iphone_usb_XXXXXX.wav").string();
    std::vector<char> name(pattern.begin(), pattern.end()); name.push_back(0);
    const int fd = mkstemps(name.data(), 4);
    if (fd < 0) throw std::runtime_error("Cannot reserve an iPhone recording file");
    close(fd); return name.data();
}
void privateDirectory(const fs::path& directory) {
    std::error_code error; fs::create_directories(directory, error);
    struct stat info{};
    if (error || lstat(directory.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() || chmod(directory.c_str(), 0700) ||
        lstat(directory.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() || (info.st_mode & 0777) != 0700)
        throw std::runtime_error("Cannot create a private iPhone pairing folder");
}
struct RuntimeDirectory {
    fs::path path;
    RuntimeDirectory() {
        char pattern[] = "/tmp/pte-iphone-XXXXXX";
        const auto created = mkdtemp(pattern);
        if (!created) throw std::runtime_error("Cannot create the private iPhone USB session");
        path = created;
    }
    ~RuntimeDirectory() { std::error_code error; fs::remove_all(path, error); }
};
}

struct IphoneSampleRecorder::Impl {
    fs::path root, directory; std::string helper, pairHelper, muxHelper;
    std::mutex stateMutex, commandMutex; std::condition_variable wake;
    Json state = idleState(); std::vector<Phone> current;
    std::string discoveryDetail="Checking iPhone USB devices...";
    std::map<std::string, int> identities; int nextId = IPHONE_CAPTURE_ACTION + 1;
    bool pending = false; int pendingId = 0, pendingRate = 0, pendingChannels = 0; std::string pendingSerial;
    std::atomic<bool> quit{false}, stopping{false}, running{false}, scanRequested{true};
    Clock::time_point nextScan{}; std::thread worker;
    explicit Impl(const std::string& appRoot): root(fs::absolute(appRoot).lexically_normal()), directory(root / "Recordings"), helper(findHelper(root)) {
        const fs::path tools = fs::path(helper).parent_path();
        const auto sibling = [&](const char* env, const char* name) {
            const char* override = std::getenv(env);
            return override ? fs::absolute(override).string() : (tools / name).string();
        };
        pairHelper = sibling("PTE_IPHONE_PAIR_HELPER", "pte-iphone-pair");
        muxHelper = sibling("PTE_IPHONE_MUX_HELPER", "pte-usbmuxd");
        worker = std::thread([this] { loop(); });
    }
    ~Impl() { quit = true; stopping = true; wake.notify_all(); if (worker.joinable()) worker.join(); }
    void fail(const std::string& message, int id = IPHONE_CAPTURE_ACTION) {
        std::lock_guard<std::mutex> lock(stateMutex); state = idleState(); state["state"] = "error"; state["message"] = message; state["requestedId"] = id;
    }
    void setPhones(std::vector<Phone> phones) {
        std::lock_guard<std::mutex> lock(stateMutex);
        for (auto& phone : phones) {
            auto it = identities.find(phone.id);
            if (it == identities.end()) {
                if (identities.size() >= 4096) throw std::runtime_error("Too many USB identities; restart the tracker");
                it = identities.emplace(phone.id, nextId++).first;
            }
            phone.inputId = it->second;
        }
        current = std::move(phones);
        discoveryDetail=current.empty()?"No iPhone detected. Connect to USB HOST, unlock and refresh.":"Phone detected. Press Record to verify Trust and request audio.";
    }
    void scan() {
        try {
            auto phones = listPhones(helper, [this] { return quit.load() || running.load(); });
            setPhones(std::move(phones));
        } catch (const Cancelled&) {}
        catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(stateMutex); current.clear();
            discoveryDetail=e.what();
            // Discovery cannot erase a completed take or change its import state.
            if (state.value("state", "idle") == "idle") state["message"] = e.what();
        }
    }
    void pairingMessage(const std::string& message) {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!stopping.load()) { state["state"] = "starting"; state["message"] = message; }
    }
    void pairPhone(const Phone& phone) {
        const bool havePair = executable(pairHelper), haveMux = executable(muxHelper);
        if (!havePair && !haveMux && !std::getenv("PTE_IPHONE_PAIR_HELPER") && !std::getenv("PTE_IPHONE_MUX_HELPER")) return;
        if (!havePair || !haveMux) throw std::runtime_error("iPhone pairing tools missing. Reinstall the complete Brick package.");
        RuntimeDirectory session;
        fs::path records = root / "private/iphone-pairing"; bool temporary = false;
        try { privateDirectory(root / "private"); privateDirectory(records); }
        catch (...) { records = session.path / "pair-records"; privateDirectory(records); temporary = true; }
        const auto socket = (session.path / "mux.sock").string();
        Child daemon(muxHelper, {"-f", "-p", "-C", records.string(), "-S", socket, "-P", (session.path / "mux.pid").string()});
        std::string daemonError;
        pairingMessage(temporary ? "Temporary pairing storage; access may be requested again. Unlock the iPhone." : "Opening private iPhone USB session; unlock the phone");
        const auto socketDeadline = Clock::now() + std::chrono::seconds(5);
        for (;;) {
            if (quit.load() || stopping.load()) throw Cancelled{};
            readAvailable(daemon.err, daemonError, 8192, true);
            std::string discarded; readAvailable(daemon.out, discarded, 8192, true);
            if (daemon.poll()) throw std::runtime_error("Private iPhone USB session could not start; reconnect and retry");
            struct stat info{};
            if (!lstat(socket.c_str(), &info) && S_ISSOCK(info.st_mode) && info.st_uid == geteuid()) break;
            if (Clock::now() >= socketDeadline) throw std::runtime_error("Private iPhone USB session timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Child pair(pairHelper, {"pair", "--udid", phone.id, "--socket", socket, "--timeout", "90"});
        JsonLines lines(ENUM_LIMIT); bool verified = false; std::string pairError, failure;
        const auto handle = [&](const std::string& line) {
            const auto value = Json::parse(line, nullptr, false);
            if (!value.is_object() || value.value("protocol_version", 0) != 1) throw std::runtime_error("Unsupported iPhone pairing response");
            const auto state = boundedText(value, "state");
            if (state != "connecting" && state != "pairing" && state != "trust" && state != "unlock" && state != "paired" && state != "error" && state != "cancelled")
                throw std::runtime_error("Invalid iPhone pairing state");
            if (state == "paired") verified = true;
            else if (state == "error" || state == "cancelled") failure = boundedText(value, "message", "iPhone pairing failed; unlock the phone and allow access if requested");
            pairingMessage(boundedText(value, "message", "Waiting for iPhone access; unlock and allow access if requested"));
        };
        const auto deadline = Clock::now() + std::chrono::seconds(95);
        for (;;) {
            if (quit.load() || stopping.load()) throw Cancelled{};
            lines.drain(pair.out, handle); readAvailable(pair.err, pairError, 8192, true);
            readAvailable(daemon.err, daemonError, 8192, true);
            std::string discarded; readAvailable(daemon.out, discarded, 8192, true);
            if (!failure.empty()) throw std::runtime_error(failure);
            if (pair.poll()) { lines.finish(pair.out, handle); break; }
            if (daemon.poll()) throw std::runtime_error("Private iPhone USB session disconnected during pairing");
            if (Clock::now() >= deadline) throw std::runtime_error("iPhone access timed out. Unlock it and allow access if requested, then retry.");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!failure.empty()) throw std::runtime_error(failure);
        if (!pair.success() || !verified) throw std::runtime_error("iPhone access was not verified; unlock and retry");
        {std::lock_guard<std::mutex> lock(stateMutex);state["phoneTrusted"]=true;}
        // Release only our daemon and its descendants before raw QVH claims USB.
        pair.stop(); daemon.stop();
        pairingMessage("iPhone access verified; connecting direct USB audio");
    }
    void event(const std::string& line, int id, int rate, int channels, const Phone& phone, uint64_t& frames, bool& ready, bool& startupPhase, Clock::time_point& audioDeadline, Clock::time_point began, std::string& error) {
        auto value = Json::parse(line, nullptr, false);
        if (!value.is_object() || value.value("protocol_version", 0) != 1) throw std::runtime_error("iPhone helper sent invalid status");
        const auto kind = boundedText(value, "state");
        const bool access = kind == "starting" || kind == "connecting" || kind == "pairing" || kind == "trust_required" || kind == "trust" || kind == "unlock";
        if (!access && kind != "ready_to_stream" && kind != "streaming" && kind != "recording" && kind != "stopping" && kind != "ready" && kind != "error") throw std::runtime_error("Unsupported iPhone capture state");
        if (!frames) {
            if (access) audioDeadline = began + std::chrono::seconds(120);
            else if (startupPhase) audioDeadline = Clock::now() + std::chrono::seconds(25);
        }
        startupPhase = access;
        uint64_t count = frames;
        if (value.contains("frames")) {
            if (!value["frames"].is_number_integer() || value["frames"].get<double>() < 0 || value["frames"].get<double>() > double(uint64_t(rate) * MAX_SECONDS)) throw std::runtime_error("Invalid iPhone audio frame count");
            count = value["frames"].get<uint64_t>(); if (count < frames) throw std::runtime_error("iPhone frame count moved backwards");
        }
        if (value.contains("rate") && (!value["rate"].is_number_integer() || value["rate"].get<int>() != rate)) throw std::runtime_error("iPhone helper changed the requested sample rate");
        if (value.contains("channels") && (!value["channels"].is_number_integer() || value["channels"].get<int>() != channels)) throw std::runtime_error("iPhone helper changed the requested channel count");
        const auto peak = [&](const char* key) {
            if (!value.contains(key)) return 0.0;
            if (!value[key].is_number()) throw std::runtime_error("Invalid iPhone input meter");
            const auto v = value[key].get<double>(); if (!std::isfinite(v)) throw std::runtime_error("Non-finite iPhone input meter"); return std::clamp(v, 0.0, 1.0);
        };
        const auto left = peak("peakL"), right = peak("peakR"); frames = count;
        if (kind == "ready") { if (!count) throw std::runtime_error("iPhone reported a completed take without audio frames"); ready = true; }
        if (kind == "error") error = boundedText(value, "message", "iPhone audio failed; reconnect and unlock the phone");
        std::lock_guard<std::mutex> lock(stateMutex);
        // READY is published only after process exit and an independent WAV check.
        state["state"] = stopping.load() || kind == "ready" || kind == "stopping" ? "stopping" : count > 0 ? "recording" : "starting";
        state["message"] = boundedText(value, "message", count ? "Receiving iPhone audio; software monitoring off" : "Waiting for iPhone audio; unlock and allow access if requested");
        state["frames"] = frames; state["rate"] = rate; state["channels"] = channels;
        state["requestedId"] = id; state["routedId"] = count ? phone.inputId : 0;
        state["routedName"] = count ? phone.name + " (direct USB, experimental)" : "";
        state["peakL"] = count ? left : 0.0; state["peakR"] = count ? right : 0.0;
    }
    void record(int id, int rate, int channels, const std::string& serial) {
        std::string path, failure; bool finalReady = false, success = false; uint64_t reportedFrames = 0;
        Phone selected;
        try {
            if (quit.load() || stopping.load()) throw Cancelled{};
            auto phones = listPhones(helper, [this] { return quit.load() || stopping.load(); });
            if (id == IPHONE_CAPTURE_ACTION) {
                if (phones.empty()) throw std::runtime_error("No iPhone detected. Connect to USB HOST, unlock and refresh.");
                if (phones.size() != 1) { setPhones(phones); throw std::runtime_error("Multiple iPhones detected. Refresh and select the specific phone."); }
                selected = phones.front();
            } else {
                const auto found = std::find_if(phones.begin(), phones.end(), [&](const Phone& p) { return p.id == serial; });
                if (found == phones.end()) { setPhones(phones); throw std::runtime_error("Selected iPhone disconnected. Refresh and select the correct phone."); }
                selected = *found;
            }
            setPhones(phones);
            { std::lock_guard<std::mutex> lock(stateMutex); selected.inputId = identities.at(selected.id);state["phoneDetected"]=true; }
            pairPhone(selected);
            if (quit.load() || stopping.load()) throw Cancelled{};
            path = reservePath(directory);
            Child child(helper, {"record", "--udid", selected.id, "--output", path, "--rate", std::to_string(rate), "--channels", std::to_string(channels), "--max-seconds", "600"}, 4000);
            JsonLines lines(RECORD_JSON_LIMIT); std::string stderrText; bool startupPhase = false;
            const auto began = Clock::now(); auto lastData = began, lastSpace = began, audioDeadline = began + std::chrono::seconds(25); uint64_t previousFrames = 0;
            const auto handle = [&](const std::string& line) {
                event(line, id, rate, channels, selected, reportedFrames, finalReady, startupPhase, audioDeadline, began, failure);
            };
            while (true) {
                const auto now = Clock::now();
                if (quit.load() || stopping.load()) { child.stop(); break; }
                lines.drain(child.out, handle); readAvailable(child.err, stderrText, 8192, true);
                if (reportedFrames != previousFrames) { previousFrames = reportedFrames; lastData = now; }
                if (child.poll()) { lines.finish(child.out, handle); break; }
                std::error_code ec; const auto bytes = fs::file_size(path, ec);
                if (ec || bytes > MAX_WAV_BYTES) throw std::runtime_error("iPhone recording exceeded its file-size limit");
                if (!failure.empty()) { child.stop(); break; }
                if (now - began > std::chrono::seconds(MAX_SECONDS + 10)) throw std::runtime_error("iPhone recording time limit reached");
                if ((!reportedFrames && now > audioDeadline) || (reportedFrames && now - lastData > std::chrono::seconds(10))) throw std::runtime_error("iPhone delivered no audio frames. Unlock it and try a different audio app.");
                if (now - lastSpace >= std::chrono::seconds(1)) {
                    lastSpace = now; const auto space = fs::space(directory, ec);
                    if (ec || space.available < DISK_RESERVE) throw std::runtime_error("Low SD-card space; iPhone recording stopped");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            // A graceful SIGTERM may flush a final ready snapshot after stop().
            lines.finish(child.out, handle);
            readAvailable(child.err, stderrText, 8192, true);
            success = child.success();
            if (!success && failure.empty() && !stopping.load() && !quit.load()) failure = "iPhone helper stopped unexpectedly; reconnect and unlock the phone";
        } catch (const Cancelled&) {}
        catch (const std::exception& e) { failure = e.what(); }
        catch (...) { failure = "iPhone USB capture failed"; }
        Wave wave; bool valid = false;
        if (!path.empty()) {
            try { wave = validateWave(path, rate, channels); valid = true; }
            catch (const std::exception& e) { if (failure.empty() && !stopping.load() && !quit.load()) failure = e.what(); }
            if (!valid) { std::error_code ec; fs::remove(path, ec); }
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            const bool cancelledEmpty = !valid && (stopping.load() || quit.load());
            if (valid && success && finalReady && reportedFrames != wave.frames && failure.empty()) failure = "iPhone WAV frame count does not match its completed status";
            const bool completed = valid && success && finalReady && failure.empty();
            state["state"] = cancelledEmpty ? "idle" : completed ? "ready" : "error";
            state["message"] = cancelledEmpty ? "iPhone recording cancelled; no audio captured" : !failure.empty() ? failure : completed ? "iPhone recording ready; import the WAV to edit or slice" : valid ? "Partial iPhone take recovered; import the valid WAV to edit or slice" : "No completed iPhone audio was received";
            state["frames"] = valid ? wave.frames : 0; state["path"] = valid ? path : "";
            state["rate"] = rate; state["channels"] = channels; state["requestedId"] = id;
            state["routedId"] = valid ? selected.inputId : 0; state["routedName"] = valid ? selected.name + " (direct USB, experimental)" : "";
            state["peakL"] = state["peakR"] = 0.0;
            running = false;
        }
    }
    void loop() {
        while (!quit.load()) {
            int id = 0, rate = 0, channels = 0; std::string serial; bool take = false;
            {
                std::unique_lock<std::mutex> lock(commandMutex);
                wake.wait_for(lock, std::chrono::milliseconds(100), [&] { return quit.load() || pending || scanRequested.load(); });
                if (pending) { take = true; id = pendingId; rate = pendingRate; channels = pendingChannels; serial = pendingSerial; pending = false; }
            }
            if (quit.load()) break;
            if (take) record(id, rate, channels, serial);
            else if (scanRequested.exchange(false)) scan();
        }
        running = false;
    }
};

IphoneSampleRecorder::IphoneSampleRecorder(std::string root): impl_(std::make_unique<Impl>(root)) {}
IphoneSampleRecorder::~IphoneSampleRecorder() = default;
std::string IphoneSampleRecorder::inputs_json() {
    auto& p = *impl_; Json rows = Json::array();
    {
        std::lock_guard<std::mutex> lock(p.stateMutex);
        for (const auto& phone : p.current)
            rows.push_back({{"id", phone.inputId}, {"name", phone.name + " USB (experimental) [" + phone.id.substr(phone.id.size() > 6 ? phone.id.size() - 6 : 0) + "]"}, {"kind", "iphone"}, {"connected", true}, {"detail", p.discoveryDetail}, {"channels", {1, 2}}, {"rates", {44100, 48000}}});
        if (rows.empty()) rows.push_back({{"id", IPHONE_CAPTURE_ACTION}, {"name", "iPhone / iPad USB (experimental)"}, {"kind", "iphone-action"}, {"connected", false}, {"detail", p.discoveryDetail}, {"channels", {1, 2}}, {"rates", {44100, 48000}}});
        if (!p.running.load() && Clock::now() >= p.nextScan) { p.nextScan = Clock::now() + std::chrono::seconds(2); p.scanRequested = true; }
    }
    p.wake.notify_one(); return rows.dump();
}
bool IphoneSampleRecorder::start(int id, int channels, int rate) {
    auto& p = *impl_; std::lock_guard<std::mutex> command(p.commandMutex);
    if (p.running.load()) return false;
    if (id < IPHONE_CAPTURE_ACTION || (channels != 1 && channels != 2) || (rate != 44100 && rate != 48000)) { p.fail("Choose mono/stereo and 44.1 or 48 kHz", id); return false; }
    if (!executable(p.helper)) { p.fail("iPhone USB helper missing. Reinstall the complete Brick package.", id); return false; }
    std::string serial;
    {
        std::lock_guard<std::mutex> lock(p.stateMutex);
        if (id != IPHONE_CAPTURE_ACTION) {
            for (const auto& item : p.identities) if (item.second == id) { serial = item.first; break; }
            if (serial.empty()) { p.state = idleState(); p.state["state"] = "error"; p.state["message"] = "Unknown iPhone input. Refresh and select the correct device."; return false; }
        }
        p.state = idleState(); p.state["state"] = "starting"; p.state["message"] = "Connecting to the selected iPhone; unlock and allow access if requested";
        p.state["requestedId"] = id; p.state["rate"] = rate; p.state["channels"] = channels;
        p.running = true; p.stopping = false;
    }
    p.pendingId = id; p.pendingRate = rate; p.pendingChannels = channels; p.pendingSerial = serial; p.pending = true;
    p.wake.notify_one(); return true;
}
void IphoneSampleRecorder::stop() {
    auto& p = *impl_; if (!p.running.load()) return; p.stopping = true;
    std::lock_guard<std::mutex> lock(p.stateMutex);
    if (p.state.value("state", "idle") == "starting" || p.state.value("state", "idle") == "recording") { p.state["state"] = "stopping"; p.state["message"] = "Finishing iPhone recording"; }
    p.wake.notify_one();
}
std::string IphoneSampleRecorder::state_json() { std::lock_guard<std::mutex> lock(impl_->stateMutex); return impl_->state.dump(); }
}
