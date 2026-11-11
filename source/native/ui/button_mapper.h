#pragma once

// The combo matrix: a button event plus the modifiers held at that instant → one named handler.
// A template so a test can drive it with a recording stub; in the app `Dispatcher` is `InputDispatcher`.

#include "ui/buttons.h"

#include <cstdint>

namespace pt::ui {

// The mapper's memory between events.
struct MapperState {
    uint64_t lastAPress = 0;   // the A,A window

    // A went down on a cell whose A opens something but whose A+DPAD / A+B means something else: the
    // open waits for A's release, and any A-combo meanwhile cancels it.
    bool aPressedAlone = false;

    // The same for B where it both closes and is a modifier (the EQ editor's slot cycle).
    bool bPressedAlone = false;

    // R+A / R+B changed the mix on the press; R released first keeps it, A/B first reverts it.
    bool rComboArmed = false;

    // SELECT went down alone; any other press clears it, so help fires on release only for a bare tap
    // (on the file browser SELECT is a modifier).
    bool selectPressedAlone = false;
    uint16_t helpHeldButtons = 0;

    bool owns_help_key(Button button) const {
        return (helpHeldButtons & (uint16_t{1} << static_cast<unsigned>(button))) != 0;
    }

    // START can claim the two shoulders for resampling before a plain L+R clears anything.
    bool shoulderClearPending = false;

    // Phrase SELECT chords consume each direction until its physical release.
    uint8_t phraseVerticalHeld = 0, phraseSelectHeld = 0;
    Button phraseFirstArrow = Button::COUNT;
    Button phraseRotationKey = Button::COUNT;
    bool phraseRotationArmed = false;

    void clear_phrase_keys() {
        phraseVerticalHeld = phraseSelectHeld = 0;
        phraseFirstArrow = Button::COUNT;
    }
};

// ⚠️ The order of the checks is the specification: most specific first, each arm returns. L+B+A before
// L+A, A+B before A, R+A / R+B before A and B.
// ⚠️ Modifiers come from the event, never a poll: a frame's events arrive at once, and a poll would
// see the frame's end (B-then-A in one frame would delete on the B press).
template <class Dispatcher>
void handle_button(const ButtonEvent& e, Dispatcher& d, MapperState& ms, uint64_t now) {
    const ButtonMods& m = e.mods;
    const uint8_t vertical = e.button == Button::DPAD_UP ? 1u : e.button == Button::DPAD_DOWN ? 2u : 0u;
    const uint8_t horizontal = e.button == Button::DPAD_LEFT ? 4u : e.button == Button::DPAD_RIGHT ? 8u : 0u;
    const uint8_t direction = vertical | horizontal;
    const bool repeatedVertical = (ms.phraseVerticalHeld & vertical) != 0;
    if (e.action == ButtonAction::RELEASED) ms.phraseSelectHeld &= ~direction;
    if (vertical && e.action == ButtonAction::PRESSED) {
        ms.phraseVerticalHeld |= vertical;
    } else if (vertical) {
        ms.phraseVerticalHeld &= ~vertical;
        if (ms.phraseFirstArrow == e.button) ms.phraseFirstArrow = Button::COUNT;
    } else if (e.action == ButtonAction::PRESSED && e.button != Button::SELECT) {
        ms.phraseFirstArrow = Button::COUNT;
    }

    // Help owns a press until its physical release, even if a different button closes it first.
    if (d.help_full_open() || ms.owns_help_key(e.button)) {
        const auto bit = uint16_t(1u << static_cast<unsigned>(e.button));
        ms.selectPressedAlone = false;
        if (e.action == ButtonAction::PRESSED && !e.cancelled) {
            ms.helpHeldButtons |= bit;
        } else {
            const bool owned = ms.owns_help_key(e.button);
            ms.helpHeldButtons &= ~bit;
            if (e.button == Button::A) d.on_a_held(false);
            if (e.button == Button::R_SHIFT) d.on_r_held(false);
            if (owned && !e.cancelled && d.help_full_open()) d.on_help_button(e.button);
        }
        return;
    }

    // R's state for MIDI learn ("hold R, turn a knob"). ⚠️ Above every early return, or R could stay held.
    if (e.button == Button::R_SHIFT) d.on_r_held(e.action == ButtonAction::PRESSED);
    // A press with R held is a combo (R+DPAD switches screens): that hold never learns.
    else if (e.action == ButtonAction::PRESSED && m.r) d.on_r_combo_press();
    // A's state for what is drawn while it is held. Above every early return too.
    if (e.button == Button::A) d.on_a_held(e.action == ButtonAction::PRESSED);

    if (e.cancelled) {
        ms.clear_phrase_keys();
        ms.phraseRotationArmed = false;
        if (ms.phraseRotationKey == e.button) ms.phraseRotationKey = Button::COUNT;
        if (e.button == Button::L_SHIFT || e.button == Button::R_SHIFT) ms.shoulderClearPending = false;
        if (ms.rComboArmed && (e.button == Button::A || e.button == Button::B || e.button == Button::R_SHIFT)) {
            ms.rComboArmed = false;
            d.on_r_combo_revert();
        }
        if (e.button == Button::A) {
            ms.aPressedAlone = false;
            ms.lastAPress = 0;
            d.on_a_released(true);
        }
        if (e.button == Button::B) ms.bPressedAlone = false;
        if (e.button == Button::SELECT) ms.selectPressedAlone = false;
        return;
    }

    // A/B belongs to the rotation until it comes up. Another press cancels the
    // rotation without sending the held A/B to the ordinary edit or copy paths.
    if (ms.phraseRotationKey != Button::COUNT) {
        if (e.button == ms.phraseRotationKey) {
            if (e.action == ButtonAction::RELEASED) {
                if (ms.phraseRotationArmed) d.on_phrase_rotate(e.button);
                ms.phraseRotationKey = Button::COUNT;
                ms.phraseRotationArmed = false;
            }
            return;
        }
        const bool held = ms.phraseRotationKey == Button::A ? m.a : m.b;
        if (held) {
            if (e.action == ButtonAction::PRESSED) ms.phraseRotationArmed = false;
            return;
        }
        // An overlay may have owned the physical release.
        ms.phraseRotationKey = Button::COUNT;
        ms.phraseRotationArmed = false;
    }

    // Punch owns A as a performance hold. START always remains transport, even
    // while that hold/modifier is down; it must never route to tracker audition.
    if(e.button==Button::START&&d.punch_open()) {
        if(e.action==ButtonAction::PRESSED)d.on_start();
        return;
    }

    // ── Release ──────────────────────────────────────────────────────────────────────────────────
    if (e.action != ButtonAction::PRESSED) {
        if (ms.shoulderClearPending && (e.button == Button::L_SHIFT || e.button == Button::R_SHIFT)) {
            ms.shoulderClearPending = false;
            d.on_l_r();
            return;
        }
        // The mute/solo chord ends on a release and consumes it (an armed A must not reach
        // `on_a_released()`). On the A/B release, `m.r` is "is R still down?".
        if (ms.rComboArmed) {
            if (e.button == Button::R_SHIFT) {
                ms.rComboArmed = false;
                d.on_r_combo_commit();
                return;
            }
            if ((e.button == Button::A || e.button == Button::B) && m.r) {
                ms.rComboArmed = false;
                d.on_r_combo_revert();
                return;
            }
        }
        if (e.button == Button::A) {
            if (ms.aPressedAlone) {   // no A-combo intervened: the deferred A fires now
                ms.aPressedAlone = false;
                d.on_button_a();
            }
            d.on_a_released();        // the FX helper commits here
        }
        if (e.button == Button::B) {
            if (ms.bPressedAlone) {
                ms.bPressedAlone = false;
                d.on_button_b();
            }
        }
        if (e.button == Button::SELECT) {
            if (ms.selectPressedAlone) {
                ms.selectPressedAlone = false;
                ms.lastAPress = 0;
                d.on_select();
            }
        }
        return;
    }

    if (ms.phraseSelectHeld & direction) return;

    if (e.button != Button::SELECT) ms.selectPressedAlone = false;

    // SELECT may open help; leave the current audition alone while reading it.
    if (e.button != Button::START && e.button != Button::SELECT && !m.a) d.on_stop_preview();

    // L+SELECT opens Punch directly. L+SELECT had no prior action; both orders work.
    if (!m.r && !m.a && !m.b &&
        ((e.button == Button::SELECT && m.l) || (e.button == Button::L_SHIFT && m.select))) {
        ms.selectPressedAlone = false;
        ms.clear_phrase_keys();
        d.on_select_l();
        return;
    }

    // R+SELECT is Studio on musical screens, and the existing SELECT+R folder action in
    // the file browser. Accept either press order without leaving a deferred bare SELECT.
    if (e.button == Button::SELECT && m.r && !m.l && !m.a && !m.b) {
        ms.selectPressedAlone = false;
        ms.clear_phrase_keys();
        d.on_select_r();
        return;
    }

    if (m.select && !m.a && !m.b && !m.l && !m.r) {
        if (horizontal && d.on_phrase_overview(e.button)) {
            ms.selectPressedAlone = false;
            ms.lastAPress = 0;
            ms.phraseSelectHeld |= horizontal;
            return;
        }
        const bool reclaim = e.button == Button::SELECT && ms.phraseFirstArrow != Button::COUNT;
        const Button arrow = reclaim ? ms.phraseFirstArrow : e.button;
        if ((reclaim || (vertical && !repeatedVertical)) && d.on_phrase_history(arrow, reclaim)) {
            ms.selectPressedAlone = false;
            ms.lastAPress = 0;
            ms.phraseFirstArrow = Button::COUNT;
            ms.phraseSelectHeld |= arrow == Button::DPAD_UP ? 1u : 2u;
            return;
        }
        if (e.button == Button::SELECT && ms.phraseVerticalHeld && d.phrase_editor_context()) {
            ms.selectPressedAlone = false;
            return;
        }
    }

    if (m.select && !m.l && !m.r &&
        ((e.button == Button::A && !m.b) || (e.button == Button::B && !m.a)) &&
        d.arm_phrase_rotation()) {
        ms.aPressedAlone = ms.bPressedAlone = false;
        ms.lastAPress = 0;
        ms.phraseRotationKey = e.button;
        ms.phraseRotationArmed = true;
        return;
    }

    // ── A + … ────────────────────────────────────────────────────────────────────────────────────
    if (m.a && !m.l && !m.r) {
        switch (e.button) {
            // Each cancels the deferred A: it was a combo.
            case Button::B:          ms.aPressedAlone = false; d.on_a_b();     return;   // delete / reset
            case Button::DPAD_UP:    ms.aPressedAlone = false; d.on_a_up();    return;   // +16 / +1 octave (or the FX helper)
            case Button::DPAD_DOWN:  ms.aPressedAlone = false; d.on_a_down();  return;   // −16 / −1 octave
            case Button::DPAD_RIGHT: ms.aPressedAlone = false; d.on_a_right(); return;   // +1
            case Button::DPAD_LEFT:  ms.aPressedAlone = false; d.on_a_left();  return;   // −1
            case Button::START:      ms.aPressedAlone = false; d.on_a_start(); return;   // audition
            default: break;
        }
    }

    // ── SELECT + … : the file browser's chords ───────────────────────────────────────────────────
    // ⚠️ The mods include the button being pressed: on SELECT+R, R's press has already set `m.r`.
    if (m.select && !m.l && (!m.r || e.button == Button::R_SHIFT)) {
        switch (e.button) {
            case Button::A:       d.on_select_a(); return;   // rename     (opens the keyboard)
            case Button::B:       d.on_select_b(); return;   // delete     (arms the confirm)
            case Button::R_SHIFT: ms.clear_phrase_keys(); d.on_r_combo_press(); d.on_select_r(); return; // Studio / browser new folder
            default: break;
        }
    }

    // ── B + DPAD: which item ─────────────────────────────────────────────────────────────────────
    if (m.b && !m.l && !m.r && !m.a) {
        switch (e.button) {
            // Each cancels the deferred B, or B+RIGHT in the EQ editor would close it on release.
            case Button::DPAD_LEFT:  ms.bPressedAlone = false; d.on_b_left();  return;   // prev item / EQ slot −1
            case Button::DPAD_RIGHT: ms.bPressedAlone = false; d.on_b_right(); return;   // next item / EQ slot +1
            case Button::DPAD_UP:    ms.bPressedAlone = false; d.on_b_up();    return;   // SONG / pool: page up
            case Button::DPAD_DOWN:  ms.bPressedAlone = false; d.on_b_down();  return;   // SONG / pool: page down
            default: break;
        }
    }

    // ── L+R: leave selection mode on release ────────────────────────────────────────────────────
    if (m.l && m.r) {
        switch (e.button) {
            case Button::SELECT:   // reserved: consumed, not passed down
            case Button::A:
            case Button::B:       return;
            case Button::L_SHIFT:
            case Button::R_SHIFT:
                ms.shoulderClearPending = !m.a && !m.b && !m.select;
                return;
            default: break;
        }
    }

    // ── L+B+A: clone — before L+A, which would paste ─────────────────────────────────────────────
    if (m.l && m.b && !m.r && e.button == Button::A) {
        d.on_l_b_a();
        return;
    }

    // ── L + … : selection and the clipboard ──────────────────────────────────────────────────────
    if (m.l && !m.r) {
        switch (e.button) {
            case Button::A:     d.on_l_a(); return;   // cut (in a selection) / paste (outside one)
            case Button::B:     d.on_l_b(); return;   // enter selection, then widen it
            case Button::START: d.on_l_start(); return;   // LIVE's row queue; never play/stop
            default: break;
        }
        // L + a direction fills rows — L alone, so a held clipboard chord cannot fire it.
        if (!m.a && !m.b && !m.select) {
            switch (e.button) {
                case Button::DPAD_UP:    d.on_l_up();    return;
                case Button::DPAD_DOWN:  d.on_l_down();  return;
                case Button::DPAD_LEFT:  d.on_l_left();  return;
                case Button::DPAD_RIGHT: d.on_l_right(); return;
                default: break;
            }
        }
    }

    // ── R + DPAD: move between screens ───────────────────────────────────────────────────────────
    if (m.r && !m.l) {
        switch (e.button) {
            case Button::DPAD_UP:    d.on_r_up();    return;
            case Button::DPAD_DOWN:  d.on_r_down();  return;
            case Button::DPAD_LEFT:  d.on_r_left();  return;
            case Button::DPAD_RIGHT: d.on_r_right(); return;
            // MUTE and SOLO. ⚠️ Both arm the latch even where nothing happens: a chord armed on SONG
            // and released on MIXER must still be closed out.
            case Button::A:          d.on_r_a(); ms.rComboArmed = true; return;
            case Button::B:          ms.bPressedAlone = false; d.on_r_b(); ms.rComboArmed = true; return;
            // ⚠️ R arriving second: half the time B lands first. Where R+B mutes, B is held
            // (`defer_b_to_release`) so it can still be claimed here — `bPressedAlone`, not `m.b`,
            // because a B that already acted must not also be a chord.
            case Button::R_SHIFT:
                if (ms.bPressedAlone) {
                    ms.bPressedAlone = false;
                    d.on_r_b();
                    ms.rComboArmed = true;
                }
                return;
            case Button::START:      d.on_r_start(); return;   // LIVE's stop; the song from below SONG
            default: break;
        }
    }

    // ── A, and the A,A double-tap (300 ms) ───────────────────────────────────────────────────────
    if (e.button == Button::A && !m.l && !m.r) {
        // Defer first; a deferred cell is never a double-tap cell, so the window is cleared.
        if (d.defer_a_to_release()) {
            ms.aPressedAlone = true;
            ms.lastAPress    = 0;
            d.on_a_deferred();   // a deferred tap aimed at the playhead reads it now
            return;
        }
        if (now - ms.lastAPress < 300) {
            ms.lastAPress = 0;   // so a triple-tap is not two double-taps
            d.on_a_a();
        } else {
            ms.lastAPress = now;
            d.on_button_a();
        }
        return;
    }

    // ── B ────────────────────────────────────────────────────────────────────────────────────────
    if (e.button == Button::B && !m.l && !m.r && !m.a) {
        if (d.defer_b_to_release()) {
            ms.bPressedAlone = true;
            return;
        }
        d.on_button_b();
        return;
    }

    // Before the no-modifier guard: SELECT's own press sets `m.select`. SELECT only arms here.
    if (e.button == Button::SELECT && !m.l && !m.r && !m.a && !m.b) {
        ms.selectPressedAlone = true;
        return;
    }
    if (e.button == Button::START && !m.l && !m.r && !m.a && !m.b && !m.select) { d.on_start(); return; }

    // ── The plain D-pad ──────────────────────────────────────────────────────────────────────────
    if (m.l || m.r || m.a || m.b || m.select) return;

    if (vertical) {
        ms.phraseFirstArrow = Button::COUNT;
        // Reclaim one plain move if SELECT arrives next. A repeated move is ordinary scrolling.
        if (!repeatedVertical && ms.phraseVerticalHeld == vertical && d.on_phrase_history_arrow(e.button)) {
            ms.phraseFirstArrow = e.button;
            return;
        }
    }

    switch (e.button) {
        case Button::DPAD_UP:    d.on_dpad_up();    break;
        case Button::DPAD_DOWN:  d.on_dpad_down();  break;
        case Button::DPAD_LEFT:  d.on_dpad_left();  break;
        case Button::DPAD_RIGHT: d.on_dpad_right(); break;
        default: break;
    }
}

}  // namespace pt::ui
