#include "linux/capture-router.h"
#include "linux/sample-recorder.h"
#include "linux/iphone-recorder.h"
#include "vendor/nlohmann/json.hpp"
#include <mutex>

namespace ptshell {
using Json = nlohmann::json;
struct CaptureRouter::Impl {
    explicit Impl(const std::string& root): sdl(root), iphone(root), selected(&sdl) {}
    SdlSampleRecorder sdl;
    IphoneSampleRecorder iphone;
    pt::ui::CaptureBackend* selected;
    std::mutex lifecycle;
};
CaptureRouter::CaptureRouter(std::string root): impl_(std::make_unique<Impl>(root)) {}
CaptureRouter::~CaptureRouter() = default;
std::string CaptureRouter::inputs_json() {
    Json result = Json::array();
    for (auto* backend : {static_cast<pt::ui::CaptureBackend*>(&impl_->sdl),
                          static_cast<pt::ui::CaptureBackend*>(&impl_->iphone)}) {
        try {
            auto rows = Json::parse(backend->inputs_json());
            if (rows.is_array()) for (auto& row : rows) result.push_back(std::move(row));
        } catch (...) {}
    }
    return result.dump();
}
bool CaptureRouter::start(int id, int channels, int rate) {
    std::lock_guard<std::mutex> lock(impl_->lifecycle);
    try {
        const auto state = Json::parse(impl_->selected->state_json()).value("state", "idle");
        if (state == "starting" || state == "recording" || state == "stopping") return false;
    } catch (...) { return false; }
    impl_->selected = id >= IPHONE_CAPTURE_ACTION ? static_cast<pt::ui::CaptureBackend*>(&impl_->iphone)
                                                : static_cast<pt::ui::CaptureBackend*>(&impl_->sdl);
    return impl_->selected->start(id, channels, rate);
}
void CaptureRouter::stop() {
    std::lock_guard<std::mutex> lock(impl_->lifecycle); impl_->selected->stop();
}
std::string CaptureRouter::state_json() {
    std::lock_guard<std::mutex> lock(impl_->lifecycle); return impl_->selected->state_json();
}
}
