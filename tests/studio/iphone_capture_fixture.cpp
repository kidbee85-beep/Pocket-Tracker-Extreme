// Standalone external helper fixture. It deliberately misbehaves across real
// pipes/processes/files so adapter tests exercise its trust boundary end to end.
#include "vendor/nlohmann/json.hpp"
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
volatile sig_atomic_t stopped = 0;
void onSignal(int) { stopped = 1; }
void pauseMs(int milliseconds) {
    for (int i = 0; i < milliseconds / 5 && !stopped; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
}
void emit(Json value) { value["protocol_version"] = 1; std::cout << value.dump() << std::endl; }
void log(const fs::path& folder, Json value) {
    value["pid"] = getpid(); const auto line = value.dump() + "\n";
    const int fd = open((folder / "calls.jsonl").c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd >= 0) { (void)write(fd, line.data(), line.size()); close(fd); }
}
void word(std::ostream& out, uint64_t value, int count) { for (int i = 0; i < count; ++i) out.put(char(value >> (8 * i))); }
void wave(const fs::path& file, int rate, int channels, int frames = 128) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    const auto size = frames * channels * 2;
    out << "RIFF"; word(out, size + 36, 4); out << "WAVEfmt "; word(out, 16, 4);
    word(out, 1, 2); word(out, channels, 2); word(out, rate, 4); word(out, rate * channels * 2, 4);
    word(out, channels * 2, 2); word(out, 16, 2); out << "data"; word(out, size, 4);
    for (int i = 0; i < frames * channels; ++i) word(out, (i % 2 ? 4096 : -4096) & 0xffff, 2);
}
std::string option(int argc, char** argv, const std::string& key) {
    for (int i = 1; i + 1 < argc; ++i) if (argv[i] == key) return argv[i + 1];
    return {};
}
}
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::signal(SIGTERM, onSignal); std::signal(SIGINT, onSignal);
    const fs::path folder = fs::path(argv[0]).parent_path();
    Json config; { std::ifstream in(folder / "fixture.json"); in >> config; }
    const auto name = fs::path(argv[0]).filename().string();
    if (name == "pte-usbmuxd") {
        const auto socket = option(argc, argv, "-S"), pair = option(argc, argv, "-C");
        struct stat info{}; if (lstat(pair.c_str(), &info) || (info.st_mode & 0777) != 0700) return 3;
        if (lstat(fs::path(socket).parent_path().c_str(), &info) || (info.st_mode & 0777) != 0700) return 4;
        log(folder, {{"call", "mux"}, {"socket", socket}, {"pairdir", pair}});
        if (config.value("mux", "") == "fail") return 2;
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0); sockaddr_un address{}; address.sun_family = AF_UNIX;
        if (socket.size() >= sizeof(address.sun_path)) return 5;
        std::memcpy(address.sun_path, socket.c_str(), socket.size() + 1);
        if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(fd, 1)) { perror("fixture socket"); return 6; }
        while (!stopped) pauseMs(10);
        close(fd); unlink(socket.c_str()); std::ofstream(folder / "mux-stopped") << "done";
        return 0;
    }
    if (name == "pte-iphone-pair") {
        const auto socket = option(argc, argv, "--socket");
        log(folder, {{"call", "pair"}, {"udid", option(argc, argv, "--udid")}, {"socket", socket}});
        emit({{"state", "trust"}, {"message", "Unlock and allow access to this test phone"}});
        if (config.value("pair", "") == "wait") { while (!stopped) pauseMs(10); return 1; }
        if (config.value("pair", "") == "reject") { emit({{"state", "error"}, {"message", "Test phone rejected access"}}); return 2; }
        if (config.value("pair", "") == "malformed") { std::cout << "not-json" << std::endl; return 0; }
        if (config.value("pair", "") == "silent") return 0;
        pauseMs(40); if (stopped) return 1;
        std::ofstream(folder / "paired") << "verified";
        emit({{"state", "paired"}, {"message", "Access verified"}}); return 0;
    }
    if (std::string(argv[1]) == "list") {
        if (config.value("list", "") == "hang") { while (!stopped) pauseMs(10); return 1; }
        if (config.value("list", "") == "malformed") { std::cout << "[]" << std::endl; return 0; }
        emit({{"devices", config.value("devices", Json::array())}}); return 0;
    }
    if (std::string(argv[1]) != "record") return 2;
    const auto path = option(argc, argv, "--output"), serial = option(argc, argv, "--udid");
    const auto rate = std::stoi(option(argc, argv, "--rate")), channels = std::stoi(option(argc, argv, "--channels"));
    log(folder, {{"call", "record"}, {"udid", serial}, {"path", path}, {"rate", rate}, {"channels", channels}});
    const auto mode = config.value("record", "good");
    if (config.value("require_pair", false) && (!fs::exists(folder / "paired") || !fs::exists(folder / "mux-stopped"))) return 7;
    const auto status = [&](const std::string& state, int frames, const std::string& message = "fixture") {
        emit({{"state", state}, {"frames", frames}, {"rate", rate}, {"channels", channels},
              {"peakL", 0.25}, {"peakR", 0.5}, {"path", path}, {"message", message}});
    };
    if (mode == "malformed") { std::cout << "{broken" << std::endl; return 0; }
    if (mode == "longline") { std::cout << std::string(70000, 'x') << std::endl; return 0; }
    if (mode == "oversize") {
        truncate(path.c_str(), 44 + 600LL * 48000 * 4 + 1024 * 1024 + 1); status("starting", 0); pauseMs(300); return 0;
    }
    if (mode == "flood") {
        Json line = {{"protocol_version", 1}, {"state", "starting"}, {"frames", 0}, {"message", std::string(8192, 'x')}};
        const auto raw = line.dump() + "\n";
        for (int i = 0; i < 1400; ++i) std::cout << raw << std::flush;
        return 0;
    }
    if (mode == "ignore") {
        const auto child = fork();
        if (child == 0) {
            std::signal(SIGTERM, SIG_IGN);
            for (;;) { std::ofstream(folder / "heartbeat") << std::chrono::steady_clock::now().time_since_epoch().count(); usleep(10000); }
        }
        std::signal(SIGTERM, SIG_IGN); status("starting", 0);
        for (;;) usleep(10000);
    }
    status("starting", 0);
    if (mode == "timeout") { pauseMs(500); status("ready_to_stream", 0); }
    if (mode == "zero") return 0;
    if (mode == "hold" || mode == "timeout") { while (!stopped) pauseMs(10); return 0; }
    pauseMs(70); if (stopped) return 0;
    if (mode == "symlink") { unlink(path.c_str()); fs::create_symlink(folder / "outside", path); status("ready", 128); return 0; }
    if (mode == "corrupt") { std::ofstream(path) << "not a completed audio wave"; status("ready", 128); return 0; }
    wave(path, mode == "wrongformat" ? 22050 : rate, channels);
    if (mode == "lie") { status("ready", 0); return 0; }
    status("recording", 128); pauseMs(50);
    if (mode == "backwards") { status("recording", 64); return 0; }
    if (mode == "partial_error") { status("error", 128, "Test iPhone disconnected"); return 2; }
    if (mode == "partial_cancel") { while (!stopped) pauseMs(10); }
    status("ready", mode == "mismatch" ? 127 : 128);
    return 0;
}
