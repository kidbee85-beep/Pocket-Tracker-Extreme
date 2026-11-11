#pragma once

#include <cstdint>

namespace pt::ui {
class Canvas;
struct AppState;

struct StudioWorkspaceState {
    bool open = false;
    int selected = 0;
    uint16_t held = 0;
};

bool draw_studio_workspace(Canvas&, const AppState&);
}  // namespace pt::ui
