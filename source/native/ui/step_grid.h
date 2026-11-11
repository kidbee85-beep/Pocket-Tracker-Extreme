#pragma once

#include <algorithm>
#include <initializer_list>

#include "ui/helpers.h"

namespace pt::ui {

// Draw the optional guides behind the cursor, selection, playhead and text.
inline void draw_step_grid(Canvas& canvas, const Theme& theme, StepGridStyle style, int row,
                           int x, int y, int width, int height,
                           std::initializer_list<int> dividers) {
    if (style == StepGridStyle::OFF) return;

    const bool dotted = style == StepGridStyle::DOTS;
    if (row % 4 == 0) {
        if (dotted) {
            const Argb ink = with_alpha(theme.textParam, .46f);
            for (int offset = 0; offset < width; offset += 12)
                canvas.fill_rect(x + offset, y, std::min(4, width - offset), 2, ink);
        } else {
            canvas.fill_rect(x, y, width, 2, with_alpha(theme.textParam, .30f));
        }
    }
    if (style == StepGridStyle::BEATS) return;

    if (dotted) {
        const Argb ink = with_alpha(theme.textParam, .34f);
        for (int divider : dividers)
            for (int offset = 4; offset + 2 <= height; offset += 7)
                canvas.fill_rect(divider, y + offset, 2, 2, ink);
    } else {
        const Argb ink = with_alpha(theme.textParam, .18f);
        for (int divider : dividers) canvas.fill_rect(divider, y, 2, height, ink);
    }
}

}  // namespace pt::ui
