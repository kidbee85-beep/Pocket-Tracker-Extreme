// Pocket Tracker Extreme's deterministic, silent demoscene introduction.
// Draw-only: timing, skip input and startup ownership remain with the shell.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include "canvas.h"

namespace pt::ui {
namespace extreme_art {
constexpr float pi = 3.14159265358979323846f;
inline float fract(float x) { return x - std::floor(x); }
inline Argb alpha(Argb color, float a) {
    return (color & 0x00ffffffu) | (Argb(std::clamp(a, 0.f, 1.f) * 255.f) << 24);
}
inline Argb mix(Argb a, Argb b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto channel = [&](int shift) {
        return Argb(float((a >> shift) & 255) * (1.f - t) + float((b >> shift) & 255) * t);
    };
    return 0xff000000u | channel(16) << 16 | channel(8) << 8 | channel(0);
}
inline uint32_t hash(uint32_t value) {
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
struct Point { int x, y; float depth; };

// A bespoke slanted seven-row mark, deliberately different from the UI's bitmap grid font.
inline void logo(Canvas& c, int x, int y, Argb accent, Argb violet, float progress) {
    constexpr uint8_t letters[7][7] = {
        {31,16,16,30,16,16,31}, // E
        {17,17,10,4,10,17,17},  // X
        {31,4,4,4,4,4,4},      // T
        {30,17,17,30,20,18,17}, // R
        {31,16,16,30,16,16,31}, // E
        {17,27,21,21,17,17,17}, // M
        {31,16,16,30,16,16,31}  // E
    };
    constexpr int cell = 9, advance = 54;
    const float reveal = .45f + .55f * std::min(1.f, progress * 7.f);
    for (int row = 0; row < 7; ++row) {
        const float t = float(row) / 6.f;
        const Argb ink = t < .46f ? mix(0xfff5fbffu, accent, t / .46f) : mix(accent, violet, (t - .46f) / .54f);
        const int settle = progress < .2f ? int(std::sin(row * 2.1f + progress * 37.f) * (1.f - progress * 5.f) * 3.f) : 0;
        const int skew = (6 - row) * 2 + settle;
        for (int letter = 0; letter < 7; ++letter) {
            const uint8_t bits = letters[letter][row];
            for (int col = 0; col < 5;) {
                if (!(bits & (16 >> col))) { ++col; continue; }
                int end = col + 1;
                while (end < 5 && (bits & (16 >> end))) ++end;
                const int left = x + letter * advance + col * cell + skew;
                const int top = y + row * cell;
                const int width = (end - col) * cell - 1;
                c.fill_rect(left + 5, top + 6, width, cell - 1, alpha(violet, .20f * reveal));
                c.fill_rect(left - 1, top - 1, width + 2, cell + 1, alpha(accent, .08f * reveal));
                c.fill_rect(left, top, width, cell - 1, alpha(ink, reveal));
                c.fill_rect(left, top, width, 1, alpha(0xffffffffu, .28f * reveal));
                col = end;
            }
        }
    }
}
}

inline void draw_extreme_splash(Canvas& c, const Theme& theme, float progress) {
    using namespace extreme_art;
    const float p = std::isfinite(progress) ? std::clamp(progress, 0.f, 1.f) : 0.f;
    const Argb ground = mix(0xff070b16u, theme.background, .07f);
    const Argb mint = mix(0xff65e7d0u, theme.rowCursor, .30f);
    const Argb violet = mix(0xffa28effu, theme.textTitle, .22f);
    const Argb white = 0xffedf6ffu;
    c.reset_clip(); c.set_surface_style(theme.surfaceStyle);
    c.surface_rect(0, 0, DESIGN_W, DESIGN_H, ground);

    // Edge registration and quiet perspective floor. The travelling rings are geometric artwork,
    // not a simulated loading diagnostic or an audio activity meter.
    c.stroke_rect(18, 18, 604, 444, alpha(violet, .18f));
    c.fill_rect(18, 18, 74, 1, alpha(mint, .78f));
    c.fill_rect(548, 461, 74, 1, alpha(mint, .58f));
    for (int i = 0; i <= 10; ++i)
        c.line(320, 196, -160 + i * 96, 432, alpha(violet, .11f));
    for (int i = 0; i < 10; ++i) {
        const float depth = fract(float(i) / 10.f + p * .35f);
        const int yy = 196 + int(depth * depth * 236.f);
        c.line(20, yy, 620, yy, alpha(violet, .035f + depth * .10f));
    }
    for (int i = 0; i < 56; ++i) {
        const uint32_t seed = hash(uint32_t(i) + 63u);
        const float angle = float(seed & 65535u) / 65535.f * 2.f * pi;
        const float depth = .16f + .84f * fract(float(seed >> 16) / 65535.f + p * .22f);
        const float radius = 25.f + 156.f * depth;
        const int sx = 320 + int(std::cos(angle) * radius * 1.58f);
        const int sy = 139 + int(std::sin(angle) * radius * .60f);
        const Argb ink = alpha(i % 3 == 0 ? violet : mint, .10f + depth * .45f);
        const int length = depth > .8f ? 3 : 1;
        c.line(sx, sy, sx + int(std::cos(angle) * length), sy + int(std::sin(angle) * length), ink);
        if (i % 11 == 0) c.fill_rect(sx, sy, 2, 2, alpha(white, .6f));
    }

    // Rotating latitude/longitude shell with a shallow, audio-like surface deformation.
    const float yaw = p * 1.65f + .3f, pitch = .22f + .10f * std::sin(p * 4.f);
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const auto project = [&](float latitude, float longitude) {
        const float wave = 1.f + .035f * std::sin(longitude * 5.f + latitude * 3.f + p * 8.f);
        const float ux = std::cos(latitude) * std::cos(longitude) * wave;
        const float uy = std::sin(latitude) * wave;
        const float uz = std::cos(latitude) * std::sin(longitude) * wave;
        const float rx = ux * cy + uz * sy, rz = -ux * sy + uz * cy;
        const float ry = uy * cp - rz * sp, z = uy * sp + rz * cp;
        const float perspective = 3.2f / (3.2f + z);
        return Point{320 + int(rx * 77.f * perspective), 139 + int(ry * 77.f * perspective), z};
    };
    for (int lat = -2; lat <= 2; ++lat) {
        const float latitude = float(lat) * pi / 7.f;
        Point previous = project(latitude, 0);
        for (int segment = 1; segment <= 20; ++segment) {
            const auto q = project(latitude, float(segment) * 2.f * pi / 20.f);
            const float front = std::clamp(.5f - (previous.depth + q.depth) * .32f, 0.f, 1.f);
            c.line(previous.x, previous.y, q.x, q.y, alpha(mix(violet, mint, front), .14f + front * .54f));
            previous = q;
        }
    }
    for (int longitude = 0; longitude < 6; ++longitude) {
        const float angle = float(longitude) * 2.f * pi / 6.f;
        Point previous = project(-pi * .5f, angle);
        for (int segment = 1; segment <= 16; ++segment) {
            const auto q = project(-pi * .5f + float(segment) * pi / 16.f, angle);
            const float front = std::clamp(.5f - q.depth * .65f, 0.f, 1.f);
            c.line(previous.x, previous.y, q.x, q.y, alpha(mix(violet, mint, front), .12f + front * .48f));
            previous = q;
        }
    }
    for (int segment = 0; segment < 32; ++segment) {
        const float a = float(segment) * 2.f * pi / 32.f, b = float(segment + 1) * 2.f * pi / 32.f;
        const auto orbit = [&](float v) { return Point{320 + int(std::cos(v) * 111.f), 144 + int(std::sin(v) * 22.f + std::cos(v) * 16.f), 0}; };
        const auto one = orbit(a), two = orbit(b);
        c.line(one.x, one.y, two.x, two.y, alpha(violet, segment < 16 ? .24f : .46f));
    }
    const float satellite = p * 4.8f + .9f;
    const int satX = 320 + int(std::cos(satellite) * 111.f), satY = 144 + int(std::sin(satellite) * 22.f + std::cos(satellite) * 16.f);
    c.fill_rect(satX - 3, satY - 3, 7, 7, alpha(mint, .09f));
    c.fill_rect(satX - 1, satY - 1, 3, 3, white);

    static const std::string header = "HANDHELD MUSIC SYSTEM";
    static const std::string title = "Pocket Tracker";
    static const std::string identity = "SYNTHESIS  /  SAMPLES  /  SEQUENCES";
    static const std::string skip = "PRESS ANY KEY TO SKIP";
    c.draw_label(header, (DESIGN_W - Canvas::label_width(header, 12)) / 2, 31, alpha(violet, .78f), 12);
    c.draw_label(title, (DESIGN_W - Canvas::label_width(title, 18)) / 2, 233, white, 18);
    // Keep the main mark clean against the floor while allowing the grid to show through its gaps.
    c.fill_rect(94, 260, 452, 83, alpha(ground, .53f));
    logo(c, 126, 267, mint, violet, p);
    c.draw_label(identity, (DESIGN_W - Canvas::label_width(identity, 12)) / 2, 355, alpha(white, .66f), 12);

    // A compact animated ribbon closes the artwork. Its waveform is explicitly decorative.
    int previousX = 160, previousY = 394;
    for (int segment = 1; segment <= 64; ++segment) {
        const float u = float(segment) / 64.f;
        const int xx = 160 + segment * 5;
        const float envelope = std::sin(u * pi);
        const int yy = 394 + int(envelope * (std::sin(u * 24.f - p * 8.f) * 5.f + std::sin(u * 45.f + p * 7.f) * 3.f));
        c.line(previousX, previousY, xx, yy, alpha(mix(mint, violet, u), .55f));
        previousX = xx; previousY = yy;
    }
    c.fill_rect(240, 418, 160, 1, alpha(violet, .18f));
    c.fill_rect(240, 418, std::max(1, int(160.f * p)), 1, alpha(mint, .72f));
    c.draw_label(skip, (DESIGN_W - Canvas::label_width(skip, 12)) / 2, 434, alpha(violet, .58f), 12);
}
}
