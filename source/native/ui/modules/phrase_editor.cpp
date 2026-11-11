#include "ui/modules/phrase_editor.h"

#include "songcore/scales.h"
#include "ui/helpers.h"
#include "ui/modules/oscilloscope.h"
#include "ui/step_grid.h"

#include <algorithm>
#include <iterator>

namespace pt::ui {

using songcore::Note;
using songcore::PhraseStep;

namespace {
bool same_rows(const PhraseEditHistory::Rows& a, const PhraseEditHistory::Rows& b) {
    return std::equal(a.begin(), a.end(), b.begin(), [](const PhraseStep& x, const PhraseStep& y) {
        return x.note == y.note && x.instrument == y.instrument && x.volume == y.volume &&
               x.fx1Type == y.fx1Type && x.fx1Value == y.fx1Value &&
               x.fx2Type == y.fx2Type && x.fx2Value == y.fx2Value &&
               x.fx3Type == y.fx3Type && x.fx3Value == y.fx3Value;
    });
}

int overview_pitch(const Note& note) {
    if (note.pitch < 0 || note.pitch > 11 || note.octave < -1 || note.octave > 9) return -1;
    return songcore::note_to_midi(note);
}
} // namespace

std::optional<PhraseEditHistory::Rows> PhraseEditHistory::snapshot(const songcore::Phrase& phrase) {
    if (phrase.steps.size() != songcore::PHRASE_ROWS) return std::nullopt;
    Rows rows;
    std::copy(phrase.steps.begin(), phrase.steps.end(), rows.begin());
    return rows;
}

void PhraseEditHistory::record(const Rows& before, const songcore::Phrase& phrase) {
    const auto after = snapshot(phrase);
    if (!after || same_rows(before, *after)) return;
    if (phraseId_ != phrase.id) clear();
    if (size_) {
        const auto& expected = cursor_ ? edits_[cursor_ - 1].after : edits_[0].before;
        if (!same_rows(before, expected)) clear();
    }
    phraseId_ = phrase.id;
    if (cursor_ == CAPACITY) {
        std::move(edits_.begin() + 1, edits_.end(), edits_.begin());
        --cursor_;
    }
    edits_[cursor_++] = {before, *after};
    size_ = cursor_;
}

int PhraseEditHistory::available(int phraseId, bool redo) const {
    if (phraseId != phraseId_) return 0;
    return redo ? size_ - cursor_ : cursor_;
}

PhraseEditHistory::Result PhraseEditHistory::apply(songcore::Phrase& phrase, bool redo) {
    if (!available(phrase.id, redo)) return Result::EMPTY;
    const auto current = snapshot(phrase);
    const auto& edit = edits_[cursor_ - (redo ? 0 : 1)];
    const auto& expected = redo ? edit.before : edit.after;
    if (!current || !same_rows(*current, expected)) {
        clear();
        return Result::DIVERGED;
    }
    const auto& restored = redo ? edit.after : edit.before;
    std::copy(restored.begin(), restored.end(), phrase.steps.begin());
    cursor_ += redo ? 1 : -1;
    return Result::APPLIED;
}

void PhraseEditHistory::clear() {
    phraseId_ = -1;
    cursor_ = size_ = 0;
}

void PhraseEditorModule::draw_overview(Canvas& c, int x, int y, const PhraseOverviewState& s) const {
    const Theme& t = s.theme;
    constexpr int width = OscilloscopeModule::WIDTH, height = OscilloscopeModule::HEIGHT;
    constexpr int slotWidth = 28, slots = songcore::PHRASE_ROWS;
    constexpr int plotWidth = slots * slotWidth;
    const int left = x + 10, top = y + 28, baseline = y + 61;
    const bool velocity = s.view == PhraseOverview::VELOCITY;
    c.panel_rect(x, y, width, height, t.vizBackground, with_alpha(t.textParam,.18f), 4);

    std::array<int, slots> notes;
    notes.fill(-1);
    std::array<int, slots> playing{};
    const int count = static_cast<int>(std::min<size_t>(slots, s.phrase.steps.size()));
    int low = 132, high = -1;
    for (int step = 0; step < count; ++step) {
        notes[step] = overview_pitch(s.phrase.steps[step].note);
        if (notes[step] < 0) continue;
        low = std::min(low, notes[step]);
        high = std::max(high, notes[step]);
    }
    for (const auto& head : s.playheads)
        if (head.phraseId == s.phrase.id && head.step >= 0 && head.step < slots) ++playing[head.step];
    const int hop = songcore::phrase_certain_hop_row(s.phrase);
    const int midpoint = (low + high) / 2;
    // Give a repeated note or a very narrow melody some headroom in the same viewport.
    const int range = std::max(12, high - low);
    const int bottomPitch = range == 12 ? midpoint - 6 : low;

    for (int step = 0; step < slots; ++step) {
        const int col = left + step * slotWidth;
        const int mid = col + slotWidth / 2;
        if ((step / 4) % 2 == 0)
            c.fill_rect(col, top, slotWidth, baseline - top + 2, with_alpha(t.rowEvery4th,.52f));
        if (s.selectedFirst >= 0 && step >= s.selectedFirst && step <= s.selectedLast)
            c.fill_rect(col, top, slotWidth, baseline - top + 2, with_alpha(t.rowSelection,.22f));
        if (step % 4 == 0)
            c.fill_rect(col, top, 1, baseline - top + 2, with_alpha(t.textParam,.48f));
        if (playing[step]) {
            c.fill_rect(mid - 3, top, 7, 2, t.textPlayhead);
            c.fill_rect(mid - 1, top + 2, 3, std::min(playing[step] + 1, 4), t.textPlayhead);
        }
    }
    for (int guide : {top + 9, top + 17, top + 25, baseline + 1})
        c.fill_rect(left, guide, plotWidth, 1, with_alpha(t.textParam,.22f));

    int previousX = -1, previousY = -1;
    for (int step = 0; step < slots; ++step) {
        const int mid = left + step * slotWidth + slotWidth / 2;
        const bool reached = hop < 0 || step < hop;
        const Argb ink = reached ? t.vizWave : with_alpha(t.vizWave,.36f);
        if (notes[step] < 0) {
            if (step < count && songcore::note_is_off(s.phrase.steps[step].note)) {
                c.line(mid - 3, baseline - 5, mid + 3, baseline, with_alpha(t.textTitle,reached?.88f:.38f));
                c.line(mid + 3, baseline - 5, mid - 3, baseline, with_alpha(t.textTitle,reached?.88f:.38f));
            } else {
                c.fill_rect(mid - 1, baseline, 3, 1, with_alpha(t.textEmpty,.6f));
            }
            previousX = -1;
        } else if (velocity) {
            const int value = std::clamp(s.phrase.steps[step].volume, 0, 127);
            const int bar = std::max(2, value * 24 / 127);
            c.fill_rect(mid - 6, baseline - bar + 1, 13, bar, ink);
        } else {
            const int point = baseline - 3 - std::clamp(notes[step] - bottomPitch, 0, range) * 22 / range;
            if (reached && previousX >= 0)
                c.line(previousX, previousY, mid, point, with_alpha(t.vizWave,.48f));
            c.fill_rect(mid - 5, point - 1, 11, 3, ink);
            previousX = reached ? mid : -1;
            previousY = point;
        }
    }

    const int cursor = std::clamp(s.cursorRow, 0, slots - 1);
    c.stroke_rect(left + cursor * slotWidth + 1, top + 6, slotWidth - 2, baseline - top - 3, t.rowCursor, 2);
    std::string readout = velocity ? "VELOCITY" : "NOTES";
    readout += "  " + hex1(cursor) + "  ";
    if (cursor < count && songcore::note_is_off(s.phrase.steps[cursor].note)) readout += "OFF";
    else if (notes[cursor] >= 0)
        readout += velocity ? hex2(std::clamp(s.phrase.steps[cursor].volume,0,127))
                            : songcore::note_name(s.phrase.steps[cursor].note);
    else readout += "---";
    // Status and the two clipboard/selection lines retain the upper portion of the strip.
    c.draw_label(Canvas::clip_label(readout,140,12), x + width - 150, y + 53, t.textValue,12);
}

void PhraseEditorModule::draw(Canvas& c, int x, int y, const PhraseEditorState& s) const {
    const Theme& t = s.theme;

    c.surface_rect(x, y, WIDTH, HEIGHT, t.background);
    c.panel_rect(x, y, WIDTH, ROW_HEIGHT + 3, t.rowEvery4th,
                 with_alpha(t.textParam, .18f), 4);

    // Column x: a 2-char cell, then a 10px gutter.
    int       colX      = x + 10;
    const int stepX     = colX; colX += 30 + 10;
    const int noteX     = colX; colX += 45 + 20;
    const int volX      = colX; colX += 30 + 15;
    const int instX     = colX; colX += 30 + 15;
    const int fx1NameX  = colX; colX += 45 + 10;
    const int fx1ValueX = colX; colX += 30 + 15;
    const int fx2NameX  = colX; colX += 45 + 10;
    const int fx2ValueX = colX; colX += 30 + 15;
    const int fx3NameX  = colX; colX += 45 + 10;
    const int fx3ValueX = colX;

    int rowY = y + TEXT_PADDING;
    c.draw_text("PHRASE " + hex2(s.phrase.id), x + 10, rowY, t.textTitle, CHAR_SPACING, FONT_SCALE);
    // The chain row and its song cell (PhraseEditorState).
    if (s.viaChain >= 0 && s.viaChainRow >= 0) {
        std::string via = "C" + hex2(s.viaChain) + "/" + hex1(s.viaChainRow);
        if (s.songRow >= 0 && s.songTrack >= 0)
            via += "  S" + hex2(s.songRow) + " T" + std::to_string(s.songTrack + 1);
        c.draw_text(via, x + 190, rowY, t.textParam, CHAR_SPACING, FONT_SCALE);
    }

    // Keep the fixed grid for cells. The proportional labels fit full field names in the same columns.
    rowY = y + ROW_HEIGHT + 14 + TEXT_PADDING + 1;
    const int cc = s.cursorColumn;
    c.draw_label("STEP", stepX, rowY, header_color(cc, 0, 0, t));
    c.draw_label("NOTE", noteX, rowY, header_color(cc, 1, 1, t));
    c.draw_label("VOL", volX, rowY, header_color(cc, 2, 2, t));
    c.draw_label("INST", instX, rowY, header_color(cc, 3, 3, t));
    c.draw_label("FX 1", fx1NameX, rowY, header_color(cc, 4, 5, t));
    c.draw_label("FX 2", fx2NameX, rowY, header_color(cc, 6, 7, t));
    c.draw_label("FX 3", fx3NameX, rowY, header_color(cc, 8, 9, t));

    // An AUS/AUF cell no ramp uses draws dimmed — the emitter's own pairing over every chain row that
    // plays this phrase, asked once per draw.
    songcore::RampCells rampCells;
    if (s.project) rampCells = songcore::find_ramp_cells(*s.project, s.phrase.id);
    else           rampCells.mark(songcore::find_ramps(s.phrase));

    // Rows a certain HOP cuts off draw dimmed.
    const int hopRow = songcore::phrase_certain_hop_row(s.phrase);

    for (int i = 0; i < static_cast<int>(s.phrase.steps.size()); ++i) {
        draw_row(c, x, y, i, s.phrase.steps[static_cast<size_t>(i)], s, rampCells, hopRow, stepX, noteX,
                 volX, instX, fx1NameX, fx1ValueX, fx2NameX, fx2ValueX, fx3NameX, fx3ValueX);
    }
}

void PhraseEditorModule::draw_row(Canvas& c, int x, int y, int index, const PhraseStep& step,
                                  const PhraseEditorState& s,
                                  const songcore::RampCells& rampCells, int hopRow, int stepX,
                                  int noteX, int volX, int instX, int fx1NameX, int fx1ValueX,
                                  int fx2NameX, int fx2ValueX, int fx3NameX, int fx3ValueX) const {
    const Theme& t = s.theme;

    // The HOP's own row plays nothing, but its HOP cell acts, so it stays lit.
    const bool cutOff = hopRow >= 0 && index >= hopRow;

    const int dataRowY = y + ROW_HEIGHT + 14 + ROW_HEIGHT + (index * ROW_HEIGHT);

    c.fill_rect(x, dataRowY, WIDTH, ROW_HEIGHT, row_bg_color(index, t));
    draw_step_grid(c, t, t.phraseGrid, index, x, dataRowY, WIDTH, ROW_HEIGHT,
                   {noteX - 6, fx1NameX - 8, fx2NameX - 8, fx3NameX - 8});
    if (index == s.cursorRow)
        c.fill_rect(x + 2, dataRowY + 4, 3, ROW_HEIGHT - 8, t.rowCursor);

    const int textY = dataRowY + TEXT_PADDING;

    // Left to right: RowCells joins a selected run across the gutters only in layout order.
    RowCells cells(c, textY, t);

    const auto cur = [&](int col) { return index == s.cursorRow && s.cursorColumn == col; };
    const auto sel = [&](int col) { return s.selectionMode && s.isCellSelected(index, col); };
    const auto draw_cell = [&](std::string value, int cellX, int col, bool dim, Argb ink,
                               const char* placeholder = nullptr) {
        const bool focused = cur(col), selected = sel(col);
        // The placeholder occupies the original cell width. Selection still joins across gutters.
        if (placeholder && !focused && !selected) value = placeholder;
        cells.cell(value, cellX, focused, selected, dim, ink);
    };

    // Every 4th row number is brighter (a beat). Column 0 is a real cursor position.
    const Argb stepColor = (index == s.cursorRow) ? cursor_mark_ink(t)
                           : (index % 4 == 0 && !cutOff) ? t.textParam
                                                         : t.textEmpty;
    cells.cell(hex1(index), stepX, cur(0), /*is_selected=*/false, /*is_empty=*/false, stepColor);

    // Playback uses an arrow beside STEP; the edit cursor has a tick at the far left and a cell fill.
    if (std::any_of(std::begin(s.playheads), std::end(s.playheads), [&](const TrackPlayhead& ph) {
            return ph.phraseId == s.phrase.id && ph.step == index;
        }))
        draw_playhead(c, stepX + CHAR_W, textY, t);

    // An OFF plays nothing: its V and I dim.
    const PhraseStep defaults;
    const bool emptyNote = step.note == Note::EMPTY();
    const bool hasNote = songcore::step_has_note(step);
    draw_cell(note_name(step.note), noteX, 1, cutOff || emptyNote, t.textValue, emptyNote ? " - " : nullptr);
    draw_cell(hex2(step.volume), volX, 2, cutOff || !hasNote, t.textParam,
              !hasNote && step.volume == defaults.volume ? "  " : nullptr);
    draw_cell(hex2(step.instrument), instX, 3, cutOff || !hasNote, t.textParam,
              !hasNote && step.instrument == defaults.instrument ? "  " : nullptr);

    // A pair dims when unset, an AUS/AUF when no ramp uses it.
    const auto fxDim = [&](int type, int slot) {
        if (cutOff && !(index == hopRow && type == songcore::FX_HOP)) return true;
        return type == 0x00 || !rampCells.active(type, index, slot);
    };

    const auto draw_fx = [&](int slot, int nameX, int valueX) {
        const int type = songcore::step_fx_type(step, slot);
        const int value = songcore::step_fx_value(step, slot);
        const int typeColumn = 2 + slot * 2;
        const bool empty = type == songcore::FX_NONE;
        const bool dim = fxDim(type, slot);
        draw_cell(effect_name(type), nameX, typeColumn, dim, t.textValue, empty ? "   " : nullptr);
        draw_cell(hex2(value), valueX, typeColumn + 1, dim, t.textParam,
                  empty && value == 0 ? "  " : nullptr);
    };
    draw_fx(1, fx1NameX, fx1ValueX);
    draw_fx(2, fx2NameX, fx2ValueX);
    draw_fx(3, fx3NameX, fx3ValueX);
}

CursorContext PhraseEditorModule::cursor_context(const PhraseEditorState& s) const {
    const PhraseStep& step = s.phrase.steps[static_cast<size_t>(s.cursorRow)];
    switch (s.cursorColumn) {
        case 0: return cc::read_only();
        case 1: {
            // An OFF has no pitch: A+B clears it, nothing else.
            if (songcore::note_is_off(step.note)) {
                CursorContext off = cc::note(0);
                off.capabilities           = {};
                off.capabilities.canDelete = true;
                return off;
            }
            const bool isEmpty = (step.note == Note::EMPTY());
            // ⚠️ This quantizes typing, not the song: `step.note` is never rewritten. The scale is
            // the one the phrase's own SCA/SCG cells above put this row in. An exempt instrument
            // (sliced, or TSP off) is offered every note.
            unsigned mask = 0x0FFFu;
            int      key  = 0;
            if (s.project != nullptr) {
                const songcore::NoteScale ns = songcore::phrase_note_scale(
                    *s.project, s.phrase, s.cursorRow, isEmpty ? s.insertInstrument : step.instrument);
                if (ns.scale != nullptr) {
                    mask = songcore::scale_mask(*ns.scale);
                    key  = ns.key;
                }
            }
            return cc::note(isEmpty ? 0 : songcore::note_to_midi(step.note), isEmpty, mask, key);
        }
        case 2: return cc::volume(step.volume);
        case 3: return cc::instrument(step.instrument);
        case 4: return cc::effect_type(step.fx1Type, 1, s.effectTypeCount);
        case 5: return cc::effect_value(step.fx1Value, 1, effect_value_max(step.fx1Type));
        case 6: return cc::effect_type(step.fx2Type, 2, s.effectTypeCount);
        case 7: return cc::effect_value(step.fx2Value, 2, effect_value_max(step.fx2Type));
        case 8: return cc::effect_type(step.fx3Type, 3, s.effectTypeCount);
        case 9: return cc::effect_value(step.fx3Value, 3, effect_value_max(step.fx3Type));
        default: return cc::none();
    }
}

PhraseInputResult PhraseEditorModule::handle_input(songcore::Phrase& phrase, int cursor_row,
                                                   int cursor_column,
                                                   const InputAction& action) const {
    PhraseStep& step = phrase.steps[static_cast<size_t>(cursor_row)];
    PhraseInputResult r;

    switch (action.type) {
        case ActionType::SET_VALUE:
            switch (cursor_column) {
                case 1:
                    step.note        = songcore::note_from_midi(action.value);
                    r.hasNote        = true;
                    r.lastEditedNote = step.note;
                    break;
                case 2:
                    step.volume        = action.value;
                    r.hasVolume        = true;
                    r.lastEditedVolume = action.value;
                    break;
                case 3:
                    step.instrument        = action.value;
                    r.hasInstrument        = true;
                    r.lastEditedInstrument = action.value;
                    break;
                // The type columns store an index into EFFECT_TYPES; the code is what the step keeps.
                case 4: step.fx1Type  = songcore::effect_type_at(action.value); break;
                case 5: step.fx1Value = action.value; break;
                case 6: step.fx2Type  = songcore::effect_type_at(action.value); break;
                case 7: step.fx2Value = action.value; break;
                case 8: step.fx3Type  = songcore::effect_type_at(action.value); break;
                case 9: step.fx3Value = action.value; break;
                default: break;
            }
            break;

        case ActionType::DELETE:
            switch (cursor_column) {
                case 1: step.note = Note::EMPTY(); break;
                case 4: clear_effect(step, 1); break;
                case 6: clear_effect(step, 2); break;
                case 8: clear_effect(step, 3); break;
                default: break;
            }
            break;

        case ActionType::INSERT_DEFAULT:
            if (cursor_column == 1) {
                step.note        = Note::C4();
                r.hasNote        = true;
                r.lastEditedNote = step.note;
            }
            break;

        default:
            break;
    }

    r.modified = (action.type != ActionType::NONE);
    return r;
}

}  // namespace pt::ui
