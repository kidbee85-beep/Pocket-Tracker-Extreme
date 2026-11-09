#include "ui/modules/sample_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ui/helpers.h"
#include "ui/modules/confirm_dialog.h"

namespace pt::ui {

namespace {

// A bounds-safe `list[i]`: a slip shows a wrong label rather than reading past the end.
const std::string& at(const std::vector<std::string>& v, int i) {
    static const std::string kEmpty;
    if (v.empty()) return kEmpty;
    return v[static_cast<size_t>(std::clamp(i, 0, static_cast<int>(v.size()) - 1))];
}

/** A vertical 1px rule from (x, y0), `h` tall. */
void v_line(Canvas& c, int x, int y0, int h, Argb color, int thickness = 1) {
    if (h <= 0) return;   // a zero-length butt-capped line draws nothing; neither does this
    c.fill_rect(x, y0, thickness, h, color);
}

}  // namespace

// ─── The vocabularies ────────────────────────────────────────────────────────────────────────────

const std::vector<std::string>& SampleEditorModule::source_values() {
    static const std::vector<std::string> v{"LEFT", "RIGHT", "STEREO", "MONO"};
    return v;
}
const std::vector<std::string>& SampleEditorModule::rate_values() {
    static const std::vector<std::string> v{"HIGH", "NORM", "LOFI"};
    return v;
}
std::vector<std::string> SampleEditorModule::bit_depth_choices(int source_bits) {
    std::vector<std::string> v;
    for (const int bits : {32, 24, 16, 8})
        if (bits <= source_bits) v.push_back(std::to_string(bits));
    if (v.empty()) v.push_back("8");
    return v;
}
const std::vector<std::string>& SampleEditorModule::duration_values() {
    static const std::vector<std::string> v{"4 BAR", "2 BAR", "1 BAR", "1/2",
                                            "1/4",   "1/8",   "1/16",  "1/32"};
    return v;
}
double SampleEditorModule::duration_beats(int duration_index) {
    const int i = std::clamp(duration_index, 0, static_cast<int>(duration_values().size()) - 1);
    return 16.0 / static_cast<double>(1 << i);   // 4 BAR = 16 beats, halving to 1/32 = 0.125
}
const std::vector<std::string>& SampleEditorModule::fx_types() {
    static const std::vector<std::string> v{"OTT", "DUST", "DRIVE", "EQ", "SYNC"};
    return v;
}
const std::vector<std::string>& SampleEditorModule::sync_types() {
    static const std::vector<std::string> v{"RPITCH", "TSTRETCH"};
    return v;
}
const std::vector<std::string>& SampleEditorModule::slice_methods() {
    static const std::vector<std::string> v{"TRANSIENT", "DIVIDE", "OFF", "MANUAL"};
    return v;
}
const std::vector<std::string>& SampleEditorModule::ops_row1() {
    static const std::vector<std::string> v{"CROP", "COPY", "CUT", "DUPL", "PASTE", "DEL"};
    return v;
}
const std::vector<std::string>& SampleEditorModule::ops_row2() {
    static const std::vector<std::string> v{"NORM", "FADE+", "FADE-", "SLNC", "REV", "UNDO"};
    return v;
}
const std::vector<std::string>& SampleEditorModule::ops_row3() {
    static const std::vector<std::string> v{"DC FIX", "INVERT", "DECLICK"};
    return v;
}

int SampleEditorModule::content_y(int row) {
    // Physical rows remain independent of the sparse keyboard row IDs.
    switch (row) {
        case 8: return 226;
        case 9: return 247;
        case 10: return 273;
        case 11: return 294;
        case 12: return 315;
        case 13: return 341;
        case 14: return 362;
        case 15: return 383;
        case 16: return 409;
        case 18: return 435;
        case 19: return 456;
        default: return 226 + (row - 8) * ROW_HEIGHT;
    }
}

// ─── The sparse row map ──────────────────────────────────────────────────────────────────────────

int SampleEditorModule::row_above(int row, int slice_method) {
    switch (row) {
        case 0:  return 0;
        case 1:  return 19;   // wrap to the last row
        case 8:  return 2;
        case 10: return 9;
        case 12: return (slice_method == SLICE_OFF) ? 10 : 11;
        case 16: return 15;
        case 18: return 16;
        default: return row - 1;
    }
}

int SampleEditorModule::row_below(int row, int slice_method) {
    switch (row) {
        case 2:  return 8;
        case 8:  return 9;
        case 10: return (slice_method == SLICE_OFF) ? 12 : 11;
        case 14: return 15;
        case 16: return 18;
        case 19: return 1;    // wrap to the first row
        default: return row + 1;
    }
}

int SampleEditorModule::max_col_for_row(int row, int slice_method) {
    if (row == 1 || row == 2) return 2;
    if (row >= 3 && row <= 8) return 1;          // the selection's two edges (START / END)
    if (row == 9) return 4;                     // FIT SEL / FIT SLICE / ALL / SNAP / GAIN
    if (row == 10) return slice_has_parameter(slice_method) ? 1 : 0;
    if (row == 11) return 1;
    if (row == 12) return 3;
    if (row == 13 || row == 14) return 5;        // six ops per row
    if (row == 15) return 2;
    if (row == 16) return 2;                     // TYPE / VALUE / APPLY
    if (row == 18) return 0;                     // NAME — the whole row is one cell
    if (row == 19) return (slice_method == SLICE_OFF) ? 2 : 3;   // CHOP only exists with slices
    return 0;
}

// ─── SampleEditorState: the derived values ───────────────────────────────────────────────────────

int SampleEditorState::manual_key_param() const {
    if (sliceMethod == SampleEditorModule::SLICE_TRANSIENT) return sliceSensitivity;
    if (sliceMethod == SampleEditorModule::SLICE_DIVIDE)    return sliceDivisions;
    return 0;   // MANUAL and OFF have no parameter for the markers to be keyed to
}

bool SampleEditorState::manual_markers_live() const {
    // ⚠️ The stamp alone: an empty live list (MANUAL, every boundary deleted) must still override
    // the file's cue points.
    return manualKeyMethod == sliceMethod && manualKeyParam == manual_key_param();
}

const std::vector<int>& SampleEditorState::method_markers() const {
    // Methods that compute rather than store read this empty list.
    static const std::vector<int> computed;

    switch (sliceMethod) {
        case SampleEditorModule::SLICE_TRANSIENT: return transientMarkers;
        // OFF and MANUAL both answer with the file's cue points, so MANUAL edits what it carries.
        case SampleEditorModule::SLICE_OFF:
        case SampleEditorModule::SLICE_MANUAL:    return fileMarkers;
        // DIVIDE derives its boundaries from `sliceDivisions` on every read, so there is no list.
        default:                                  return computed;
    }
}

int SampleEditorState::marker_count() const {
    return static_cast<int>(manual_markers_live() ? manualMarkers.size() : method_markers().size());
}

int64_t SampleEditorState::marker_position(int k) const {
    const int n = marker_count();
    if (k < 0 || n == 0) return 0;
    const int i = std::min(k, n - 1);   // past the end: the boundary an unplaced one is born on
    return manual_markers_live() ? static_cast<int64_t>(manualMarkers[static_cast<size_t>(i)].frame)
                                 : static_cast<int64_t>(method_markers()[static_cast<size_t>(i)]);
}

int SampleEditorState::slice_marker_index() const {
    if (sliceMethod == SampleEditorModule::SLICE_OFF) return -1;
    return sliceIndex - 1;   // −1 at index 0: slice 00's left edge is the sample's own start
}

int SampleEditorState::slice_index_ceiling() const {
    const int markers = marker_count();
    switch (sliceMethod) {
        // N markers make N + 1 slices …
        case SampleEditorModule::SLICE_TRANSIENT: return markers;
        // … and MANUAL reaches one further, a slice born when dragged off its left neighbour.
        case SampleEditorModule::SLICE_MANUAL:    return markers + 1;
        case SampleEditorModule::SLICE_DIVIDE:
            return markers > 0 ? markers : std::max(sliceDivisions - 1, 0);
        default:                                  return 0;
    }
}

void SampleEditorState::slice_bounds(int idx, int64_t& start, int64_t& end) const {
    const int64_t total = static_cast<int64_t>(totalFrames);
    if (sliceMethod == SampleEditorModule::SLICE_OFF) {
        start = 0;
        end   = total;
        return;
    }

    // Marker `idx − 1` is slice `idx`'s left edge, marker `idx` its right.
    const int n = marker_count();
    if (n > 0) {
        // `marker_position` clamps past the end (MANUAL's free slot); the right edge does not —
        // past the last boundary the slice runs to the end.
        start = (idx == 0) ? 0 : marker_position(idx - 1);
        end   = (idx < n) ? marker_position(idx) : total;
        return;
    }

    if (sliceMethod == SampleEditorModule::SLICE_DIVIDE) {
        const int64_t div = std::max(sliceDivisions, 1);
        start = (static_cast<int64_t>(idx) * total) / div;
        end   = std::min((static_cast<int64_t>(idx + 1) * total) / div, total);
        return;
    }

    start = 0;   // TRANSIENT before the detector has run, and MANUAL on a sample with no boundaries
    end   = total;
}

int64_t SampleEditorState::effective_slice_position() const {
    int64_t start = 0, end = 0;
    slice_bounds(sliceIndex, start, end);
    return start;
}

int64_t SampleEditorState::view_start() const {
    if (zoomLevel == 0 || totalFrames <= 0) return 0;

    const int64_t total   = static_cast<int64_t>(totalFrames);
    const int64_t visible = visible_frames();

    const auto window_for = [&](int64_t center) {
        return std::clamp<int64_t>(center - visible / 2, 0, std::max<int64_t>(total - visible, 0));
    };

    // With nothing sounding, centre on the marker the cursor points at.
    int64_t center;
    if (viewCenter >= 0) {
        center = viewCenter;
    } else if (cursorRow == 8 && cursorCol == 1) {
        center = selectionEnd;
    } else if (cursorRow == 11 && sliceMethod != SampleEditorModule::SLICE_OFF) {
        center = effective_slice_position();
    } else {
        center = selectionStart;   // row 8 col 0, and every other row
    }
    const int64_t anchored = window_for(center);

    // A playhead pulls the window only when the selection does not fit; otherwise the view would
    // shake with nothing to reveal.
    if (viewCenter < 0 && playbackPosition >= 0.0f &&
        !(selectionStart >= anchored && selectionEnd <= anchored + visible)) {
        return window_for(static_cast<int64_t>(playbackPosition * static_cast<float>(total)));
    }
    return anchored;
}

int64_t SampleEditorState::view_end() const {
    const int64_t total = static_cast<int64_t>(totalFrames);
    if (zoomLevel == 0 || totalFrames <= 0) return total;
    const int64_t visible = visible_frames();
    return std::min<int64_t>(view_start() + visible, total);
}

int SampleEditorState::max_zoom() const {
    // 64 visible frames leaves room to inspect the waveform between individual samples.
    int level = 0;
    const int64_t total = std::max(totalFrames, 1);
    while (level < 24 && (total >> level) > 64) ++level;
    return std::max(4, level);
}

int64_t SampleEditorState::visible_frames() const {
    return std::max<int64_t>(static_cast<int64_t>(std::max(totalFrames, 0)) >>
                            std::clamp(zoomLevel, 0, max_zoom()), 1);
}

int64_t SampleEditorState::fine_step() const {
    return std::max<int64_t>(visible_frames() / SampleEditorModule::WAVEFORM_W, 1);
}

void SampleEditorState::set_zoom(int level) {
    zoomLevel = std::clamp(level, 0, max_zoom());
    viewCenter = -1;
}

void SampleEditorState::fit_selection(bool slice) {
    int64_t start = selectionStart, end = selectionEnd;
    if (slice) slice_bounds(sliceIndex, start, end);
    start = std::clamp<int64_t>(start, 0, std::max(totalFrames, 0));
    end = std::clamp<int64_t>(end, start, std::max(totalFrames, 0));
    const int64_t length = std::max<int64_t>(end - start, 1);
    zoomLevel = 0;
    while (zoomLevel < max_zoom() &&
           std::max<int64_t>(static_cast<int64_t>(totalFrames) >> (zoomLevel + 1), 1) >= length)
        ++zoomLevel;
    viewCenter = start + (end - start) / 2;
}

std::string SampleEditorState::zoom_display() const {
    const int level = std::clamp(zoomLevel, 0, max_zoom());
    if (level >= 20) return std::to_string(1 << (level - 20)) + "MX";
    if (level >= 10) return std::to_string(1 << (level - 10)) + "KX";
    return std::to_string(1 << level) + "X";
}

std::string SampleEditorState::duration_display() const {
    if (sampleRate <= 0 || totalFrames <= 0) return "--:--.--";

    // The pending pitch is counted in: DURATION says what SAVE will bake.
    const double  ratio      = std::pow(2.0, pitchSemitones / 12.0);
    const int64_t effFrames  = std::max<int64_t>(static_cast<int64_t>(totalFrames / ratio), 1);
    const int64_t ms         = (effFrames * 1000) / sampleRate;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d.%02d", static_cast<int>(ms / 60000),
                  static_cast<int>((ms % 60000) / 1000), static_cast<int>((ms % 1000) / 10));
    return std::string(buf);
}

int SampleEditorState::source_bpm() const {
    if (sampleRate <= 0 || totalFrames <= 0) return 0;

    const double ratio   = std::pow(2.0, pitchSemitones / 12.0);
    const double seconds = static_cast<double>(totalFrames) / (ratio * sampleRate);
    if (seconds <= 0.0) return 0;

    const long bpm = std::lround(SampleEditorModule::duration_beats(durationIndex) * 60.0 / seconds);
    return (bpm < 1 || bpm > 999) ? 0 : static_cast<int>(bpm);
}

std::string SampleEditorState::bpm_display() const {
    const int bpm = source_bpm();
    return bpm > 0 ? std::to_string(bpm) + "BPM" : "---BPM";
}

void SampleEditorState::reset_stretch() {
    const SampleEditorState fresh;
    durationIndex  = fresh.durationIndex;
    stretchBpm     = fresh.stretchBpm;
    pitchSemitones = fresh.pitchSemitones;
    stretchGrainMs = fresh.stretchGrainMs;
}

bool SampleEditorState::fx_armed() const {
    if (fxType == SampleEditorModule::FX_EQ) return fxEqSlot >= 0;
    return fxType <= SampleEditorModule::FX_DRIVE && fxValue > 0;
}

int SampleEditorState::fx_param() const {
    return (fxType == SampleEditorModule::FX_EQ) ? fxEqSlot : fxValue;
}

// ─── The draw ────────────────────────────────────────────────────────────────────────────────────

namespace {

// Compact, consistent cards distinguish parameters from actions. Row IDs still belong to the
// existing dispatcher; these helpers only arrange and draw them.
void sample_button(Canvas& c, int x, int y, int w, const std::string& text,
                   bool active, const Theme& t, int scale = 3) {
    (void)scale;
    const Argb base = active ? t.rowCursor : t.rowEvery4th;
    c.panel_rect(x, y, w, 19, base, active ? t.rowCursor : with_alpha(t.textParam, .15f), 3);
    const auto label = Canvas::clip_label(text,w-12,12);
    c.draw_label(label,x+(w-Canvas::label_width(label,12))/2,y+3,
                 active?cursor_cell_ink(t):t.textValue,12);
}

void sample_field(Canvas& c, int x, int y, int w, const std::string& label,
                  const std::string& value, bool active, const Theme& t, int scale = 3) {
    c.panel_rect(x, y, w, 19, active ? t.rowCursor : t.rowEvery4th,
                 active ? t.rowCursor : with_alpha(t.textParam, .12f), 3);
    const auto ink = active ? cursor_cell_ink(t) : t.textValue;
    c.draw_label(label, x + 7, y + 3, active ? ink : t.textParam,12);
    const int labelW = Canvas::label_width(label,12);
    const int available = std::max(1, w - labelW - 22);
    const auto v = Canvas::clip_text(value, std::max(1, (available + 1) / (5 * scale + 1)));
    c.draw_text(v, x + w - 8 - Canvas::text_width(v, 1, scale),
                y + (19 - 5 * scale) / 2, ink, 1, scale);
}

void sample_group(Canvas& c, int x, int y, int h, const std::string& label,
                  bool active, const Theme& t) {
    c.panel_rect(x + 10, y - 2, 620, h, darken(t.rowEvery4th, .75f),
                 with_alpha(t.textParam, .13f), 5);
    c.fill_rect(x + 11, y + 3, 2, h - 10, with_alpha(active ? t.rowCursor : t.textParam, active ? .85f : .18f));
    c.draw_label(label, x + 19, y + 3, active ? t.rowCursor : t.textParam,12);
}

void draw_ops_row(Canvas& c, int x, int y, const std::vector<std::string>& ops, bool is_cur,
                  int cur_col, const Theme& t) {
    const int left = x + 85, width = 540, gap = 4;
    const int n = static_cast<int>(ops.size());
    for (int i = 0; i < n; ++i) {
        const int a = left + i * width / n;
        const int b = left + (i + 1) * width / n;
        sample_button(c, a, y, b - a - gap, ops[static_cast<size_t>(i)], is_cur && cur_col == i, t,
                      n >= 6 ? 2 : 3);
    }
}

}  // namespace

void SampleEditorModule::draw(Canvas& c, int x, int y, const SampleEditorState& s,
                              const Theme& t) const {
    c.set_surface_style(t.surfaceStyle);
    c.surface_rect(x, y, WIDTH, HEIGHT, t.background);

    // A quiet title and file statistics, followed by two three-column parameter rows.
    c.draw_label("Sample Editor", x + 12, y + 5, t.textTitle, 18);
    const std::string stats = s.bpm_display() + "  /  " + std::to_string(s.sampleRate) + "Hz  /  " + s.duration_display();
    c.draw_text(stats, x + 628 - Canvas::text_width(stats, 1, 2), y + 10, t.textParam, 1, 2);
    const int fieldX[] = {10, 219, 428};
    const std::string source = s.hasStereoData ? at(source_values(), s.sourceMode) : "MONO";
    const std::string pitch = (s.pitchSemitones >= 0 ? "+" : "") + std::to_string(s.pitchSemitones);
    const std::string labels[2][3] = {{"ZOOM", "SOURCE", "RATE"}, {"PITCH", "DURATION", "BIT"}};
    const std::string values[2][3] = {{s.zoom_display(), source, at(rate_values(), s.rateMode)},
                                    {pitch, at(duration_values(), s.durationIndex), std::to_string(s.bitDepth)}};
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 3; ++col)
            sample_field(c, x + fieldX[col], y + 28 + row * 22, 202, labels[row][col], values[row][col],
                         s.cursorRow == row + 1 && s.cursorCol == col, t);

    draw_waveform(c, x, y + WAVEFORM_Y, s, t);

    // TRIM and view tools share one panel; START/END remain exact sample-frame values.
    sample_group(c, x, y + content_y(8), 44, "TRIM", s.cursorRow >= 3 && s.cursorRow <= 9, t);
    sample_field(c, x + 85, y + content_y(8), 263, "START f", std::to_string(s.selectionStart),
                 s.cursorRow == 8 && s.cursorCol == 0, t, 2);
    sample_field(c, x + 352, y + content_y(8), 269, "END f", std::to_string(s.selectionEnd),
                 s.cursorRow == 8 && s.cursorCol == 1, t, 2);
    const std::string tools[] = {"FIT SEL", "FIT SL", "ALL", s.snapEnabled ? "SNAP ON" : "SNAP OFF",
                                 "GAIN " + std::to_string(1 << std::clamp(s.waveformGain, 0, 3)) + "X"};
    for (int i = 0; i < 5; ++i)
        sample_button(c, x + 85 + i * 108, y + content_y(9), 104, tools[i],
                      s.cursorRow == 9 && s.cursorCol == i, t, 2);

    sample_group(c, x, y + content_y(10), 65, "SLICE", s.cursorRow >= 10 && s.cursorRow <= 12, t);
    sample_field(c, x + 85, y + content_y(10), 319, "MODE", at(slice_methods(), s.sliceMethod),
                 s.cursorRow == 10 && s.cursorCol == 0, t);
    if (slice_has_parameter(s.sliceMethod)) {
        const std::string label = s.sliceMethod == SLICE_TRANSIENT ? "SENS" : "BY";
        const std::string value = hex2(s.sliceMethod == SLICE_TRANSIENT ? s.sliceSensitivity : s.sliceDivisions);
        sample_field(c, x + 408, y + content_y(10), 213, label, value,
                     s.cursorRow == 10 && s.cursorCol == 1, t);
    } else {
        c.draw_text(s.sliceMethod == SLICE_OFF ? "SLICING DISABLED" : "MANUAL MARKERS", x + 420,
                    y + content_y(10) + 5, t.textEmpty, 1, 2);
    }
    if (s.sliceMethod != SLICE_OFF) {
        std::string index = std::to_string(s.sliceIndex);
        if (s.sliceMethod == SLICE_TRANSIENT || s.sliceMethod == SLICE_MANUAL)
            index += " / " + std::to_string(s.slice_index_ceiling() + 1);
        sample_field(c, x + 85, y + content_y(11), 263, "INDEX", index,
                     s.cursorRow == 11 && s.cursorCol == 0, t);
        sample_field(c, x + 352, y + content_y(11), 269, "START f", std::to_string(s.effective_slice_position()),
                     s.cursorRow == 11 && s.cursorCol == 1, t, 2);
    } else {
        c.draw_text("Choose a slice mode to place markers", x + 94, y + content_y(11) + 5, t.textEmpty, 1, 2);
    }
    static const std::vector<std::string> sliceTools{"ADD @S", "REMOVE", "SEL SL", "SEL ALL"};
    draw_ops_row(c, x, y + content_y(12), sliceTools, s.cursorRow == 12, s.cursorCol, t);

    sample_group(c, x, y + content_y(13), 65, "EDIT", s.cursorRow >= 13 && s.cursorRow <= 15, t);
    draw_ops_row(c, x, y + content_y(13), ops_row1(), s.cursorRow == 13, s.cursorCol, t);
    draw_ops_row(c, x, y + content_y(14), ops_row2(), s.cursorRow == 14, s.cursorCol, t);
    draw_ops_row(c, x, y + content_y(15), ops_row3(), s.cursorRow == 15, s.cursorCol, t);

    sample_group(c, x, y + content_y(16), 23, "FX", s.cursorRow == 16, t);
    sample_button(c, x + 85, y + content_y(16), 141, at(fx_types(), s.fxType), s.cursorRow == 16 && s.cursorCol == 0, t);
    const auto fxValue = s.fxType == FX_EQ ? (s.fxEqSlot >= 0 ? hex2(s.fxEqSlot) + " >" : "-- >")
                        : s.fxType == FX_SYNC ? at(sync_types(), s.syncType) : hex2(s.fxValue);
    sample_button(c, x + 230, y + content_y(16), 256, fxValue, s.cursorRow == 16 && s.cursorCol == 1, t);
    sample_button(c, x + 490, y + content_y(16), 131, "APPLY", s.cursorRow == 16 && s.cursorCol == 2, t);

    // Name and file actions form the fixed footer, with the entire name safely clipped to its cell.
    sample_group(c, x, y + content_y(18), 44, "FILE", s.cursorRow >= 18, t);
    const auto filename = Canvas::clip_text(s.sampleName.empty() ? "UNTITLED" : s.sampleName, 45);
    c.panel_rect(x + 85, y + content_y(18), 536, 19, s.cursorRow == 18 ? t.rowCursor : t.rowEvery4th,
                 s.cursorRow == 18 ? t.rowCursor : with_alpha(t.textParam, .12f), 3);
    c.draw_text(filename, x + 93, y + content_y(18) + 5,
                s.cursorRow == 18 ? cursor_cell_ink(t) : t.textValue, 1, 2);
    if (s.isModified) c.draw_text("*", x + 607, y + content_y(18) + 3, s.cursorRow == 18 ? cursor_cell_ink(t) : t.rowCursor, 1, 2);
    const int count = s.sliceMethod == SLICE_OFF ? 3 : 4;
    const std::string fileOps[] = {"LOAD", "SAVE", "OVERWRITE", "CHOP"};
    for (int i = 0; i < count; ++i) {
        const int left = 85 + i * 540 / count;
        const int right = 85 + (i + 1) * 540 / count;
        sample_button(c, x + left, y + content_y(19), right - left - 4, fileOps[i],
                     s.cursorRow == 19 && s.cursorCol == i, t, 2);
    }

    if (s.showConfirmClose) draw_confirm_dialog(c, x, y, t);
}

void SampleEditorModule::draw_waveform(Canvas& c, int x, int y, const SampleEditorState& s,
                                       const Theme& t) const {
    const int wfLeft  = x + 10;
    const int wfRight = wfLeft + WAVEFORM_W;
    // The last column inside the panel. Marks test against the edge and draw here, so an
    // end-of-view mark shows.
    const int wfLast  = wfRight - 1;
    const int waveTop = y + 30;
    const int waveH = WAVEFORM_H - 58;
    const int midY = waveTop + waveH / 2;

    c.panel_rect(wfLeft, y, WAVEFORM_W, WAVEFORM_H, t.vizBackground,
                 with_alpha(t.textParam, .25f), 5);
    c.fill_rect(wfLeft + 1, waveTop, WAVEFORM_W - 2, waveH, darken(t.vizBackground, .84f));
    c.fill_rect(wfLeft + 1, midY, WAVEFORM_W - 2, 1, with_alpha(t.textParam, .22f));
    for (int i = 1; i < 8; ++i)
        c.fill_rect(wfLeft + i * WAVEFORM_W / 8, waveTop, 1, waveH,
                    with_alpha(t.textParam, i % 2 ? .045f : .10f));
    c.fill_rect(wfLeft + 1, waveTop + waveH / 4, WAVEFORM_W - 2, 1, with_alpha(t.textParam, .05f));
    c.fill_rect(wfLeft + 1, waveTop + waveH * 3 / 4, WAVEFORM_W - 2, 1, with_alpha(t.textParam, .05f));

    const int64_t viewStart = s.view_start();
    const int64_t viewLen   = std::max<int64_t>(s.view_end() - viewStart, 1);
    const double viewLenF = static_cast<double>(viewLen);

    // Frame → pixel, for every mark on the panel.
    auto frame_x = [&](double frame) {
        return static_cast<double>(wfLeft) +
               ((frame - static_cast<double>(viewStart)) / viewLenF) * WAVEFORM_W;
    };

    // Whole-file overview and the current viewport remain stable while the detail is zoomed.
    c.fill_rect(wfLeft + 4, y + 3, WAVEFORM_W - 8, 12, with_alpha(t.textParam, .06f));
    const int overviewBins=static_cast<int>(s.overviewData.size()/2);
    for(int i=0;i<overviewBins;++i){
        const float lo=std::clamp(s.overviewData[i*2],-1.f,1.f);
        const float hi=std::clamp(s.overviewData[i*2+1],-1.f,1.f);
        v_line(c,wfLeft+i*WAVEFORM_W/std::max(overviewBins,1),y+9-static_cast<int>(hi*5),
               std::max(1,static_cast<int>((hi-lo)*5)),with_alpha(t.textParam,.48f));
    }
    if(s.totalFrames>0){
        const int left=wfLeft+static_cast<int>(static_cast<double>(viewStart)*WAVEFORM_W/s.totalFrames);
        const int width=std::max(2,static_cast<int>(static_cast<double>(viewLen)*WAVEFORM_W/s.totalFrames));
        c.stroke_rect(left, y + 3, std::min(width, wfRight - left), 12, with_alpha(t.textTitle, .85f));
    }
    auto time_label=[&](int64_t frame){char b[32];
        std::snprintf(b,sizeof(b),"%.3fms",s.sampleRate>0?1000.0*frame/s.sampleRate:0.0);return std::string(b);};
    c.draw_text(time_label(viewStart), wfLeft + 7, y + 18, t.textParam, 1, 2);
    const auto endLabel=time_label(s.view_end());
    c.draw_text(endLabel,wfRight-Canvas::text_width(endLabel,1,2)-7,y+18,t.textParam,1,2);
    char detail[96];
    const int64_t selected=std::max<int64_t>(s.selectionEnd-s.selectionStart,0);
    std::snprintf(detail,sizeof(detail),"LEN %lldf / %.3fms   STEP %lldf",static_cast<long long>(selected),
                  s.sampleRate>0?1000.0*selected/s.sampleRate:0.0,static_cast<long long>(s.fine_step()));
    c.fill_rect(wfLeft + 1, y + WAVEFORM_H - 18, WAVEFORM_W - 2, 1, with_alpha(t.textParam, .13f));
    c.draw_text(Canvas::clip_text(s.toolMessage.empty()?std::string(detail):s.toolMessage,54),
                wfLeft + 7, y + WAVEFORM_H - 13, s.toolMessage.empty()?t.textParam:t.textTitle, 1, 2);


    // ── The wave itself: one vertical rule per bin, from its min to its max ──────────────────────
    if (s.waveformData.size() >= 2) {
        const int   bins  = static_cast<int>(s.waveformData.size()) / 2;
        const float halfH = waveH / 2.0f;
        const float gain=static_cast<float>(1 << std::clamp(s.waveformGain,0,3));
        int previousY=midY;
        for (int i = 0; i < bins; ++i) {
            const float minVal = s.waveformData[static_cast<size_t>(i) * 2];
            const float maxVal = s.waveformData[static_cast<size_t>(i) * 2 + 1];

            const int cx  = wfLeft + static_cast<int>(static_cast<float>(i) *
                                                      static_cast<float>(WAVEFORM_W) /
                                                      static_cast<float>(bins));
            const int top = std::clamp(midY - static_cast<int>(maxVal * halfH * gain),waveTop,waveTop+waveH);
            const int bot = std::clamp(midY - static_cast<int>(minVal * halfH * gain),waveTop,waveTop+waveH);

            // Bins inside the selection are lit: that is the selection; S/E only mark its edges.
            const int64_t binFrame = viewStart + (static_cast<int64_t>(i) * viewLen) / bins;
            const bool    inSel    = (binFrame >= s.selectionStart && binFrame < s.selectionEnd);
            const Argb ink=inSel?t.vizWave:t.textEmpty;
            v_line(c, cx, top, std::max(1,bot-top), ink);
            if(viewLen<=bins&&i>0)v_line(c,cx,std::min(previousY,top),std::abs(previousY-top)+1,ink);
            previousY=top;
        }
    } else {
        const std::string empty = s.totalFrames <= 0 ? "NO SAMPLE LOADED" : "WAVEFORM LOADING";
        c.draw_text(empty, wfLeft + (WAVEFORM_W - Canvas::text_width(empty, 1, 3)) / 2,
                    midY - 13, t.textParam, 1, 3);
        if (s.totalFrames <= 0) {
            const std::string hint = "Choose LOAD below to open a sample";
            c.draw_text(hint, wfLeft + (WAVEFORM_W - Canvas::text_width(hint, 1, 2)) / 2,
                        midY + 10, t.textEmpty, 1, 2);
        }
    }

    if (s.totalFrames <= 0) return;

    // ── The selection's two edges ────────────────────────────────────────────────────────────────
    {
        const float sXf = frame_x(static_cast<double>(s.selectionStart));
        const float eXf = frame_x(static_cast<double>(s.selectionEnd));
        // Test the unclamped position, draw the clamped one: an edge scrolled out must vanish.
        if (sXf >= static_cast<float>(wfLeft) && sXf <= static_cast<float>(wfRight)) {
            const int sX = std::clamp(static_cast<int>(sXf), wfLeft, wfLast);
            v_line(c, sX, waveTop, waveH, t.textTitle);
            const int labelX = std::min(sX + 2, wfLast - 15);
            c.fill_rect(labelX, waveTop + 2, 14, 14, t.textTitle);
            c.draw_text("S", labelX + 2, waveTop + 4, t.vizBackground, 1, 2);
        }
        if (eXf >= static_cast<float>(wfLeft) && eXf <= static_cast<float>(wfRight)) {
            const int eX = std::clamp(static_cast<int>(eXf), wfLeft, wfLast);
            v_line(c, eX, waveTop, waveH, t.textTitle);
            const int labelX = std::max(eX - 16, wfLeft + 1);
            c.fill_rect(labelX, waveTop + 2, 14, 14, t.textTitle);
            c.draw_text("E", labelX + 2, waveTop + 4, t.vizBackground, 1, 2);
        }
    }

    const bool onSliceRow = (s.cursorRow == 11);

    // The band behind the slice under the cursor, faint so the wave shows through.
    auto highlight_slice = [&](int64_t start, int64_t end) {
        const int sX = static_cast<int>(std::clamp(frame_x(static_cast<double>(start)), static_cast<double>(wfLeft), static_cast<double>(wfRight)));
        const int eX = static_cast<int>(std::clamp(frame_x(static_cast<double>(end)), static_cast<double>(wfLeft), static_cast<double>(wfRight)));
        if (eX > sX) c.fill_rect(sX, waveTop, eX - sX, waveH, with_alpha(t.rowCursor, 0.1f));
    };

    // A slice boundary. The two bounding the current slice draw dimmer — the established look.
    auto boundary = [&](int64_t frame, bool active) {
        const float mXf = frame_x(static_cast<double>(frame));
        if (mXf < static_cast<float>(wfLeft) || mXf > static_cast<float>(wfRight)) return;
        v_line(c, std::min(static_cast<int>(mXf), wfLast), waveTop, waveH,
               active ? t.textEmpty : t.textParam);
    };

    // ── The slice boundaries: the detector's (TRANSIENT) or the file's (OFF) ─────────────────────
    const int markerCount = s.marker_count();
    if (markerCount > 0) {
        if (onSliceRow) {
            int64_t start = 0, end = 0;
            s.slice_bounds(s.sliceIndex, start, end);
            highlight_slice(start, end);
        }
        for (int idx = 0; idx < markerCount; ++idx) {
            // Marker `idx` is the right edge of slice `idx` and the left of `idx + 1`.
            const bool active = onSliceRow && (idx == s.sliceIndex - 1 || idx == s.sliceIndex);
            boundary(s.marker_position(idx), active);
        }
    }

    // ⚠️ Only while DIVIDE is arithmetic: once a marker is nudged the list above is the boundaries.
    if (s.sliceMethod == SLICE_DIVIDE && s.sliceDivisions > 0 && markerCount == 0) {
        const int64_t div   = s.sliceDivisions;
        const int64_t total = static_cast<int64_t>(s.totalFrames);
        if (onSliceRow) {
            highlight_slice((static_cast<int64_t>(s.sliceIndex) * total) / div,
                            std::min((static_cast<int64_t>(s.sliceIndex + 1) * total) / div, total));
        }
        for (int64_t i = 1; i < div; ++i) {
            const bool active = onSliceRow && (i == s.sliceIndex || i == s.sliceIndex + 1);
            boundary((i * total) / div, active);
        }
    }

    // ── The playhead ────────────────────────────────────────────────────────────────────────────
    if (s.playbackPosition >= 0.0f) {
        const float playFrame = s.playbackPosition * static_cast<float>(s.totalFrames);
        const float mXf       = frame_x(playFrame);
        if (mXf >= static_cast<float>(wfLeft) && mXf <= static_cast<float>(wfRight))
            v_line(c, std::min(static_cast<int>(mXf), wfLast - 1), waveTop, waveH, t.vizWave,
                   /*thickness=*/2);
    }
}

void SampleEditorModule::draw_confirm_dialog(Canvas& c, int x, int y, const Theme& t) const {
    // Covers the editor. ⚠️ So it is not in `modal_backdrop_active`: the bars must not dim either.
    c.fill_rect(x, y, WIDTH, HEIGHT, t.background);

    draw_confirm_box(c, "ARE YOU SURE?", t);
}

// ─── The cursor ──────────────────────────────────────────────────────────────────────────────────

CursorContext SampleEditorModule::cursor_context(const SampleEditorState& s) const {
    // The confirm dialog owns every button while it is up.
    if (s.showConfirmClose) return cc::none();

    switch (s.cursorRow) {
        case 0: return cc::read_only();   // the title bar

        case 1:
            switch (s.cursorCol) {
                case 0: return cc::hex_byte(s.zoomLevel, 0, s.max_zoom());
                // A mono sample: the cell reads MONO and cannot change (read_only, not none).
                case 1: return s.hasStereoData
                                   ? cc::toggle_ternary(at(source_values(), s.sourceMode), source_values())
                                   : cc::read_only();
                case 2: return cc::toggle_ternary(at(rate_values(), s.rateMode), rate_values());
                default: return cc::none();
            }

        case 2:
            switch (s.cursorCol) {
                // PITCH ±24 carried as 0..48; handle_input un-biases. A+B = +0.
                case 0: return cc::hex_byte(s.pitchSemitones + 24, 0, 48, -1, false, false, false,
                                            /*def=*/24);
                case 1: return cc::toggle_ternary(at(duration_values(), s.durationIndex), duration_values());
                case 2: {
                    // An 8-bit sample has nowhere lower to go.
                    const std::vector<std::string> choices = bit_depth_choices(s.sourceBitDepth);
                    if (choices.size() < 2) return cc::read_only();
                    return cc::toggle_ternary(std::to_string(s.bitDepth), choices);
                }
                default: return cc::none();
            }

        // Rows 3..8: A+DPAD drags a selection edge — the dispatcher's nudge_selection_edge.
        case 3: case 4: case 5: case 6: case 7: case 8:
            return cc::none();

        case 10:
            switch (s.cursorCol) {
                case 0: return cc::toggle_ternary(at(slice_methods(), s.sliceMethod), slice_methods());
                case 1:
                    if (s.sliceMethod == SLICE_TRANSIENT) return cc::hex_byte(s.sliceSensitivity, 0, 255);
                    if (s.sliceMethod == SLICE_DIVIDE)    return cc::hex_byte(s.sliceDivisions, 0, 255);
                    return cc::none();   // OFF has no parameter
                default: return cc::none();
            }

        case 11:
            // Col 1, the position, has no cell: A+DPAD drags the marker (nudge_slice_marker).
            if (s.cursorCol == 0 && s.sliceMethod != SLICE_OFF)
                return cc::hex_byte(s.sliceIndex, 0, s.slice_index_ceiling());
            return cc::none();

        case 9: case 12: case 13: case 14: case 15:
            return cc::none();   // the op buttons: A fires them, there is nothing to step

        case 16:
            switch (s.cursorCol) {
                case 0: return cc::toggle_ternary(at(fx_types(), s.fxType), fx_types());
                case 1:
                    // An EQ SLOT, as every EQ cell: "--" is empty, A+RIGHT takes slot 00, A+B clears.
                    if (s.fxType == FX_EQ)
                        return cc::eq_slot(s.fxEqSlot);
                    if (s.fxType == FX_SYNC) return cc::toggle_ternary(at(sync_types(), s.syncType), sync_types());
                    return cc::hex_byte(s.fxValue, 0, 255);                            // an AMOUNT
                default: return cc::none();   // col 2 = APPLY, an action
            }

        case 18: case 19:
            return cc::none();   // NAME opens the keyboard; the save buttons are actions

        default:
            return cc::none();
    }
}

// ─── The edit ────────────────────────────────────────────────────────────────────────────────────

SampleEditorInputResult SampleEditorModule::handle_input(SampleEditorState& s,
                                                         const InputAction& action) const {
    SampleEditorInputResult r;

    // The EQ slot is the one cell here that deletes and inserts.
    if (s.cursorRow == 16 && s.cursorCol == 1 && s.fxType == FX_EQ) {
        r.modified = apply_optional_byte(s.fxEqSlot, 127, action);
        return r;
    }

    if (action.type != ActionType::SET_VALUE) return r;   // every other cell is a SET_VALUE cell
    const int v = action.value;

    switch (s.cursorRow) {
        case 1:
            switch (s.cursorCol) {
                case 0: s.set_zoom(v); r.modified = true; break;
                case 1: s.sourceMode = v;                   r.modified = true; break;
                case 2:
                    // Destructive: the module reports it, the dispatcher makes the engine calls.
                    if (v != s.rateMode) r.rateModeChanged = true;
                    s.rateMode = v;
                    r.modified = true;
                    break;
                default: break;
            }
            break;

        case 2:
            switch (s.cursorCol) {
                case 0: s.pitchSemitones = v - 24;  r.modified = true; break;   // un-bias (see cursor_context)
                case 1: s.durationIndex  = v;       r.modified = true; break;
                case 2: {
                    // Destructive like RATE.
                    const int bits = std::atoi(at(bit_depth_choices(s.sourceBitDepth), v).c_str());
                    if (bits != s.bitDepth) r.bitDepthChanged = true;
                    s.bitDepth = bits;
                    r.modified = true;
                    break;
                }
                default: break;
            }
            break;

        case 10:
            switch (s.cursorCol) {
                case 0:
                    s.sliceMethod = v;
                    // Clearing the detector's list makes the feed re-run it.
                    if (v == SLICE_TRANSIENT) s.transientMarkers.clear();
                    r.modified = true;
                    break;
                case 1:
                    if (s.sliceMethod == SLICE_TRANSIENT) {
                        s.sliceSensitivity = v;
                        s.transientMarkers.clear();   // re-detect at the new threshold
                    } else {
                        s.sliceDivisions = v;
                    }
                    r.modified = true;
                    break;
                default: break;
            }
            break;

        case 11:
            // Stepping the index selects that slice, so CROP/CHOP/preview act on the one in view.
            if (s.cursorCol == 0 && s.sliceMethod != SLICE_OFF) {
                int64_t start = 0, end = 0;
                s.slice_bounds(v, start, end);
                s.sliceIndex      = v;
                s.selectionStart  = start;
                s.selectionEnd    = end;
                r.modified        = true;
            }
            break;

        case 16:
            switch (s.cursorCol) {
                case 0: s.fxType = v; r.modified = true; break;
                case 1:
                    if (s.fxType == FX_SYNC) s.syncType = v;
                    else                     s.fxValue  = v;
                    r.modified = true;
                    break;
                default: break;
            }
            break;

        default:
            break;
    }

    return r;
}

// ─── The TSTRETCH box ────────────────────────────────────────────────────────────────────────────

namespace {

// Under the waveform, so the playhead of a preview runs in view above the box.
constexpr int STRETCH_BOX_W   = 320;
constexpr int STRETCH_BOX_X   = (DESIGN_W - STRETCH_BOX_W) / 2;
constexpr int STRETCH_BOX_Y   = SampleEditorModule::WAVEFORM_Y + SampleEditorModule::WAVEFORM_H + 8;
constexpr int STRETCH_PAD     = 8;
constexpr int STRETCH_GAP     = 8;    // between the title and the lines
constexpr int STRETCH_LABEL_X = STRETCH_BOX_X + 14;
constexpr int STRETCH_VALUE_X = STRETCH_LABEL_X + 10 * CHAR_W;

/** One line of the box: a label and the cell beside it. No cell = a reading, no label = a blank line. */
struct StretchLine {
    const char* label;
    int         cell;   // a StretchCell, or −1
};
constexpr StretchLine STRETCH_LINES[] = {
    {"ORIG BPM", -1},
    {"DURATION", static_cast<int>(StretchCell::DURATION)},
    {"NEW BPM",  static_cast<int>(StretchCell::BPM)},
    {"PITCH",    static_cast<int>(StretchCell::PITCH)},
    {nullptr,    -1},
    {"GRAIN",    static_cast<int>(StretchCell::GRAIN)},
    {nullptr,    -1},
};
constexpr int STRETCH_LINE_COUNT = static_cast<int>(sizeof(STRETCH_LINES) / sizeof(STRETCH_LINES[0]));

// The title, air, the lines, then the PREVIEW / APPLY line.
constexpr int STRETCH_BOX_H =
    ROW_HEIGHT + STRETCH_GAP + (STRETCH_LINE_COUNT + 1) * ROW_HEIGHT + 2 * STRETCH_PAD;
static_assert(STRETCH_BOX_Y + STRETCH_BOX_H + MODAL_BORDER - 1 <= DESIGN_H,
              "the TSTRETCH box runs off the bottom of the screen");

std::string pad3(int v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%03d", std::clamp(v, 0, 999));
    return std::string(buf);
}

/** PITCH as row 2 shows it: "+0", "+7", "-12". */
std::string signed_semitones(int v) { return (v >= 0 ? "+" : "") + std::to_string(v); }

std::string stretch_value(const SampleEditorState& s, StretchCell cell, int song_tempo) {
    switch (cell) {
        case StretchCell::DURATION: return at(SampleEditorModule::duration_values(), s.durationIndex);
        case StretchCell::BPM:      return pad3(s.stretch_bpm(song_tempo));
        case StretchCell::PITCH:    return signed_semitones(s.pitchSemitones);
        case StretchCell::GRAIN:    return hex2(s.stretchGrainMs);
        default:                    return "";
    }
}

}  // namespace

StretchCell stretch_step(StretchCell from, int d_row, int d_col) {
    const bool onButtons = (from == StretchCell::PREVIEW || from == StretchCell::APPLY);
    if (d_col != 0 && onButtons) return (d_col > 0) ? StretchCell::APPLY : StretchCell::PREVIEW;
    if (d_row == 0) return from;
    if (onButtons) return (d_row < 0) ? StretchCell::GRAIN : from;
    return static_cast<StretchCell>(std::clamp(static_cast<int>(from) + d_row,
                                               static_cast<int>(StretchCell::DURATION),
                                               static_cast<int>(StretchCell::PREVIEW)));
}

CursorContext stretch_cursor_context(const SampleEditorState& s, int song_tempo) {
    using M = SampleEditorModule;
    switch (s.stretchCursor) {
        case StretchCell::DURATION:
            return cc::toggle_ternary(at(M::duration_values(), s.durationIndex), M::duration_values());
        case StretchCell::BPM: {
            // The song TEMPO's own range and steps; A+B goes back to following it.
            CursorContext c = cc::hex_byte(s.stretch_bpm(song_tempo), 20, 999, -1, false, false, false,
                                           /*def=*/song_tempo);
            c.largeStep = 10;
            return c;
        }
        case StretchCell::PITCH:
            // Row 2's own cell and bias; here A+B takes it back to +0.
            return cc::hex_byte(s.pitchSemitones + 24, 0, 48, -1, false, false, false, /*def=*/24);
        case StretchCell::GRAIN:
            return cc::hex_byte(s.stretchGrainMs, 5, 255, -1, false, false, false, /*def=*/40);
        default:
            return cc::none();   // PREVIEW and APPLY are actions
    }
}

void stretch_handle_input(SampleEditorState& s, const InputAction& action, int song_tempo) {
    if (action.type != ActionType::SET_VALUE) return;
    const int v = action.value;
    switch (s.stretchCursor) {
        case StretchCell::DURATION: s.durationIndex = v; break;
        // ⚠️ The song's own tempo is stored as 0, so the cell follows the song rather than remembering it.
        case StretchCell::BPM:      s.stretchBpm = (v == song_tempo) ? 0 : v; break;
        case StretchCell::PITCH:    s.pitchSemitones = v - 24; break;
        case StretchCell::GRAIN:    s.stretchGrainMs = v; break;
        default: break;
    }
}

void draw_stretch_editor(Canvas& c, const SampleEditorState& s, int song_tempo, const Theme& t) {
    if (!s.stretchOpen) return;

    c.reset_clip();
    draw_modal_backdrop(c);
    c.panel_rect(STRETCH_BOX_X + 3, STRETCH_BOX_Y + 4, STRETCH_BOX_W, STRETCH_BOX_H,
                 0x88000000, 0x00000000, 7);
    c.panel_rect(STRETCH_BOX_X, STRETCH_BOX_Y, STRETCH_BOX_W, STRETCH_BOX_H,
                 t.background, with_alpha(t.textTitle, .70f), 7);
    c.fill_rect(STRETCH_BOX_X + 12, STRETCH_BOX_Y + ROW_HEIGHT + STRETCH_PAD + 2,
                STRETCH_BOX_W - 24, 1, with_alpha(t.textParam, .18f));

    const std::string title = "TIMESTRETCH";
    int ty = STRETCH_BOX_Y + STRETCH_PAD + TEXT_PADDING;
    c.draw_text(title, STRETCH_BOX_X + (STRETCH_BOX_W - static_cast<int>(title.size()) * CHAR_W) / 2,
                ty, t.textTitle, CHAR_SPACING, FONT_SCALE);
    ty += ROW_HEIGHT + STRETCH_GAP;

    auto on = [&](StretchCell cell) { return s.stretchCursor == cell; };

    for (const StretchLine& line : STRETCH_LINES) {
        if (line.label != nullptr) {
            const bool        isCell = (line.cell >= 0);
            const StretchCell cell   = static_cast<StretchCell>(std::max(line.cell, 0));
            const bool        cur    = isCell && on(cell);
            c.draw_text(line.label, STRETCH_LABEL_X, ty, cur ? cursor_mark_ink(t) : t.textParam,
                        CHAR_SPACING, FONT_SCALE);
            if (isCell) {
                draw_cursor_cell(c, stretch_value(s, cell, song_tempo), STRETCH_VALUE_X, ty, cur,
                                 t.textValue, t);
            } else {
                // The header's reading: the sample's tempo at this DURATION, the PITCH below counted in.
                const int bpm = s.source_bpm();
                c.draw_text(bpm > 0 ? pad3(bpm) : "---", STRETCH_VALUE_X, ty, t.textParam, CHAR_SPACING,
                            FONT_SCALE);
            }
        }
        ty += ROW_HEIGHT;
    }

    draw_cursor_cell(c, "PREVIEW", STRETCH_LABEL_X, ty, on(StretchCell::PREVIEW), t.textValue, t);
    draw_cursor_cell(c, "APPLY",   STRETCH_VALUE_X, ty, on(StretchCell::APPLY),   t.textValue, t);
}

}  // namespace pt::ui
