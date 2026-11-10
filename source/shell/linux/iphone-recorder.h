#pragma once
#include "ui/audio_capture.h"
#include <memory>
#include <string>

namespace ptshell {
// Separate namespace from SDL device IDs; this action is not a connected phone.
inline constexpr int IPHONE_CAPTURE_ACTION = 1000000;

// A bounded process adapter for the audio-only Apple USB helper. Discovery and
// capture run off the UI thread. The helper owns USB and finalizes its own WAV.
class IphoneSampleRecorder final : public pt::ui::CaptureBackend {
public:
    explicit IphoneSampleRecorder(std::string appRoot);
    ~IphoneSampleRecorder() override;
    IphoneSampleRecorder(const IphoneSampleRecorder&) = delete;
    IphoneSampleRecorder& operator=(const IphoneSampleRecorder&) = delete;
    std::string inputs_json() override;
    bool start(int deviceId, int channels, int rate) override;
    void stop() override;
    std::string state_json() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
