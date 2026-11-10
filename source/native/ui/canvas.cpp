#include "canvas.h"
#include "font_hd.h"
#include "font_ui.h"
#include "demo_font_style.h"
#include "demo_fonts.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pt::ui {

namespace {

// Src-over, 8-bit integer.
inline uint32_t blend(uint32_t dst, uint32_t src) {
    const uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0xFF) return src;
    if (sa == 0) return dst;
    const uint32_t ia = 255 - sa;
    const uint32_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    const uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    // Rounded, not truncated.
    const uint32_t r = (sr * sa + dr * ia + 127) / 255;
    const uint32_t g = (sg * sa + dg * ia + 127) / 255;
    const uint32_t b = (sb * sa + db * ia + 127) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

inline Argb shade(Argb color, int delta) {
    const auto ch = [&](int shift) {
        return static_cast<Argb>(std::clamp(static_cast<int>((color >> shift) & 255) + delta, 0, 255));
    };
    return (color & 0xFF000000u) | ch(16) << 16 | ch(8) << 8 | ch(0);
}

// Deterministic blue-noise-like grain, small enough to leave text and data quiet.
inline int grain(int x, int y) {
    uint32_t seed = static_cast<uint32_t>(x) * 0x1f123bb5u ^ static_cast<uint32_t>(y) * 0x5f356495u;
    seed ^= seed >> 15; seed *= 0x2c1b3c6du; seed ^= seed >> 12;
    return static_cast<int>(seed % 3) - 1;
}

int material_delta(int style, int x, int y) {
    switch (style) {
        case 1: // Brushed aluminium: fine horizontal tool marks.
            return (y % 4 == 0 ? -2 : (y % 4 == 1 ? 1 : 0)) + grain(x / 5, y);
        case 2: // Interleaved bundles, alternating their thread direction.
            return ((x / 4 + y / 4) & 1) ? (x % 4 < 2 ? -2 : 1) : (y % 4 < 2 ? 2 : -1);
        case 3: { // Paper grain with occasional short, quiet fibres.
            const int fibre = y % 5 == 0 && (x + (y / 5) * 17) % 47 < 15 ? -1 : 0;
            return grain(x, y) * 2 + fibre;
        }
        case 4: { // Sparse right-angle routes and square solder pads.
            const int xx = x % 64, yy = y % 64;
            if ((x / 64 + 2 * (y / 64)) % 3 != 0) return 0;
            const bool route = (yy == 18 && xx >= 10 && xx <= 46) ||
                               (xx == 46 && yy >= 18 && yy <= 49);
            const bool pad = (xx >= 8 && xx <= 11 && yy >= 17 && yy <= 20) ||
                             (xx >= 45 && xx <= 48 && yy >= 48 && yy <= 51);
            return route || pad ? 3 : 0;
        }
        case 5: // Precision drafting grid; major rules remain only a few RGB steps.
            return x % 64 == 0 || y % 64 == 0 ? 2 : (x % 16 == 0 || y % 16 == 0 ? 1 : 0);
        case 6: { // Broad diagonal silk folds, entirely integer and deterministic.
            const int phase = (x + 2 * y) % 128;
            const int distance = std::abs(phase - 64);
            return distance * distance * 4 / 4096 - 2;
        }
        case 7: return 0;
        case 8: // Ceramic glaze: quiet, broad relief over microscopic pores.
            return grain(x / 2,y / 2) + ((x / 120 + y / 90) % 3 == 0 ? 1 : 0);
        case 9: { // Punched metal, staggered ventilation perforations.
            const int xx=(x+(y/8%2)*4)%8,yy=y%8;
            return (xx==3||xx==4)&&(yy==3||yy==4)?-3:(xx==3&&yy==5?1:0);
        }
        case 10: { // Very fine contour engraving; no animation or bitmap assets.
            const int xx=x%160-80,yy=y%120-60;
            const int ring=(xx*xx+yy*yy)/180;
            return ring%9==0?2:0;
        }
        case 11: // Anodized satin with a fine cross-grain.
            return (x%5==0?-1:0)+(y%7==0?1:0)+grain(x/7,y/3);
        case 12: { // Original printmaking-style crosshatch, with quiet open cells.
            const int diagonal=(x+y)%18,opposite=(x-y+DESIGN_H)%24;
            return (diagonal==0?3:0)+(opposite==0?2:0)+grain(x/4,y/4);
        }
        case 13: // CRT-era raster glass, static: no full-screen flashing or scrolling.
            return (y%4==0?-2:0)+(x%3==0?1:0)+(y%32==0?2:0);
        case 14: { // Perspective vector floor beneath a fine registration grid.
            if(y<210)return x%80==0&&y%8<2?2:0;
            const int depth=y-186,span=22+depth/3;
            const int perspective=(x-320)*96/std::max(1,depth);
            return perspective%64==0||y%span==0?4:0;
        }
        case 15: { // Isometric tile edges; a bespoke geometric game-era homage.
            const int a=(x+2*y)%96,b=(x-2*y+1024)%96;
            return a==0||b==0?3:((y%48==0&&((x/48+y/24)&1))?1:0);
        }
        case 16: { // Sparse 2px constellation pixels, repeatable without random state.
            const int xx=x%64,yy=y%48;const unsigned h=unsigned(x/64*73+y/48*151);
            return (xx==int(h%43)+8&&yy==int(h%29)+7)?5:
                   (xx%16==0&&yy%16==0?1:0);
        }
        case 17: { // Routed X junctions: our original Bunka circuit signature.
            const int xx=x%96,yy=y%72;
            const bool trace=(yy==18&&xx>=12&&xx<=70)||(xx==70&&yy>=18&&yy<=51);
            const bool junction=xx>=26&&xx<=38&&yy>=36&&yy<=48&&
                                (xx-26==yy-36||xx-26+yy-36==12);
            return trace||junction?4:((xx==12||xx==70)&&(yy==18||yy==51)?5:0);
        }
        default: return grain(x, y);
    }
}

// One UTF-8 code point at `i`, advancing past it. A malformed byte is consumed as U+FFFD (blank).
inline uint32_t next_codepoint(const std::string& s, size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    const size_t n = s.size();

    const auto cont = [&](size_t k) {
        return i + k < n && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
    };
    const auto bits = [&](size_t k) { return static_cast<uint32_t>(s[i + k]) & 0x3F; };

    if (b0 < 0x80) { i += 1; return b0; }
    if ((b0 & 0xE0) == 0xC0 && cont(1)) {
        const uint32_t cp = ((b0 & 0x1Fu) << 6) | bits(1);
        i += 2; return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        const uint32_t cp = ((b0 & 0x0Fu) << 12) | (bits(1) << 6) | bits(2);
        i += 3; return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        const uint32_t cp = ((b0 & 0x07u) << 18) | (bits(1) << 12) | (bits(2) << 6) | bits(3);
        i += 4; return cp;
    }
    i += 1;
    return 0xFFFD;
}

struct LabelFont {
    const detail::UiFontGlyph* glyphs;
    const uint8_t* pixels;
    int height, fallbackScale;
};

LabelFont label_font(int size) {
    return size >= 15 ? LabelFont{detail::uiFont18Glyphs, detail::uiFont18Pixels, detail::uiFont18Height, 3}
                      : LabelFont{detail::uiFont12Glyphs, detail::uiFont12Pixels, detail::uiFont12Height, 2};
}

int label_index(uint32_t cp) {
    if (cp >= 32 && cp < 127) return static_cast<int>(cp - 32);
    if (cp >= 160 && cp < 256) return static_cast<int>(95 + cp - 160);
    return cp == 0x2026 ? 191 : -1;
}

int label_advance(uint32_t cp, const LabelFont& font) {
    const int index = label_index(cp);
    return index >= 0 ? font.glyphs[index].advance : 5 * font.fallbackScale;
}

}  // namespace

void Canvas::clear(Argb color) {
    std::fill(px_.begin(), px_.end(), color);
}

void Canvas::set_clip(int x, int y, int w, int h) {
    clipX_ = std::max(0, x);
    clipY_ = std::max(0, y);
    clipW_ = std::max(0, std::min(x + w, DESIGN_W) - clipX_);
    clipH_ = std::max(0, std::min(y + h, DESIGN_H) - clipY_);
}

void Canvas::reset_clip() {
    clipX_ = 0;
    clipY_ = 0;
    clipW_ = DESIGN_W;
    clipH_ = DESIGN_H;
}

void Canvas::fill_rect(int x, int y, int w, int h, Argb color) {
    if (w <= 0 || h <= 0) return;

    const int x0 = std::max(x, clipX_);
    const int y0 = std::max(y, clipY_);
    const int x1 = std::min(x + w, clipX_ + clipW_);
    const int y1 = std::min(y + h, clipY_ + clipH_);
    if (x0 >= x1 || y0 >= y1) return;

    const bool opaque = ((color >> 24) & 0xFF) == 0xFF;
    for (int py = y0; py < y1; ++py) {
        uint32_t* row = px_.data() + static_cast<size_t>(py) * DESIGN_W;
        if (opaque) {
            std::fill(row + x0, row + x1, color);
        } else {
            for (int px = x0; px < x1; ++px) row[px] = blend(row[px], color);
        }
    }
}

void Canvas::set_surface_style(int style) {
    surfaceStyle_ = std::clamp(style, 0, kSurfaceStyleCount-1);
}

void Canvas::set_font_style(int style) { fontStyle_=valid_demo_font_style(style); }

void Canvas::surface_rect(int x, int y, int w, int h, Argb base) {
    if (w <= 0 || h <= 0) return;
    const int x0 = std::max(x, clipX_), y0 = std::max(y, clipY_);
    const int x1 = std::min(x + w, clipX_ + clipW_), y1 = std::min(y + h, clipY_ + clipH_);
    if (x0 >= x1 || y0 >= y1) return;
    // One background per canvas: only regenerate when its theme colour changes.
    // All subsequent frames copy runs, avoiding per-pixel noise work in the draw loop.
    if (surfacePixels_.empty() || surfaceColor_ != base || cachedSurfaceStyle_ != surfaceStyle_) {
        surfacePixels_.resize(static_cast<size_t>(DESIGN_W) * DESIGN_H);
        surfaceColor_ = base;
        cachedSurfaceStyle_ = surfaceStyle_;
        const bool pureBlack = (base & 0x00FFFFFFu) == 0;
        if (pureBlack || surfaceStyle_ == 7) {
            std::fill(surfacePixels_.begin(), surfacePixels_.end(), base);
        } else for (int py = 0; py < DESIGN_H; ++py) {
            const int light = 2 - py * 3 / DESIGN_H;
            for (int px = 0; px < DESIGN_W; ++px) {
                // Legacy textures keep their exact range. New opt-in materials permit a little
                // more relief, still far below the contrast of any foreground data or label.
                const int delta = std::clamp(light + material_delta(surfaceStyle_, px, py), -4, surfaceStyle_>=12?6:4);
                surfacePixels_[static_cast<size_t>(py) * DESIGN_W + px] = shade(base, delta);
            }
        }
    }
    const bool opaque = (base >> 24) == 255;
    for (int py = y0; py < y1; ++py) {
        auto* dest = px_.data() + static_cast<size_t>(py) * DESIGN_W;
        const auto* src = surfacePixels_.data() + static_cast<size_t>(py) * DESIGN_W;
        if (opaque) std::copy(src + x0, src + x1, dest + x0);
        else for (int px = x0; px < x1; ++px) dest[px] = blend(dest[px], src[px]);
    }
}

void Canvas::panel_rect(int x, int y, int w, int h, Argb base, Argb border, int radius) {
    if (w <= 0 || h <= 0) return;
    const int r = std::clamp(radius, 0, std::min(w, h) / 2);
    const auto inset = [](int row, int height, int rad) {
        if (rad == 0) return 0;
        const int edge = std::min(row, height - 1 - row);
        if (edge >= rad) return 0;
        const double dy = rad - edge - .5;
        return std::max(0, static_cast<int>(std::ceil(rad - std::sqrt(rad * rad - dy * dy) - .5)));
    };
    for (int row = 0; row < h; ++row) {
        const int outer = inset(row, h, r);
        if (row == 0 || row == h - 1 || w <= 2) {
            fill_rect(x + outer, y + row, w - 2 * outer, 1, border);
            continue;
        }
        const int inner = std::max(outer + 1, 1 + inset(row - 1, h - 2, std::max(0, r - 1)));
        // Border and fill spans never overlap: translucent shadows composite once.
        fill_rect(x + outer, y + row, inner - outer, 1, border);
        fill_rect(x + w - inner, y + row, inner - outer, 1, border);
        const bool opaque = (base >> 24) == 255;
        const int light = opaque ? (row == 1 ? 7 : 2 - row * 3 / h) : 0;
        fill_rect(x + inner, y + row, w - 2 * inner, 1, shade(base, light));
    }
}

void Canvas::stroke_rect(int x, int y, int w, int h, Argb color, int thickness) {
    if (w <= 0 || h <= 0 || thickness <= 0) return;
    const int t = std::min(thickness, std::min(w, h));
    fill_rect(x, y, w, t, color);                  // top
    fill_rect(x, y + h - t, w, t, color);          // bottom
    fill_rect(x, y + t, t, h - 2 * t, color);      // left
    fill_rect(x + w - t, y + t, t, h - 2 * t, color);  // right
}

void Canvas::line(int x0, int y0, int x1, int y1, Argb color) {
    if (clipW_ <= 0 || clipH_ <= 0 || (color >> 24) == 0) return;
    const int left = clipX_, right = clipX_ + clipW_ - 1;
    const int top = clipY_, bottom = clipY_ + clipH_ - 1;
    const double dx = static_cast<double>(x1) - x0, dy = static_cast<double>(y1) - y0;
    double enter = 0, leave = 1;
    const auto trim = [&](double p, double q) {
        if (p == 0) return q >= 0;
        const double t = q / p;
        if (p < 0) { if (t > leave) return false; enter = std::max(enter, t); }
        else { if (t < enter) return false; leave = std::min(leave, t); }
        return true;
    };
    if (!trim(-dx, static_cast<double>(x0) - left) || !trim(dx, static_cast<double>(right) - x0) ||
        !trim(-dy, static_cast<double>(y0) - top) || !trim(dy, static_cast<double>(bottom) - y0)) return;
    const int endX = std::clamp(static_cast<int>(std::lround(x0 + leave * dx)), left, right);
    const int endY = std::clamp(static_cast<int>(std::lround(y0 + leave * dy)), top, bottom);
    int x = std::clamp(static_cast<int>(std::lround(x0 + enter * dx)), left, right);
    int y = std::clamp(static_cast<int>(std::lround(y0 + enter * dy)), top, bottom);
    const int spanX = std::abs(endX - x), spanY = -std::abs(endY - y);
    const int stepX = x < endX ? 1 : -1, stepY = y < endY ? 1 : -1;
    int error = spanX + spanY;
    for (;;) {
        auto& pixel = px_[static_cast<size_t>(y) * DESIGN_W + x];
        pixel = blend(pixel, color);
        if (x == endX && y == endY) break;
        const int twice = error * 2;
        if (twice >= spanY) { error += spanY; x += stepX; }
        if (twice <= spanX) { error += spanX; y += stepY; }
    }
}

// Each source pixel a font_scale square. Drawn as runs and bands rather than per pixel (text is most
// of a frame's cost); the rects never overlap, so a translucent colour blends each pixel once.
void Canvas::draw_glyph(const Glyph& g, int x, int y, Argb color, int font_scale) {
    if (font_scale <= 0) return;
    for (int row = 0; row < 5;) {
        const uint8_t bits = g[static_cast<size_t>(row)];
        if (bits == 0) { ++row; continue; }

        int last = row + 1;
        while (last < 5 && g[static_cast<size_t>(last)] == bits) ++last;
        const int band_y = y + row * font_scale;
        const int band_h = (last - row) * font_scale;

        for (int col = 0; col < 5;) {
            if (!((bits >> (4 - col)) & 1)) { ++col; continue; }
            int end = col + 1;
            while (end < 5 && ((bits >> (4 - end)) & 1)) ++end;
            fill_rect(x + col * font_scale, band_y, (end - col) * font_scale, band_h, color);
            col = end;
        }
        row = last;
    }
}

void Canvas::draw_text(const std::string& text, int x, int y, Argb color, int spacing,
                       int font_scale) {
    int cx = x;
    for (size_t i = 0; i < text.size();) {
        const auto cp=next_codepoint(text,i);
        if((font_scale==2||font_scale==3)&&cp>=32&&cp<127){
            const int cell=font_scale*5;
            const int height=font_scale==3?studioFont15Height:studioFont10Height;
            const auto* custom=fontStyle_?detail::demo_grid_pixels(fontStyle_,font_scale,cp):nullptr;
            const auto* g=custom?custom:font_scale==3?studioFont15[cp-32]:studioFont10[cp-32];
            const int left = std::max(0, clipX_ - cx), right = std::min(cell, clipX_ + clipW_ - cx);
            const int top = std::max(0, clipY_ - y), bottom = std::min(height, clipY_ + clipH_ - y);
            const unsigned colorAlpha = color >> 24;
            for (int gy = top; gy < bottom; ++gy) {
                auto* dest = px_.data() + static_cast<size_t>(y + gy) * DESIGN_W;
                for (int gx = left; gx < right; ++gx) {
                    const unsigned alpha = (g[gy * cell + gx] * colorAlpha + 127) / 255;
                    if (alpha) dest[cx + gx] = blend(dest[cx + gx], (color & 0x00ffffffu) | alpha << 24);
                }
            }
        }else draw_glyph(glyph_for_codepoint(cp),cx,y,color,font_scale);
        cx += 5 * font_scale + spacing;
    }
}

void Canvas::draw_label(const std::string& text, int x, int y, Argb color, int size) {
    if (size <= 0 || clipW_ <= 0 || clipH_ <= 0 || (color >> 24) == 0) return;
    auto font = label_font(size);
    if(fontStyle_)if(const auto* custom=detail::demo_label_pixels(fontStyle_,size))font.pixels=custom;
    if (static_cast<int64_t>(y) + font.height <= clipY_ || y >= clipY_ + clipH_) return;
    const int top = static_cast<int>(std::max<int64_t>(0, static_cast<int64_t>(clipY_) - y));
    const int bottom = static_cast<int>(std::min<int64_t>(font.height, static_cast<int64_t>(clipY_) + clipH_ - y));
    const unsigned colorAlpha = color >> 24;
    int64_t pen = x;
    for (size_t i = 0; i < text.size();) {
        if (pen >= clipX_ + clipW_) break;
        const auto cp = next_codepoint(text, i);
        const int index = label_index(cp);
        if (index >= 0) {
            const auto& glyph = font.glyphs[index];
            const int64_t gx = pen + glyph.left;
            if (glyph.width > 0 && gx + glyph.width > clipX_ && gx < clipX_ + clipW_) {
                const int left = static_cast<int>(std::max<int64_t>(0, static_cast<int64_t>(clipX_) - gx));
                const int right = static_cast<int>(std::min<int64_t>(glyph.width, static_cast<int64_t>(clipX_) + clipW_ - gx));
                for (int row = top; row < bottom; ++row) {
                    auto* dest = px_.data() + static_cast<size_t>(y + row) * DESIGN_W;
                    const auto* source = font.pixels + glyph.offset + row * glyph.width;
                    for (int col = left; col < right; ++col) {
                        const unsigned alpha = (source[col] * colorAlpha + 127) / 255;
                        if (alpha) dest[gx + col] = blend(dest[gx + col], (color & 0x00ffffffu) | alpha << 24);
                    }
                }
            }
            pen += glyph.advance;
        } else {
            const int cell = font.fallbackScale * 5;
            if (pen + cell > clipX_)
                draw_glyph(glyph_for_codepoint(cp), static_cast<int>(pen), y + (font.height - cell) / 2, color, font.fallbackScale);
            pen += cell;
        }
    }
}

int Canvas::label_width(const std::string& text, int size) {
    if (size <= 0) return 0;
    const auto font = label_font(size);
    int64_t width = 0;
    for (size_t i = 0; i < text.size();) {
        width += label_advance(next_codepoint(text, i), font);
        if (width >= std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
    }
    return static_cast<int>(width);
}

std::string Canvas::clip_label(const std::string& text, int maxWidth, int size) {
    if (maxWidth <= 0 || size <= 0) return {};
    if (label_width(text, size) <= maxWidth) return text;
    const auto font = label_font(size);
    const int marker = label_advance(0x2026, font);
    if (marker > maxWidth) return {};
    int width = 0;
    size_t keep = 0;
    for (size_t i = 0; i < text.size();) {
        const int advance = label_advance(next_codepoint(text, i), font);
        if (advance > maxWidth - marker - width) break;
        width += advance;
        keep = i;
    }
    return text.substr(0, keep) + "\xE2\x80\xA6";
}

int Canvas::text_width(const std::string& text, int spacing, int font_scale) {
    return glyph_count(text) * (5 * font_scale + spacing) - (text.empty() ? 0 : spacing);
}

int Canvas::glyph_count(const std::string& text) {
    int n = 0;
    for (size_t i = 0; i < text.size();) {
        next_codepoint(text, i);
        ++n;
    }
    return n;
}

std::string Canvas::clip_text(const std::string& text, int max_glyphs) {
    if (max_glyphs <= 0) return {};

    size_t i   = 0;
    size_t cut = 0;   // byte offset just past glyph `max_glyphs - 1`, where the marker goes
    int    n   = 0;

    while (i < text.size()) {
        if (n == max_glyphs - 1) cut = i;
        next_codepoint(text, i);
        if (++n > max_glyphs) return text.substr(0, cut) + "\xE2\x80\xA6";
    }
    return text;
}

std::string Canvas::clip_text_head(const std::string& text, int max_glyphs) {
    if (max_glyphs <= 0) return {};
    const int total = glyph_count(text);
    if (total <= max_glyphs) return text;

    size_t i = 0;   // from the front: only that lands on a code-point boundary
    for (int skip = total - (max_glyphs - 1); skip > 0; --skip) next_codepoint(text, i);
    return "\xE2\x80\xA6" + text.substr(i);
}

}  // namespace pt::ui
