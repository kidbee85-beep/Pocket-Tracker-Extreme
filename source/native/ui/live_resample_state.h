#pragma once
#include <memory>
#include <string>
namespace pt::ui {
struct LiveResampleSave;
struct LiveResampleView {
    bool active = false, saving = false, failed = false, recoveryChecked = false;
    bool ownsSynthRoot = false;
    int frames = 0, rate = 48000, limit = 0, lengthIndex = 0;
    std::string path, importedPath, message;
    // A zero-frame attempt must not displace the previous completed take in the UI.
    std::string previousPath, previousImportedPath;
    int previousFrames = 0, previousRate = 48000, previousLimit = 0;
    std::shared_ptr<LiveResampleSave> job;
};
}
