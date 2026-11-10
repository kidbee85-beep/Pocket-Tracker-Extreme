#pragma once
#include <memory>
#include <string>
namespace pt::ui {
struct LiveResampleSave;
struct LiveResampleView {
    bool active = false, saving = false, failed = false, recoveryChecked = false;
    int frames = 0, rate = 48000, limit = 0, lengthIndex = 0;
    std::string path, importedPath, message;
    std::shared_ptr<LiveResampleSave> job;
};
}
