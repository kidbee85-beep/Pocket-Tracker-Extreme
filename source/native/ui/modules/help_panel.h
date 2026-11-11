#pragma once

#include "ui/canvas.h"
#include "ui/help_text.h"
#include "ui/theme.h"

namespace pt::ui {

class HelpPanelModule {
  public:
    void draw(Canvas& c, HelpTopic topic, const Theme& t) const;
};

}  // namespace pt::ui
