#pragma once
#include "ui/audio_capture.h"
#include <memory>
#include <string>

namespace ptshell {
// Preserves the existing microphone/USB-interface path and adds the explicitly
// experimental Apple direct-USB path. Only one backend can record at a time.
class CaptureRouter final : public pt::ui::CaptureBackend {
public:
    explicit CaptureRouter(std::string appRoot);
    ~CaptureRouter() override;
    std::string inputs_json() override;
    bool start(int deviceId, int channels, int rate) override;
    void stop() override;
    std::string state_json() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
