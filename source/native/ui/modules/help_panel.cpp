#include "ui/modules/help_panel.h"

#include "ui/helpers.h"

#include <array>
#include <string>

namespace pt::ui {

void HelpPanelModule::draw(Canvas& c, HelpTopic topic, const Theme& t) const {
    const auto& entry = help_entry(topic);
    std::array<const char*, 5> actions{};
    int count = 0;
    for (const char* key : entry.keys) {
        if (!key || !*key) continue;
        actions[count++] = key;
        if (count == static_cast<int>(actions.size())) break;
    }
    if (!count) {
        if (*entry.line2) actions[count++] = entry.line2;
        if (*entry.line3) actions[count++] = entry.line3;
    }

    const auto titleLength = help_title_length(entry.line1);
    const std::string title(entry.line1, titleLength);
    const char* description = entry.line1 + titleLength;
    if (*description == ':') ++description;
    while (*description == ' ') ++description;

    constexpr int width = 548, rowHeight = 28, inset = 21;
    const int x = (DESIGN_W - width) / 2;
    const int height = 124 + count * rowHeight;
    const int y = (DESIGN_H - height) / 2;
    const int left = x + inset;
    const int inner = width - inset * 2;
    c.reset_clip();
    draw_modal_backdrop(c);
    draw_modal_box(c, x, y, width, height, t);
    c.draw_label("QUICK HELP", left, y + 14, t.rowCursor, 12);
    c.draw_label(Canvas::clip_label(title, inner, 22), left, y + 33, t.textValue, 22);
    if (*description) c.draw_label(Canvas::clip_label(description, inner, 15), left, y + 62, t.textParam, 15);
    c.fill_rect(left, y + 85, inner, 1, with_alpha(t.rowCursor, .52f));
    for (int i = 0; i < count; ++i) {
        const int row = y + 92 + i * rowHeight;
        c.panel_rect(left, row, inner, rowHeight - 4, t.rowEvery4th, with_alpha(t.textParam, .18f), 3);
        c.draw_label(Canvas::clip_label(actions[i], inner - 22, 16), left + 11, row + 4, t.textValue, 16);
    }
    const int footer = y + height - 24;
    c.draw_label("A  Details", left, footer, t.rowCursor, 15);
    constexpr const char* back = "B / SELECT  Back";
    c.draw_label(back, x + width - inset - Canvas::label_width(back, 15), footer, t.textParam, 15);
}

} // namespace pt::ui
