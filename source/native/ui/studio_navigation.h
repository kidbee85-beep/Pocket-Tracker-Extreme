#pragma once
#include <array>

namespace pt::ui {
struct StudioTabDescriptor {
    int id;
    const char* label;
};

// IDs remain stable. Appearance (legacy ID6) belongs to app Settings, not the
// ordered musical workspace. Render and input must use this same descriptor.
inline constexpr std::array<StudioTabDescriptor, 7> kStudioMusicTabs{{
    {0, "Synth"}, {1, "FX"}, {2, "Master"}, {3, "Punch"},
    {4, "Record"}, {5, "Drumlab"}, {7, "Sources"}
}};

inline bool studio_is_music_tab(int id) {
    for (const auto& tab : kStudioMusicTabs) if (tab.id == id) return true;
    return false;
}

inline int studio_next_music_tab(int id, int direction) {
    for (std::size_t i = 0; i < kStudioMusicTabs.size(); ++i) {
        if (kStudioMusicTabs[i].id == id) {
            const int count = static_cast<int>(kStudioMusicTabs.size());
            return kStudioMusicTabs[(static_cast<int>(i) + count + (direction > 0 ? 1 : -1)) % count].id;
        }
    }
    return kStudioMusicTabs.front().id;
}
} // namespace pt::ui
