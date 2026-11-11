// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/sample_granular_ui.h"
#include "ui/sample_granular_presets.h"
#include "ui/app_state.h"
#include "ui/premium.h"
#include "ui/std_filesystem.h"
#include "songcore/note_tables.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pt::ui {
namespace {
using namespace premium;
using studio::granular::GrainSnapshot;
using studio::granular::Snapshot;

constexpr int kLeft = 16;
constexpr int kWidth = 608;
constexpr int kWaveX = 28;
constexpr int kWaveY = 176;
constexpr int kWaveWidth = 584;
constexpr int kWaveHeight = 96;
constexpr int kOverviewY = 278;
constexpr int kOverviewHeight = 10;

bool monochrome(const Theme& theme) {
    return theme.name == "MONO" || theme.name.find("OLED") != std::string::npos;
}

void panel(Canvas& canvas, const Theme& theme, int x, int y, int width, int height,
           bool focused = false) {
    const Argb accent = theme.rowCursor;
    const Argb fill = focused ? mix(theme.rowEvery4th, accent, .10f) : theme.rowEvery4th;
    canvas.panel_rect(x, y, width, height, fill,
                      with_alpha(focused ? accent : theme.textParam, focused ? .88f : .20f), 5);
    if (focused) canvas.fill_rect(x + 2, y + 5, 2, height - 10, accent);
}

void right_label(Canvas& canvas, const std::string& text, int right, int y, Argb ink, int width) {
    const auto clipped = Canvas::clip_label(text, width);
    canvas.draw_label(clipped, right - Canvas::label_width(clipped), y, ink);
}

struct FrameRange {
    double first, end;
    double length() const { return end - first; }
    bool contains(double frame) const { return frame >= first && frame < end; }
    int column(double frame) const {
        return kWaveX + static_cast<int>(std::lround(
            std::clamp((frame - first) / length(), 0., 1.) * (kWaveWidth - 1)));
    }
};

bool has_source(const AppState& state) {
    const auto& granular = state.sampleGranular;
    const auto& source = granular.snapshot;
    return !granular.waitingForSource && source.sourceValid &&
           source.instrumentId == state.currentInstrument &&
           source.firstFrame >= 0 && source.endFrame > source.firstFrame &&
           source.endFrame <= source.sourceFrames && source.sourceRate > 0. &&
           std::isfinite(source.sourceRate);
}

bool has_waveform(const AppState& state, bool sourceReady) {
    const auto& granular = state.sampleGranular;
    const auto& source = granular.snapshot;
    return sourceReady && granular.waveformReady &&
           granular.waveformInstrument == state.currentInstrument &&
           granular.waveformGeneration == source.sourceGeneration &&
           granular.waveformFirst >= source.firstFrame &&
           granular.waveformEnd <= source.endFrame &&
           granular.waveformEnd > granular.waveformFirst;
}

void draw_grid(Canvas& canvas, const Theme& theme) {
    const Argb ground = darken(theme.vizBackground, .83f);
    const Argb trace = theme.textParam;
    canvas.fill_rect(kWaveX, kWaveY, kWaveWidth, kWaveHeight, ground);
    // The raster and registration marks stay still. Motion is driven by the current voice snapshot.
    for (int band = 0; band < 8; ++band) {
        const int left = kWaveX + band * kWaveWidth / 8;
        const int right = kWaveX + (band + 1) * kWaveWidth / 8;
        if (band % 2 == 0)
            canvas.fill_rect(left, kWaveY, right - left, kWaveHeight, with_alpha(trace, .025f));
        canvas.fill_rect(left, kWaveY, 1, kWaveHeight, with_alpha(trace, .14f));
        for (int tick = 1; tick < 4; ++tick) {
            const int x = left + tick * (right - left) / 4;
            canvas.fill_rect(x, kWaveY + kWaveHeight - 4, 1, 3, with_alpha(trace, .28f));
        }
    }
    for (int level = 1; level < 4; ++level) {
        const int y = kWaveY + level * kWaveHeight / 4;
        canvas.fill_rect(kWaveX, y, kWaveWidth, 1, with_alpha(trace, level == 2 ? .20f : .07f));
    }
    for (int side = 0; side < 2; ++side) {
        const int x = side ? kWaveX + kWaveWidth - 9 : kWaveX;
        const int outer = side ? x + 8 : x;
        const Argb mark = with_alpha(theme.rowCursor, .54f);
        canvas.fill_rect(x, kWaveY, 9, 1, mark);
        canvas.fill_rect(outer, kWaveY, 1, 6, mark);
        canvas.fill_rect(x, kWaveY + kWaveHeight - 1, 9, 1, mark);
        canvas.fill_rect(outer, kWaveY + kWaveHeight - 6, 1, 6, mark);
    }
}

void draw_pcm(Canvas& canvas, const SampleGranularUIState& granular, Argb ink) {
    const int middle = kWaveY + kWaveHeight / 2;
    constexpr int scale = kWaveHeight / 2 - 8;
    const Argb body = with_alpha(ink, .28f);
    const Argb rim = with_alpha(ink, .72f);
    // Interpolate the cached min/max pairs between their source bins. Each column has bounded work
    // regardless of sample length or zoom, with no decoder or sample reads on the render path.
    for (int x = 0; x < kWaveWidth; ++x) {
        const float bin = x * (kSampleGranularWaveBins - 1.f) / (kWaveWidth - 1);
        const int left = static_cast<int>(bin);
        const int right = std::min(left + 1, kSampleGranularWaveBins - 1);
        const float weight = bin - left;
        const float lo = granular.waveform[2 * left] * (1.f - weight) + granular.waveform[2 * right] * weight;
        const float hi = granular.waveform[2 * left + 1] * (1.f - weight) + granular.waveform[2 * right + 1] * weight;
        if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi) continue;
        const int top = middle - static_cast<int>(std::lround(std::clamp(hi, -1.f, 1.f) * scale));
        const int bottom = middle - static_cast<int>(std::lround(std::clamp(lo, -1.f, 1.f) * scale));
        canvas.fill_rect(kWaveX + x, top, 1, bottom - top + 1, body);
        canvas.fill_rect(kWaveX + x, top, 1, 1, rim);
        if (bottom > top) canvas.fill_rect(kWaveX + x, bottom, 1, 1, rim);
    }
}

void draw_span_edge(Canvas& canvas, const FrameRange& view, double left, int leftY,
                    double right, int rightY, Argb ink) {
    const double start = std::max(left, view.first);
    const double end = std::min(right, view.end);
    if (end < start || right <= left) return;
    const auto y_at = [&](double frame) {
        return leftY + static_cast<int>(std::lround((rightY - leftY) * (frame - left) / (right - left)));
    };
    canvas.line(view.column(start), y_at(start), view.column(end), y_at(end), ink);
}

bool visible_grain(const GrainSnapshot& grain, const FrameRange& region) {
    return grain.active && region.contains(grain.sourceFrame) &&
           std::isfinite(grain.signedReadIncrement) &&
           std::isfinite(grain.window) && std::isfinite(grain.pan);
}

void draw_grain(Canvas& canvas, const GrainSnapshot& grain, const FrameRange& region,
                const FrameRange& view, Argb forward, Argb reverse) {
    const bool backward = grain.signedReadIncrement < 0.;
    const Argb ink = backward ? reverse : forward;
    const float window = std::clamp(grain.window, 0.f, 1.f);
    const int y = kWaveY + kWaveHeight / 2 +
                  static_cast<int>(std::lround(std::clamp(grain.pan, -1.f, 1.f) * 29.f));
    const double travel = std::abs(grain.signedReadIncrement) * grain.lengthFrames;
    const double midpoint = grain.sourceFrame + grain.signedReadIncrement *
                            (grain.lengthFrames * .5 - grain.ageFrames);
    if (std::isfinite(travel) && travel > 0. && std::isfinite(midpoint)) {
        const double half = std::min(travel, region.length()) / 2.;
        double wrapped = std::fmod(midpoint - region.first, region.length());
        if (wrapped < 0.) wrapped += region.length();
        const int height = 4 + static_cast<int>(window * 8.f);
        const Argb edge = with_alpha(ink, .18f + window * .42f);
        // The outline shows the span at the current read rate. The moving head's brightness uses
        // the actual window amplitude. Wrap at the playable region, then clip to the viewport.
        for (int wrap = -1; wrap <= 1; ++wrap) {
            const double center = region.first + wrapped + wrap * region.length();
            draw_span_edge(canvas, view, center - half, y + height, center, y - height, edge);
            draw_span_edge(canvas, view, center, y - height, center + half, y + height, edge);
        }
    }
    if (!view.contains(grain.sourceFrame)) return;
    const int x = view.column(grain.sourceFrame);
    const int direction = backward ? -1 : 1;
    const int arrow = x + direction * 6;
    const int height = 3 + static_cast<int>(window * 6.f);
    canvas.fill_rect(x - 2, y - height - 2, 5, 2 * height + 5, with_alpha(ink, .09f + window * .17f));
    canvas.fill_rect(x, y - height, 1, 2 * height + 1, with_alpha(ink, .54f + window * .46f));
    canvas.line(x - direction * 5, y, arrow, y, with_alpha(ink, .45f + window * .55f));
    canvas.line(arrow - direction * 3, y - 3, arrow, y, ink);
    canvas.line(arrow, y, arrow - direction * 3, y + 3, ink);
}

void draw_scan(Canvas& canvas, const FrameRange& view, double frame, Argb ink) {
    if (!view.contains(frame)) return;
    const int x = view.column(frame);
    canvas.fill_rect(x - 3, kWaveY, 7, kWaveHeight, with_alpha(ink, .11f));
    canvas.fill_rect(x, kWaveY, 1, kWaveHeight, with_alpha(ink, .83f));
    canvas.line(x - 4, kWaveY + 1, x, kWaveY + 6, ink);
    canvas.line(x, kWaveY + 6, x + 4, kWaveY + 1, ink);
}

void draw_position(Canvas& canvas, const FrameRange& view, double frame, Argb ink,
                   bool editing) {
    if (!view.contains(frame)) return;
    const int x = view.column(frame);
    const int base = kWaveY + kWaveHeight - 7;
    const Argb dash = with_alpha(ink, editing ? .93f : .60f);
    for (int y = kWaveY + 10; y < base - 5; y += 7)
        canvas.fill_rect(x, y, 1, 3, dash);
    canvas.line(x - 4, base, x, base - 4, ink);
    canvas.line(x, base - 4, x + 4, base, ink);
    canvas.line(x + 4, base, x, base + 4, ink);
    canvas.line(x, base + 4, x - 4, base, ink);
    if (editing) canvas.fill_rect(x - 1, base - 1, 3, 3, ink);
}

void draw_overview(Canvas& canvas, const Theme& theme, const FrameRange& region,
                   const FrameRange& view, double position, double scan,
                   const Snapshot& snapshot, Argb positionInk, Argb forward, Argb reverse) {
    canvas.fill_rect(kWaveX, kOverviewY, kWaveWidth, kOverviewHeight, darken(theme.vizBackground, .73f));
    const int left = region.column(view.first);
    const int right = region.column(view.end);
    canvas.fill_rect(left, kOverviewY, right - left + 1, kOverviewHeight, with_alpha(theme.rowCursor, .24f));
    canvas.stroke_rect(left, kOverviewY, right - left + 1, kOverviewHeight, with_alpha(theme.rowCursor, .60f));
    for (int tick = 1; tick < 8; ++tick) {
        const int x = kWaveX + tick * kWaveWidth / 8;
        canvas.fill_rect(x, kOverviewY + 1, 1, kOverviewHeight - 2, with_alpha(theme.textParam, .21f));
    }
    if (snapshot.active) {
        if (region.contains(scan))
            canvas.fill_rect(region.column(scan), kOverviewY, 1, kOverviewHeight - 3, forward);
        for (const auto& grain : snapshot.grains) {
            if (!visible_grain(grain, region)) continue;
            const Argb ink = grain.signedReadIncrement < 0. ? reverse : forward;
            const int x = std::clamp(region.column(grain.sourceFrame) - 1, kWaveX, kWaveX + kWaveWidth - 3);
            canvas.fill_rect(x, kOverviewY + 4, 3, 2, with_alpha(ink, .40f + .60f * std::clamp(grain.window, 0.f, 1.f)));
        }
    }
    const int mark = std::clamp(region.column(position) - 1, kWaveX, kWaveX + kWaveWidth - 3);
    canvas.fill_rect(mark, kOverviewY + kOverviewHeight - 3, 3, 3, positionInk);
}

void draw_timeline(Canvas& canvas, const AppState& state, const FrameRange& region,
                   const FrameRange& view, double position, bool focused,
                   Argb positionInk, Argb scanInk) {
    const auto& theme = state.theme;
    const auto& snapshot = state.sampleGranular.snapshot;
    const double rate = snapshot.sourceRate;
    char time[80];
    std::snprintf(time, sizeof time, "%.3f - %.3f s", view.first / rate, view.end / rate);
    label(canvas, time, kWaveX, 291, theme.textValue, 193);
    char positionText[48];
    std::snprintf(positionText, sizeof positionText, "Position %.3f s", position / rate);
    label(canvas, positionText, 235, 291, positionInk, 154);
    if (snapshot.active) {
        label(canvas, "Scan", 393, 291, scanInk, 80);
    } else {
        char format[40];
        std::snprintf(format, sizeof format, "%.1f kHz", rate / 1000.);
        label(canvas, format, 393, 291, theme.textParam, 80);
    }
    char zoom[40];
    const double ratio = region.length() / view.length();
    if (ratio <= 1.000001) std::snprintf(zoom, sizeof zoom, "Full region");
    else std::snprintf(zoom, sizeof zoom, "%.2g× zoom", ratio);
    right_label(canvas, zoom, kWaveX + kWaveWidth, 291, focused ? theme.rowCursor : theme.textValue, 130);
}

void draw_wave_panel(Canvas& canvas, const AppState& state) {
    const auto& granular = state.sampleGranular;
    const auto& snap = granular.snapshot;
    const auto& theme = state.theme;
    const bool sourceReady = has_source(state);
    const bool waveformReady = has_waveform(state, sourceReady);
    const bool focused = state.studioRow == kGranularWaveRow;
    const bool positionMode = granular.waveformMode == GranularWaveformMode::Position;
    const bool mono = monochrome(theme);
    const Argb forward = mono ? theme.rowCursor : mix(theme.rowCursor, 0xFF67F6CFu, .72f);
    const Argb reverse = mono ? theme.rowCursor : mix(theme.rowCursor, 0xFFFF947Du, .74f);
    const Argb waveInk = mono ? theme.textValue : mix(theme.vizWave, theme.textTitle, .58f);
    const Argb positionInk = mono ? theme.textValue : mix(theme.textValue, 0xFFFFBE70u, .76f);
    panel(canvas, theme, kLeft, 146, kWidth, 165, focused);
    const std::string name = granular.sourcePath.empty() ? "Sample source" : path_name(granular.sourcePath);
    label(canvas, name, kWaveX, 150, theme.textValue, 234, 18);
    const Argb modeInk = positionMode ? positionInk : theme.rowCursor;
    canvas.panel_rect(273, 150, 99, 19, mix(theme.rowEvery4th, modeInk, .11f),
                      with_alpha(modeInk, focused ? .78f : .30f), 4);
    label(canvas, positionMode ? "Position" : "View", 282, 152, modeInk, 83);
    const char* status = granular.waitingForSource ? "Loading" : !sourceReady ? "No sample" :
                         snap.active && snap.frozen ? "Frozen" :
                         snap.active && snap.releasing ? "Release" :
                         snap.active && (state.studioAuditionHeld || granular.recording.ownsRoot) ? "Audition" :
                         snap.active ? "Playing" : "Idle";
    label(canvas, status, 385, 153, snap.active && sourceReady ? theme.rowCursor : theme.textParam, 95);
    char count[36];
    std::snprintf(count, sizeof count, "%02d / %d grains",
                  sourceReady && snap.active ? std::clamp(snap.activeGrains, 0, studio::granular::kMaxGrains) : 0,
                  studio::granular::kMaxGrains);
    right_label(canvas, count, kWaveX + kWaveWidth, 153, theme.textValue, 129);
    draw_grid(canvas, theme);
    if (!waveformReady) {
        const char* hint = granular.waitingForSource || sourceReady ? "Preparing waveform" :
                           granular.sourcePath.empty() ? "Choose Load source to start" : "Sample unavailable";
        const int width = Canvas::label_width(hint);
        canvas.draw_label(hint, kWaveX + (kWaveWidth - width) / 2, kWaveY + 41, theme.textParam);
        return;
    }
    const FrameRange region{double(snap.firstFrame), double(snap.endFrame)};
    const FrameRange view{double(granular.waveformFirst), double(granular.waveformEnd)};
    const double position = region.first +
        std::clamp(granular.parameters.position, 0.f, 1.f) * (region.length() - 1.);
    const double scan = snap.scanSourceFrame;
    {
        Canvas::ClipScope clip(canvas, kWaveX, kWaveY, kWaveWidth, kWaveHeight);
        draw_pcm(canvas, granular, waveInk);
        if (snap.active) {
            draw_scan(canvas, view, scan, forward);
            for (const auto& grain : snap.grains)
                if (visible_grain(grain, region)) draw_grain(canvas, grain, region, view, forward, reverse);
        }
        draw_position(canvas, view, position, positionInk, focused && positionMode);
    }
    draw_overview(canvas, theme, region, view, position, scan, snap, positionInk, forward, reverse);
    draw_timeline(canvas, state, region, view, position, focused, positionInk, forward);
}

void draw_target(Canvas& canvas, const AppState& state, const songcore::Instrument& instrument) {
    const auto& theme = state.theme;
    const auto& granular = state.sampleGranular;
    panel(canvas, theme, 16, 100, 86, 39, state.studioRow == 0);
    label(canvas, "SLOT", 26, 103, theme.textParam, 40);
    label(canvas, songcore::hex2(state.currentInstrument), 26, 115, theme.textValue, 35, 18);
    right_label(canvas, songcore::note_name(instrument.root), 93, 121, theme.rowCursor, 30);
    panel(canvas, theme, 109, 100, 182, 39, state.studioRow == 1);
    label(canvas, instrument.granularEnabled ? "ENGINE ON" : "ENGINE BYPASSED",
          120, 103, instrument.granularEnabled ? theme.rowCursor : theme.textParam, 159);
    label(canvas, "Sample Granular", 120, 115, theme.textValue, 159, 18);
    panel(canvas, theme, 298, 100, 326, 39, state.studioRow == 2);
    const int category = granular.factoryPreset / kSampleGranularPerCategory;
    const std::string bank = granular.custom ? "GRAIN PRESET" :
                             std::string("GRAIN PRESET / ") + sample_granular_category_short(category);
    label(canvas, bank, 310, 103, theme.textParam, 218);
    if (!granular.custom) {
        char index[20];
        std::snprintf(index, sizeof index, "%02d / 16", granular.factoryPreset % 16 + 1);
        right_label(canvas, index, 612, 103, theme.rowCursor, 78);
    }
    label(canvas, granular.presetName.empty() ? "Current settings" : granular.presetName,
          310, 115, theme.textValue, 301, 18);
}

void draw_factory_picker(Canvas& canvas, const AppState& state) {
    const auto& theme = state.theme;
    const auto& granular = state.sampleGranular;
    const auto& picker = granular.presetPicker;
    const bool favorites = picker.bank == kSampleGranularFavoritesBank;
    const int count = sample_granular_picker_count(state);
    const int chosen = sample_granular_picker_index(state, picker.item);
    panel(canvas, theme, kLeft, 100, kWidth, 39);
    label(canvas, favorites ? "GRAIN FAVORITES" : "GRAIN PRESETS", 27, 109, theme.textTitle, 220, 18);
    right_label(canvas, "Slot " + songcore::hex2(state.currentInstrument) + "  /  " + granular.presetName,
                613, 114, theme.textParam, 345);

    constexpr int bankGap = 4;
    constexpr int bankCount = kSampleGranularCategories + 1;
    constexpr int bankWidth = (kWidth - (bankCount - 1) * bankGap) / bankCount;
    for (int bank = 0; bank < bankCount; ++bank) {
        const bool active = bank == picker.bank;
        const bool focused = active && picker.banksFocused;
        const int x = kLeft + bank * (bankWidth + bankGap);
        const int width = bank == bankCount - 1 ? kLeft + kWidth - x : bankWidth;
        const Argb fill = focused ? theme.rowSelection :
                          active ? mix(theme.rowEvery4th, theme.rowCursor, .17f) : theme.rowEvery4th;
        canvas.panel_rect(x, 147, width, 25, fill,
                          with_alpha(active ? theme.rowCursor : theme.textParam, active ? .92f : .20f), 4);
        const std::string name = bank == kSampleGranularFavoritesBank ? "FAV" : sample_granular_category_short(bank);
        const auto clipped = Canvas::clip_label(name, width - 8);
        canvas.draw_label(clipped, x + (width - Canvas::label_width(clipped)) / 2, 153,
                          focused ? theme.rowEvery4th : theme.textValue);
    }

    constexpr int columns = kSampleGranularPickerColumns;
    constexpr int patchGap = 4;
    constexpr int patchWidth = (kWidth - (columns - 1) * patchGap) / columns;
    for (int item = 0; item < count; ++item) {
        const int index = sample_granular_picker_index(state, item);
        const int column = item % columns;
        const int x = kLeft + column * (patchWidth + patchGap);
        const int y = 180 + (item / columns) * 35;
        const int width = column == columns - 1 ? kLeft + kWidth - x : patchWidth;
        const bool focused = !picker.banksFocused && picker.item == item;
        const bool current = !granular.custom && granular.factoryPreset == index;
        const bool marked = !favorites && sample_granular_preset_is_favorite(state, index);
        const auto ink = focused ? theme.rowEvery4th : theme.textValue;
        canvas.panel_rect(x, y, width, 30, focused ? theme.rowSelection : theme.rowEvery4th,
                          with_alpha(focused ? theme.rowCursor : theme.textParam, focused ? .95f : .20f), 4);
        label(canvas, sample_granular_factory_preset(index).name, x + 8, y + 7, ink,
              width - 16 - (current ? 7 : 0) - (marked ? 12 : 0), 14);
        if (current)
            canvas.fill_rect(x + width - 11, y + 12, 4, 4, focused ? ink : theme.rowCursor);
        if (marked) {
            const int starX = x + width - (current ? 21 : 12);
            const auto starInk = focused ? ink : theme.rowCursor;
            canvas.line(starX - 3, y + 14, starX + 3, y + 14, starInk);
            canvas.line(starX, y + 11, starX, y + 17, starInk);
        }
    }

    if (chosen >= 0) {
        const auto& preset = sample_granular_factory_preset(chosen);
        panel(canvas, theme, kLeft, 325, kWidth, 94);
        label(canvas, sample_granular_category(preset.category), 27, 331, theme.textTitle, 300, 15);
        std::string status;
        if (!granular.custom && granular.factoryPreset == chosen) status = "Current preset";
        if (sample_granular_preset_is_favorite(state, chosen))
            status += status.empty() ? "Favorite" : " / Favorite";
        right_label(canvas, status, 613, 332, theme.rowCursor, 190);
        label(canvas, preset.description, 27, 350, theme.textParam, 586);

        const auto& parameters = preset.parameters;
        char size[24], density[24], pitch[24], scan[24], stereo[24];
        std::snprintf(size, sizeof size, "%.0f ms", parameters.sizeMs);
        std::snprintf(density, sizeof density, "%.0f Hz", parameters.densityHz);
        std::snprintf(pitch, sizeof pitch, "%+.0f st", parameters.pitchSemitones);
        std::snprintf(scan, sizeof scan, "%+.2f x", parameters.scanSpeed);
        std::snprintf(stereo, sizeof stereo, "%.0f %%", parameters.panSpread * 100.f);
        const char* names[] = {"Grain size", "Density", "Pitch", "Scan", "Pan spread"};
        const char* values[] = {size, density, pitch, parameters.freeze ? "Frozen" : scan, stereo};
        for (int metric = 0; metric < 5; ++metric) {
            const int x = 27 + metric * 118;
            if (metric) canvas.fill_rect(x - 8, 374, 1, 33, with_alpha(theme.textParam, .22f));
            label(canvas, names[metric], x, 372, theme.textParam, 104);
            label(canvas, values[metric], x, 388, theme.textValue, 104, 16);
        }
    } else {
        panel(canvas, theme, kLeft, 180, kWidth, 105);
        label(canvas, "No favorites yet", 30, 199, theme.textTitle, 570, 18);
        label(canvas, "Choose a factory preset and press A + Down to mark it.",
              30, 234, theme.textParam, 570, 14);
    }

    canvas.fill_rect(18, 424, 606, 1, with_alpha(theme.textParam, .20f));
    const char* guide = picker.banksFocused ? "Left / Right: Banks    Down: Presets    Select: Help" :
                                            "D-pad: Presets    L / R: Bank    Select: Help";
    label(canvas, state.studioMessage.empty() ? guide : state.studioMessage,
          22, 435, state.studioMessage.empty() ? theme.textParam : theme.rowCursor, 596);
    const char* shortcuts = picker.banksFocused || count == 0
        ? (favorites ? "B: Cancel    A + Up: Return to bank" : "A: Open bank    B: Cancel    A + Up: Favorites")
        : (favorites ? "A: Apply    B: Cancel    A + Down: Remove    A + Up: Return"
                     : "A: Apply    B: Cancel    A + Down: Mark / remove    A + Up: Favorites");
    label(canvas, shortcuts, 22, 457, theme.textValue, 596);
}

void draw_parameters(Canvas& canvas, const AppState& state, int count) {
    const auto& theme = state.theme;
    const auto pages = sample_granular_page_count();
    const int page = std::clamp(state.studioSynthPage, 0, pages - 1);
    label(canvas, sample_granular_page_name(state), 18, 316, theme.rowCursor, 274);
    const std::string navigation = std::to_string(page + 1) + " / " + std::to_string(pages) + "   R + Left / Right";
    right_label(canvas, navigation, 622, 316, theme.textParam, 221);
    for (int i = 0; i < pages; ++i) {
        canvas.fill_rect(310 + i * 13, 322, 9, 3, with_alpha(theme.rowCursor, i == page ? .95f : .20f));
    }
    constexpr int gap = 6;
    const int available = kWidth - (count - 1) * gap;
    for (int i = 0; i < count; ++i) {
        const int x = kLeft + i * available / count + i * gap;
        const int width = (i + 1) * available / count - i * available / count;
        const bool focused = state.studioRow == i + 3;
        panel(canvas, theme, x, 333, width, 42, focused);
        label(canvas, sample_granular_control_label(state, i), x + 11, 336,
              focused ? theme.rowCursor : theme.textParam, width - 22);
        label(canvas, sample_granular_control_value(state, i), x + 11, 350,
              theme.textValue, width - 22, 18);
        const int level = std::clamp(sample_granular_control_level(state, i), 0, 255);
        const int meterWidth = width - 22;
        canvas.fill_rect(x + 11, 371, meterWidth, 1, with_alpha(theme.textParam, .22f));
        canvas.fill_rect(x + 11, 371, level * meterWidth / 255, 1,
                         with_alpha(theme.rowCursor, focused ? .92f : .62f));
    }
}

void action_pill(Canvas& canvas, const Theme& theme, const char* text, int x, int y, int width,
                 bool focused, Argb ink, bool active = false) {
    canvas.panel_rect(x, y, width, 19,
                      focused || active ? mix(theme.rowEvery4th, ink, focused ? .22f : .12f) : theme.rowEvery4th,
                      with_alpha(focused || active ? ink : theme.textParam, focused ? .95f : active ? .56f : .17f), 4);
    const auto clipped = Canvas::clip_label(text, width - 10);
    canvas.draw_label(clipped, x + (width - Canvas::label_width(clipped)) / 2,
                      y + 2, focused || active ? ink : theme.textValue);
}

void draw_actions(Canvas& canvas, const AppState& state, const songcore::Instrument& instrument,
                  int actionRow) {
    const auto& theme = state.theme;
    const auto& granular = state.sampleGranular;
    const auto focused = [&](int action) { return state.studioRow == actionRow && granular.action == action; };
    const char* actions[] = {instrument.granularEnabled ? "Bypass" : "Enable", "Load source", "Crop / edit",
                             "Save settings", "Load preset"};
    constexpr int gap = 5;
    constexpr int width = (kWidth - 4 * gap) / 5;
    for (int i = 0; i < 5; ++i) {
        action_pill(canvas, theme, actions[i], kLeft + i * (width + gap), 380,
                    width + (i == 4 ? 3 : 0), focused(i), theme.rowCursor);
    }
    const auto& recording = granular.recording;
    using RecordingState = studio::GranularDiskTake::State;
    const bool busy = granular_recording_busy(recording);
    const bool running = recording.state == RecordingState::Recording;
    const Argb recInk = monochrome(theme) ? theme.rowCursor : mix(theme.rowCursor, 0xFFFF726Eu, .82f);
    const char* record = recording.state == RecordingState::Stopping ? "Saving" : busy ? "Stop" : "Rec";
    action_pill(canvas, theme, record, 16, 402, 79, focused(kGranularRecordAction), recInk, busy);
    if (running) canvas.fill_rect(26, 409, 5, 5, recInk);
    action_pill(canvas, theme, "Use take", 101, 402, 99, focused(kGranularUseTakeAction), theme.rowCursor,
                granular_recording_has_take(recording));
    const int amount = granular.compareMorph.amount;
    char morphLabel[20];
    const char* compare = !granular.compareAvailable ? "A/B" : granular.compareLiveB ? "A/B   B" : "A/B   A";
    if (amount >= 0) {
        std::snprintf(morphLabel, sizeof morphLabel, "A/B   %d%%", amount);
        compare = morphLabel;
    }
    action_pill(canvas, theme, compare, 206, 402, 89, focused(kGranularCompareAction), theme.rowCursor,
                granular.compareAvailable);
    if (amount >= 0) {
        constexpr int start = 218;
        constexpr int width = 65;
        const int end = width * amount / 100;
        canvas.fill_rect(start, 417, width + 1, 1, with_alpha(theme.textParam, .55f));
        canvas.fill_rect(start, 417, end + 1, 1, with_alpha(theme.rowCursor, .9f));
        canvas.fill_rect(start + end, 416, 2, 3, theme.textValue);
    }
    const auto& snapshot = granular.snapshot;
    const char* limit = snapshot.active && snapshot.rateClamped ? "Rate limited" :
                        snapshot.active && snapshot.activeGrains >= studio::granular::kMaxGrains &&
                        snapshot.skippedSpawns ? "Grain pool full" : nullptr;
    const auto status = limit && recording.state == RecordingState::Idle && recording.message.empty() &&
                        !granular_recording_has_take(recording) ?
                        std::string(limit) : granular_recording_status(recording);
    label(canvas, status, 307, 404, running ? recInk : theme.textParam, 317);
}
} // namespace

void draw_sample_granular(Canvas& canvas, const AppState& state) {
    if (!state.project || state.currentInstrument < 0 ||
        state.currentInstrument >= static_cast<int>(state.project->instruments.size())) return;
    if (sample_granular_preset_picker_open(state)) {
        draw_factory_picker(canvas, state);
        return;
    }
    const auto& instrument = state.project->instruments[state.currentInstrument];
    const int count = sample_granular_control_count(state);
    draw_target(canvas, state, instrument);
    draw_wave_panel(canvas, state);
    draw_parameters(canvas, state, count);
    draw_actions(canvas, state, instrument, std::max(6, 3 + count));
}
} // namespace pt::ui
