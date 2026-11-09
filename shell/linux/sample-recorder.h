#pragma once

#include "ui/audio_capture.h"

#include <memory>
#include <string>

namespace ptshell {

// SDL capture for the Linux/PortMaster shell. SDL_INIT_AUDIO must outlive this
// object. All device opening, PCM reads and disk writes run on its worker.
// There is deliberately no path from this input to the audio output.
class SdlSampleRecorder final : public pt::ui::CaptureBackend {
public:
    explicit SdlSampleRecorder(std::string appRoot);
    ~SdlSampleRecorder() override;
    SdlSampleRecorder(const SdlSampleRecorder&) = delete;
    SdlSampleRecorder& operator=(const SdlSampleRecorder&) = delete;

    std::string inputs_json() override;
    bool start(int deviceId, int channels, int rate) override;
    void stop() override;
    std::string state_json() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ptshell
