#pragma once

// A 640×480 software framebuffer and the primitives the whole UI is drawn with: filled and stroked
// rects, bitmap text, a clip. The shell scales the finished frame onto the display.

#include <cstdint>
#include <string>
#include <vector>

#include "font5x5.h"
#include "theme.h"

namespace pt::ui {

inline constexpr int DESIGN_W = 640;
inline constexpr int DESIGN_H = 480;

// The modal dim; the shell blends the same value over the letterbox bars.
inline constexpr Argb MODAL_BACKDROP = 0xCC000000;

class Canvas {
public:
    Canvas() : px_(static_cast<size_t>(DESIGN_W) * DESIGN_H, 0xFF000000) { reset_clip(); }

    // ── The frame ────────────────────────────────────────────────────────────────────────────────

    // The whole canvas, clip ignored.
    void clear(Argb color = 0xFF000000);

    // ARGB, row-major — what the shell uploads.
    const uint32_t* pixels() const { return px_.data(); }
    int             pitch_bytes() const { return DESIGN_W * 4; }

    // ── Primitives ───────────────────────────────────────────────────────────────────────────────

    // Blends src-over when `color` has alpha < 255.
    void fill_rect(int x, int y, int w, int h, Argb color);

    // Quiet, static grain and a shallow light falloff. No animation or random state.
    void set_surface_style(int style);
    void surface_rect(int x, int y, int w, int h, Argb base);

    // Rounded instrument-style panel, with an inside edge and a fine top highlight.
    void panel_rect(int x, int y, int w, int h, Argb base, Argb border, int radius = 5);

    // An outline inside the bounds.
    void stroke_rect(int x, int y, int w, int h, Argb color, int thickness = 1);

    // A one-pixel line, including its endpoints; clipped before rasterization.
    void line(int x0, int y0, int x1, int y1, Argb color);

    // `font_scale` multiplies the 5×5 cell (3 → the standard 15×15 text).
    void draw_glyph(const Glyph& g, int x, int y, Argb color, int font_scale);

    // UTF-8; each code point advances `5 * font_scale + spacing`.
    void draw_text(const std::string& text, int x, int y, Argb color, int spacing, int font_scale);

    // Proportional interface text. Sizes 12/18 use 14/21-pixel ink atlases;
    // intermediate sizes select the nearest atlas. Grid text keeps draw_text.
    void draw_label(const std::string& text, int x, int y, Argb color, int size = 12);
    static int label_width(const std::string& text, int size = 12);
    static std::string clip_label(const std::string& text, int maxWidth, int size = 12);

    // No trailing gap.
    static int text_width(const std::string& text, int spacing, int font_scale);

    static int glyph_count(const std::string& text);   // code points

    // At most `max_glyphs` columns, a cut marked with `…` inside the budget. Never splits a code point.
    static std::string clip_text(const std::string& text, int max_glyphs);

    // The same, keeping the end — for paths.
    static std::string clip_text_head(const std::string& text, int max_glyphs);

    // ── Clipping ─────────────────────────────────────────────────────────────────────────────────

    void set_clip(int x, int y, int w, int h);
    void reset_clip();

    // So an early `return` inside a module cannot leak a clip into the next one.
    class ClipScope {
    public:
        ClipScope(Canvas& c, int x, int y, int w, int h)
            : c_(c), cx_(c.clipX_), cy_(c.clipY_), cw_(c.clipW_), ch_(c.clipH_) {
            c.set_clip(x, y, w, h);
        }
        ~ClipScope() { c_.set_clip(cx_, cy_, cw_, ch_); }
        ClipScope(const ClipScope&)            = delete;
        ClipScope& operator=(const ClipScope&) = delete;

    private:
        Canvas& c_;
        int     cx_, cy_, cw_, ch_;
    };

private:
    std::vector<uint32_t> px_;
    std::vector<uint32_t> surfacePixels_;
    Argb                 surfaceColor_ = 0;
    int                  surfaceStyle_ = 0;
    int                  cachedSurfaceStyle_ = -1;
    int                   clipX_ = 0, clipY_ = 0, clipW_ = DESIGN_W, clipH_ = DESIGN_H;
};

}  // namespace pt::ui
