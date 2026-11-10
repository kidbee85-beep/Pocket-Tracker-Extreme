#pragma once

#include "ui/source_sampler.h"
#include <memory>
#include <string>

namespace pt::platform {

// HTTP, decoding and disk access run on a worker. The audio hook only consumes
// a preallocated SPSC ring; it never opens a second ALSA device.
class SourceSampler final : public ui::SourceBackend {
public:
    explicit SourceSampler(const std::string& appRoot);
    ~SourceSampler() override;
    SourceSampler(const SourceSampler&) = delete;
    SourceSampler& operator=(const SourceSampler&) = delete;
    bool command(const std::string& action, const std::string& argsJson) override;
    std::string state_json() override;
    bool take_query_request() override;
    void render_audio(float* stereo, int frames, int sampleRate);
    static void audio_hook(void* source, float* stereo, int frames, int sampleRate);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace pt::platform
