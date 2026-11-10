#include "ui/drumlab.h"
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/effects.h"
#include "songcore/timing.h"
#include "songcore/voice_derive.h"
#include "songcore/scales.h"
#include "ui/canvas.h"
#include "ui/helpers.h"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <vector>

namespace pt::ui {
namespace {
constexpr const char* genres[] = {"Hip Hop", "Boom Bap", "Lo-Fi", "Drum & Bass", "House", "Techno", "Dub", "Breakbeat"};
constexpr const char* roles[] = {"Kick", "Snare", "Closed Hat", "Open Hat", "Perc 1", "Perc 2", "Tom", "FX"};
constexpr const char* shortRoles[] = {"KICK", "SNR", "CH", "OH", "P1", "P2", "TOM", "FX"};
constexpr const char* settings[] = {"Style", "Sources", "Length", "Density", "Variation", "Swing", "Humanize", "Fill", "Seed"};

constexpr uint16_t bit(int n) { return static_cast<uint16_t>(1u << n); }
// Original rhythmic recipes. They are not transcriptions of songs or preset banks.
constexpr uint16_t masks[8][8] = {
    {bit(0)|bit(6)|bit(8), bit(4)|bit(12), 0x5555, bit(14), bit(3)|bit(11), bit(7)|bit(15), bit(15), bit(0)},
    {bit(0)|bit(7)|bit(10), bit(4)|bit(12), 0x5555, bit(6)|bit(14), bit(2)|bit(10), bit(13), bit(15), bit(0)},
    {bit(0)|bit(9), bit(4)|bit(12), 0x5555, bit(14), bit(3)|bit(11), bit(7), bit(15), bit(0)},
    {bit(0)|bit(10), bit(4)|bit(12), 0xffff, bit(6)|bit(14), bit(3)|bit(11), bit(7)|bit(15), bit(14)|bit(15), bit(0)},
    {0x1111, bit(4)|bit(12), 0x5555, 0x4444, bit(3)|bit(7)|bit(11)|bit(15), bit(6)|bit(14), bit(15), bit(0)},
    {0x1111, bit(4)|bit(12), 0xaaaa, 0x4444, bit(2)|bit(6)|bit(10)|bit(14), bit(3)|bit(9)|bit(15), bit(14), bit(0)},
    {bit(0)|bit(8), bit(8), 0x5555, bit(14), bit(6)|bit(14), bit(3)|bit(11), bit(15), bit(0)},
    {bit(0)|bit(6)|bit(10), bit(4)|bit(12), 0xaaaa, bit(14), bit(3)|bit(11), bit(7)|bit(15), bit(13)|bit(15), bit(0)}
};

struct Random {
    uint32_t state;
    explicit Random(uint32_t seed) : state(seed ? seed : 0x6d2b79f5u) {}
    uint32_t next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    int below(int limit) { return limit > 0 ? static_cast<int>(next() % static_cast<unsigned>(limit)) : 0; }
};

bool valid_config(const DrumLabState& s) {
    if (s.genre < 0 || s.genre >= 8 || s.activeLanes < 5 || s.activeLanes > 8 ||
        (s.steps != 16 && s.steps != 32 && s.steps != 64)) return false;
    for (int value : {s.density, s.variation, s.swing, s.humanize, s.fill})
        if (value < 0 || value > 100) return false;
    return true;
}

bool source_assigned(const songcore::Project& p, int id) {
    if (id < 0 || id >= static_cast<int>(p.instruments.size()) || id >= songcore::POOL_INSTRUMENTS) return false;
    const auto& ins = p.instruments[id];
    if (ins.instrumentType == songcore::InstrumentType::EXTERNAL) return false;
    return ins.instrumentType == songcore::InstrumentType::SOUNDFONT
               ? ins.soundfontPath.has_value() && !ins.soundfontPath->empty()
               : ins.sampleFilePath.has_value() && !ins.sampleFilePath->empty();
}

int straight_groove(const songcore::Project& p) {
    for (int id = 0; id < std::min<int>(songcore::POOL_GROOVES, p.grooves.size()); ++id) {
        const auto& groove = p.grooves[id];
        const int count = songcore::groove_active_length(groove), ruler = songcore::groove_ticks_per_step(groove);
        bool flat = true;
        for (int row = 0; row < count; ++row) if (groove.steps[row] != ruler) { flat = false; break; }
        if (flat) return id;
    }
    return -1;
}

bool ready(DrumLabState& s, const songcore::Project& p) {
    if (!valid_config(s)) { s.status = "Invalid Drum Lab settings; nothing changed."; return false; }
    if (p.scaleKey < 0 || p.scaleKey > 11) { s.status = "Set a valid project scale key before generating drums."; return false; }
    if (!s.generated || s.dirty || s.generatedSteps != s.steps) { s.status = "Generate the pattern before Preview or Apply."; return false; }
    for (int lane = 0; lane < s.activeLanes; ++lane) {
        if (!source_assigned(p, s.lanes[lane].instrument)) {
            s.status = std::string("Assign a loaded sample / synth to ") + roles[lane] + ".";
            return false;
        }
        const auto& root = p.instruments[s.lanes[lane].instrument].root;
        if (root.pitch < 0 || root.pitch > 11 || songcore::note_to_midi(root) < 0 || songcore::note_to_midi(root) > 131) {
            s.status = std::string("Set a valid ROOT note for ") + roles[lane] + "."; return false;
        }
        for (int step = 0; step < s.steps; ++step)
            if (s.grid[lane][step].velocity > 127 || s.grid[lane][step].delayTicks >= songcore::TICS_PER_STEP) {
                s.status = "Pattern values are invalid; regenerate before continuing."; return false;
            }
    }
    if (straight_groove(p) < 0) { s.status = "A straight groove slot is needed for matching Preview / Apply timing."; return false; }
    return true;
}

bool phrase_empty(const songcore::Phrase& phrase) {
    if (phrase.steps.size() != songcore::PHRASE_ROWS) return false;
    for (const auto& step : phrase.steps)
        if (step.note != songcore::Note::EMPTY() || step.instrument != 0 || step.volume != 0x7f ||
            step.fx1Type || step.fx1Value || step.fx2Type || step.fx2Value || step.fx3Type || step.fx3Value) return false;
    return true;
}

bool chain_empty(const songcore::Chain& chain) {
    return chain.phraseRefs.size() == songcore::CHAIN_ROWS && chain.transposeValues.size() == songcore::CHAIN_ROWS &&
           std::all_of(chain.phraseRefs.begin(), chain.phraseRefs.end(), [](int v) { return v == -1; }) &&
           std::all_of(chain.transposeValues.begin(), chain.transposeValues.end(), [](int v) { return v == 0; });
}

std::string hex(int value, int digits = 2) {
    char text[24]; std::snprintf(text, sizeof(text), digits == 8 ? "%08X" : "%02X", static_cast<unsigned>(value));
    return text;
}

std::string setting_value(const DrumLabState& s, int row) {
    switch (row) {
        case 0: return drumlab_genre_name(s.genre);
        case 1: return std::to_string(s.activeLanes) + " lanes";
        case 2: return std::to_string(s.steps) + " steps";
        case 3: return std::to_string(s.density) + "%";
        case 4: return std::to_string(s.variation) + "%";
        case 5: return std::to_string(s.swing) + "%";
        case 6: return std::to_string(s.humanize) + "%";
        case 7: return std::to_string(s.fill) + "%";
        default: return hex(static_cast<int>(s.seed), 8);
    }
}

void schedule_hit(AudioEngine& engine, const songcore::Project& project, int track, int instrument,
                  const DrumLabStep& step, int64_t frame, int tempo) {
    engine.scheduleProgramNote(frame, track, instrument, drumlab_note_on(project, instrument, step), tempo, false);
}
} // namespace

const char* drumlab_genre_name(int genre) { return genres[std::clamp(genre, 0, 7)]; }
const char* drumlab_lane_name(int lane) { return roles[std::clamp(lane, 0, 7)]; }

songcore::NoteOnPayload drumlab_note_on(const songcore::Project& project, int instrument, const DrumLabStep& step) {
    songcore::NoteOnPayload result{};
    if (instrument < 0 || instrument >= static_cast<int>(project.instruments.size())) return result;
    const auto& ins = project.instruments[instrument];
    const int transpose = songcore::effective_transpose_semitones(songcore::project_transpose_semitones(project), project, instrument, {});
    int midi = songcore::note_to_midi(ins.root);
    if (transpose != 0) midi = std::clamp(midi + transpose, 0, 127);
    // Every generated phrase explicitly restores the base scale on its first row.
    // Ask the same helper the phrase note cursor uses, including sliced/TRANSP exemptions.
    static const songcore::Phrase baseScalePhrase;
    if (midi >= 0 && midi <= 127) midi = songcore::phrase_note_scale(project, baseScalePhrase, 0, instrument).snap(midi);
    result.note = static_cast<uint8_t>(midi);
    result.transpose = transpose;
    result.velocity = static_cast<int8_t>(step.velocity);
    const float velocity = step.velocity / 127.f;
    result.velGainBits = songcore::f32_bits(velocity * velocity);
    result.volGainBits = songcore::f32_bits(songcore::hex_to_float(ins.volume));
    result.panBits = songcore::f32_bits(songcore::hex_to_float(ins.pan));
    result.start = result.slice = result.tableId = result.tableRow = -1;
    return result;
}

int drumlab_hit_count(const DrumLabState& s) {
    int count = 0;
    for (int lane = 0; lane < std::clamp(s.activeLanes, 0, 8); ++lane)
        for (int step = 0; step < std::clamp(s.generatedSteps, 0, 64); ++step) count += s.grid[lane][step].velocity > 0;
    return count;
}

bool drumlab_generate(DrumLabState& s) {
    if (!valid_config(s)) { s.status = "Invalid Drum Lab settings; pattern kept."; return false; }
    if (s.previewRunning) { s.status = "Stop Preview before generating a new pattern."; return false; }
    auto generated = s.grid;
    for (int lane = 0; lane < 8; ++lane) {
        if (s.generated && s.lanes[lane].locked) continue;
        generated[lane] = {};
        if (lane >= s.activeLanes) continue;
        Random random(s.seed ^ (0x9e3779b9u * static_cast<uint32_t>(lane + 1)) ^ (0x85ebca6bu * static_cast<uint32_t>(s.genre + 1)));
        for (int step = 0; step < s.steps; ++step) {
            const int position = step % 16;
            const bool base = (masks[s.genre][lane] & bit(position)) != 0;
            const bool anchor = lane < 2 && base;
            const int chance = random.below(100), fillChance = random.below(100);
            const int humanVelocity = random.below(2 * (s.humanize / 4) + 1) - s.humanize / 4;
            const int maxDelay = s.humanize ? 1 + (s.humanize - 1) * 3 / 100 : 0;
            const int humanDelay = random.below(maxDelay + 1);
            const int ghostProbability = s.density * s.variation / (lane == 2 ? 250 : 450);
            bool hit = anchor || chance < (base ? s.density : ghostProbability);
            if (lane >= 1 && lane != 3 && step >= s.steps - 4 && fillChance < s.fill &&
                (lane == 1 || lane == 6 || position % 2 == lane % 2)) hit = true;
            if (!hit) continue;
            const int accents[] = {108,112,84,90,87,82,99,90};
            const int accent = position % 4 == 0 ? 7 : (lane == 2 && position % 4 == 2 ? -13 : 0);
            const int ghost = !base && !anchor ? -25 : 0;
            generated[lane][step].velocity = static_cast<uint8_t>(std::clamp(accents[lane] + accent + ghost + humanVelocity, 24, 127));
            generated[lane][step].delayTicks = static_cast<uint8_t>(std::clamp((step % 2 ? s.swing * 6 / 100 : 0) + humanDelay, 0, songcore::TICS_PER_STEP - 1));
        }
    }
    s.grid = generated; s.generated = true; s.dirty = false; s.generatedSteps = s.steps; s.confirmApply = false;
    s.gridPage = std::clamp(s.gridPage, 0, s.steps / 16 - 1);
    s.status = std::to_string(drumlab_hit_count(s)) + " hits generated. Preview, then append safely with Apply.";
    return true;
}

bool drumlab_apply(DrumLabState& s, songcore::SongcoreHost& host) {
    if (host.is_playing() || s.previewRunning) { s.status = "Stop playback before Apply; song kept."; return false; }
    try {
    const auto& project = host.project();
    if (!ready(s, project)) return false;
    if (project.tracks.size() < 8) { s.status = "Project track bank is incomplete; song kept."; return false; }
    int last = -1;
    std::array<bool, songcore::POOL_CHAINS> usedChains{};
    std::array<bool, songcore::POOL_PHRASES> usedPhrases{};
    for (const auto& track : project.tracks) for (size_t row = 0; row < track.chainRefs.size(); ++row) {
        const int chain = track.chainRefs[row];
        if (chain != -1) last = std::max(last, static_cast<int>(std::min<size_t>(row, 256)));
        if (chain >= 0 && chain < songcore::POOL_CHAINS) usedChains[chain] = true;
    }
    if (last >= 255) { s.status = "No room to append: song row FF is occupied. Song kept."; return false; }
    for (const auto& chain : project.chains) for (int phrase : chain.phraseRefs)
        if (phrase >= 0 && phrase < songcore::POOL_PHRASES) usedPhrases[phrase] = true;
    std::vector<int> freeChains, freePhrases;
    for (int id = 0; id < std::min<int>(songcore::POOL_CHAINS, project.chains.size()); ++id)
        if (!usedChains[id] && project.chains[id].id == id && chain_empty(project.chains[id])) freeChains.push_back(id);
    for (int id = 0; id < std::min<int>(songcore::POOL_PHRASES, project.phrases.size()); ++id)
        if (!usedPhrases[id] && project.phrases[id].id == id && phrase_empty(project.phrases[id])) freePhrases.push_back(id);
    const int bars = s.steps / 16, groove = straight_groove(project);
    if (static_cast<int>(freeChains.size()) < s.activeLanes || static_cast<int>(freePhrases.size()) < s.activeLanes * bars) {
        s.status = "Not enough empty, unused chains / phrases. Song kept."; return false;
    }
    // Finish every allocation before committing the three vector swaps below.
    auto tracks = project.tracks; auto chains = project.chains; auto phrases = project.phrases;
    const int row = last + 1;
    for (int lane = 0; lane < 8; ++lane) {
        tracks[lane].chainRefs.resize(std::max<int>(tracks[lane].chainRefs.size(), row + 1), -1);
        if (lane >= s.activeLanes) continue;
        const int chainId = freeChains[lane]; tracks[lane].chainRefs[row] = chainId;
        chains[chainId] = songcore::Chain(chainId);
        for (int bar = 0; bar < bars; ++bar) {
            const int phraseId = freePhrases[lane * bars + bar];
            chains[chainId].phraseRefs[bar] = phraseId; phrases[phraseId] = songcore::Phrase(phraseId);
            for (int position = 0; position < 16; ++position) {
                auto& target = phrases[phraseId].steps[position];
                // Explicitly reset inherited song grooves, without editing any groove bank.
                if (position == 0) { target.fx2Type = songcore::FX_GRV; target.fx2Value = groove; }
                if (position == 0) { target.fx3Type = songcore::FX_SCA; target.fx3Value = std::clamp(project.scaleKey, 0, 11) << 4; }
                const auto& hit = s.grid[lane][bar * 16 + position];
                if (!hit.velocity) continue;
                target.note = project.instruments[s.lanes[lane].instrument].root;
                target.instrument = s.lanes[lane].instrument; target.volume = hit.velocity;
                if (hit.delayTicks) { target.fx1Type = songcore::FX_LAT; target.fx1Value = hit.delayTicks; }
            }
        }
    }
    std::string success = "Appended " + std::to_string(bars) + " bar(s) at SONG " + hex(row) + ". Existing music kept; save the project.";
    auto& target = host.edit_project();
    target.tracks.swap(tracks); target.chains.swap(chains); target.phrases.swap(phrases);
    s.appliedSongRow = row; ++s.applySerial; s.confirmApply = false;
    s.status.swap(success);
    return true;
    } catch (const std::bad_alloc&) {
        s.status = "Not enough memory; song kept."; s.confirmApply = false; return false;
    }
}

bool drumlab_preview(DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    if (s.previewRunning) { drumlab_stop(s, host, engine); s.status = "Preview stopped. Song unchanged."; return true; }
    if (host.is_playing()) { s.status = "Stop the song before Drum Lab Preview."; return false; }
    if (!ready(s, host.project())) return false;
    for (int lane = 0; lane < s.activeLanes; ++lane)
        if (!host.instrument_media_ready(s.lanes[lane].instrument)) {
            s.status = std::string("Source not loaded for ") + roles[lane] + ". Reload it in INSTRUMENT."; return false;
        }
    host.stop(); host.push_params();
    for (int table = 0; table < std::min<int>(songcore::POOL_TABLES, host.project().tables.size()); ++table)
        songcore::push_table(engine, host.project(), table);
    s.previewTempo = std::clamp(songcore::tempo_in_force(host.project()), 1, 999);
    s.previewFramesPerStep = std::max<int64_t>(1, songcore::frames_per_step(s.previewTempo, engine.getSampleRate()));
    s.previewStartFrame = engine.getCurrentFrame() + std::max(100, engine.getSampleRate() / 20);
    s.previewNextFrame = s.previewStartFrame; s.previewSwingRatio = 50;
    s.previewStepStartFrames.fill(-1);
    s.previewNextStep = 0; s.previewDisplayStep = -1; s.previewRunning = true; s.confirmApply = false;
    engine.requestResume();
    s.status = "Preview loops on the existing mixer. A on Preview stops; song is unchanged.";
    drumlab_tick(s, host, engine); return true;
}

void drumlab_stop(DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    s.confirmApply = false;
    if (!s.previewRunning) return;
    s.previewRunning = false; s.previewDisplayStep = -1;
    if (!host.is_playing()) { host.stop(); engine.clearScheduledNotes(); }
}

void drumlab_before_transport(DrumLabState& s, AudioEngine& engine) {
    const bool owned = s.previewRunning;
    s.previewRunning = false; s.confirmApply = false; s.previewDisplayStep = -1;
    if (owned) { engine.clearScheduledNotes(); engine.stopAllRamped(); }
}

void drumlab_tick(DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    if (!s.previewRunning) return;
    if (host.is_playing()) { s.previewRunning = false; s.previewDisplayStep = -1; return; }
    if (!valid_config(s) || s.dirty || s.generatedSteps != s.steps ||
        songcore::tempo_in_force(host.project()) != s.previewTempo) {
        drumlab_stop(s, host, engine); s.status = "Settings changed; Preview stopped."; return;
    }
    for (int lane = 0; lane < s.activeLanes; ++lane)
        if (!host.instrument_media_ready(s.lanes[lane].instrument)) {
            drumlab_stop(s, host, engine); s.status = std::string("Source no longer loaded: ") + roles[lane] + "."; return;
        }
    const int64_t now = engine.getCurrentFrame(), horizon = now + std::max(512, engine.getSampleRate() / 8);
    // Do not spray stale hits if a modal operation paused UI scheduling for several seconds.
    if (now > s.previewNextFrame + engine.getSampleRate()) {
        // Jump to the current complete pair, then at most two half-pairs ahead. Never schedule a
        // burst of missed hits. At50 this is exactly the former ceil-to-the-next-step behavior.
        const int64_t pair = std::max<int64_t>(0, (now - s.previewStartFrame) / (2 * s.previewFramesPerStep));
        s.previewNextStep = pair * 2;
        s.previewNextFrame = s.previewStartFrame + pair * 2 * s.previewFramesPerStep;
        s.previewSwingRatio = std::clamp(host.project().swing, 50, 75);
        s.previewStepStartFrames.fill(-1);
        while (s.previewNextFrame < now) {
            s.previewStepStartFrames[static_cast<size_t>(s.previewNextStep % s.steps)] = s.previewNextFrame;
            s.previewNextFrame += songcore::swing_step_duration(s.previewFramesPerStep,
                                    s.previewSwingRatio, (s.previewNextStep & 1) != 0);
            ++s.previewNextStep;
        }
    }
    for (int budget = 0; budget < 64; ++budget) {
        const int64_t frame = s.previewNextFrame;
        if (frame > horizon) break;
        const int step = static_cast<int>(s.previewNextStep % s.steps);
        // Apply explicitly selects a straight groove, so Preview follows PROJECT Swing too, even
        // when groove0 is custom. Retain the ratio until its short step has completed the pair.
        if ((s.previewNextStep & 1) == 0) s.previewSwingRatio = std::clamp(host.project().swing, 50, 75);
        const int64_t stepDuration = songcore::swing_step_duration(s.previewFramesPerStep,
                                    s.previewSwingRatio, (s.previewNextStep & 1) != 0);
        for (int lane = 0; lane < s.activeLanes; ++lane) {
            if (!source_assigned(host.project(), s.lanes[lane].instrument)) continue;
            const auto& hit = s.grid[lane][step];
            if (hit.velocity) schedule_hit(engine, host.project(), lane, s.lanes[lane].instrument, hit,
                std::max(frame, now + 1) + hit.delayTicks * songcore::frames_per_tic(stepDuration), s.previewTempo);
        }
        s.previewStepStartFrames[static_cast<size_t>(step)] = frame;
        s.previewNextFrame += stepDuration;
        ++s.previewNextStep;
    }
    if (now >= s.previewStartFrame) {
        int64_t latest = -1;
        for (int step = 0; step < s.steps; ++step) {
            const int64_t frame = s.previewStepStartFrames[static_cast<size_t>(step)];
            if (frame >= s.previewStartFrame && frame <= now && frame > latest) {
                latest = frame; s.previewDisplayStep = step;
            }
        }
        if (s.previewDisplayStep >= 0) s.gridPage = s.previewDisplayStep / 16;
    }
}

bool drumlab_button(const ButtonEvent& e, DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    s.row = std::clamp(s.row, 0, DRUMLAB_ROWS - 1); s.col = std::clamp(s.col, 0, 1);
    s.footerAction = s.row >= 17 ? s.row - 17 : std::clamp(s.footerAction, 0, 2);
    if (e.button == Button::START) { if (e.action == ButtonAction::PRESSED) drumlab_stop(s, host, engine); return false; }
    if (e.button == Button::SELECT && e.action == ButtonAction::RELEASED) { s.showHelp = !s.showHelp; return true; }
    if (s.showHelp) {
        if (e.button == Button::B && e.action == ButtonAction::RELEASED) s.showHelp = false;
        return true;
    }
    if (e.button == Button::A) {
        if (e.action == ButtonAction::PRESSED) { s.aUsed = false; return true; }
        if (s.aUsed) { s.aUsed = false; return true; }
        if (s.row == 17) { drumlab_stop(s, host, engine); if (s.generated) ++s.seed; drumlab_generate(s); }
        else if (s.row == 18) drumlab_preview(s, host, engine);
        else if (s.row == 19) {
            const bool confirmed = s.confirmApply;
            drumlab_stop(s, host, engine);
            if (!confirmed) {
                if (ready(s, host.project())) { s.confirmApply = true; s.status = "A again: append to a new SONG row using only empty resources."; }
            } else drumlab_apply(s, host);
        } else if (s.row >= 9 && s.col == 1) { s.lanes[s.row - 9].locked = !s.lanes[s.row - 9].locked; s.confirmApply = false; }
        return true;
    }
    if ((e.button == Button::L_SHIFT || e.button == Button::R_SHIFT) && e.action == ButtonAction::RELEASED) {
        const int pages = std::max(1, s.steps / 16), delta = e.button == Button::R_SHIFT ? 1 : -1;
        s.gridPage = (s.gridPage + pages + delta) % pages; return true;
    }
    if (e.action != ButtonAction::PRESSED || !is_dpad(e.button)) return false;
    const bool positive = e.button == Button::DPAD_RIGHT || e.button == Button::DPAD_UP;
    if (!e.mods.a) {
        if (e.button == Button::DPAD_UP || e.button == Button::DPAD_DOWN) {
            // The three bottom actions share one visual row. Vertical movement enters/leaves
            // that row, while horizontal movement below chooses an action in screen order.
            if (e.button == Button::DPAD_UP) s.row = s.row >= 17 ? 16 : std::max(0, s.row - 1);
            else if (s.row == 16) s.row = 17 + s.footerAction;
            else if (s.row < 16) ++s.row;
            s.confirmApply = false;
        } else if (s.row >= 17) {
            s.row = std::clamp(s.row + (positive ? 1 : -1), 17, 19);
            s.footerAction = s.row - 17; s.confirmApply = false;
        } else if (s.row >= 9 && s.row <= 16) s.col = e.button == Button::DPAD_RIGHT ? 1 : 0;
        return true;
    }
    s.aUsed = true; s.confirmApply = false;
    if (s.row >= 17) return true; // An edit chord cannot press or invalidate an action button.
    drumlab_stop(s, host, engine);
    const bool coarse = e.button == Button::DPAD_UP || e.button == Button::DPAD_DOWN;
    const int delta = (positive ? 1 : -1) * (coarse ? 10 : 1);
    if (s.row == 0) s.genre = (s.genre + 8 + (positive ? 1 : -1)) % 8;
    else if (s.row == 1) s.activeLanes = std::clamp(s.activeLanes + (positive ? 1 : -1), 5, 8);
    else if (s.row == 2) s.steps = positive ? (s.steps < 32 ? 32 : 64) : (s.steps > 32 ? 32 : 16);
    else if (s.row >= 3 && s.row <= 7) {
        int* values[] = {&s.density, &s.variation, &s.swing, &s.humanize, &s.fill};
        *values[s.row - 3] = std::clamp(*values[s.row - 3] + delta, 0, 100);
    } else if (s.row == 8) s.seed += static_cast<uint32_t>(delta);
    else if (s.row >= 9 && s.row <= 16) {
        auto& lane = s.lanes[s.row - 9];
        if (s.col == 0) lane.instrument = std::clamp(lane.instrument + (positive ? 1 : -1) * (coarse ? 16 : 1), 0, 127);
        else lane.locked = !lane.locked;
        return true;
    } else return true;
    s.dirty = true; s.status = "Settings changed. Generate updates unlocked lanes."; return true;
}

void drumlab_draw(Canvas& c, const DrumLabState& s, const Theme& t, const songcore::Project& project) {
    const Argb panel = t.rowEvery4th, accent = t.rowCursor, muted = t.textParam, ink = t.textValue;
    c.panel_rect(18,104,604,44,panel,with_alpha(muted,.2f),6);
    c.draw_label("DRUM LAB",30,111,accent,12);
    c.draw_label(drumlab_genre_name(s.genre),30,127,ink,12);
    const std::string summary = std::to_string(std::clamp(s.activeLanes,5,8)) + " sources / " + std::to_string(s.steps) + " steps / " + std::to_string(drumlab_hit_count(s)) + " hits";
    c.draw_label(summary,260,120,muted,12);
    if (s.showHelp) {
        c.panel_rect(18,158,604,282,panel,with_alpha(muted,.2f),7);
        const char* lines[] = {"Assign 5-8 sample or synth slots already loaded in INSTRUMENT.",
            "A + arrows edits a setting. Up / Down chooses the next field.",
            "Left / Right selects SLOT / LOCK or the bottom action buttons.",
            "Locks preserve a lane's triggers, velocities and timing on Generate.",
            "Generate creates an editable 16 / 32 / 64-step pattern.",
            "Preview loops without changing your song. A on Preview stops it.",
            "Apply needs A twice; only empty, unused chains / phrases are claimed.",
            "New music is appended after the last song row. Save your project.",
            "L / R changes the visible bar. Select or B closes this help."};
        for (int i=0;i<9;++i) c.draw_label(lines[i],30,173+i*27,i==7?accent:ink,12);
        return;
    }
    const bool assignments = s.row >= 9 && s.row <= 16;
    c.panel_rect(18,158,208,242,panel,with_alpha(muted,.18f),6);
    c.draw_label(assignments ? "SOURCES / LANE LOCKS" : "GROOVE SETTINGS",29,168,accent,12);
    for (int i=0;i<(assignments?8:9);++i) {
        const int row = assignments ? i+9 : i, y = 190+i*(assignments?25:22);
        const bool selected = s.row==row;
        if(selected)c.panel_rect(25,y-2,194,23,darken(accent,.16f),accent,3);
        if(assignments){
            const bool active=i<std::clamp(s.activeLanes,5,8);
            c.draw_label(shortRoles[i],31,y+2,active?ink:muted,12);
            c.draw_text(hex(s.lanes[i].instrument),83,y+2,selected&&s.col==0?accent:ink,1,2);
            const int id=s.lanes[i].instrument;
            std::string name=source_assigned(project,id)?project.instruments[id].name:"No source";
            c.draw_label(Canvas::clip_label(name,60,12),111,y+2,muted,12);
            c.draw_label(s.lanes[i].locked?"LOCK":"free",180,y+2,selected&&s.col==1?accent:muted,12);
        }else{
            c.draw_label(settings[i],31,y+2,selected?ink:muted,12);
            const std::string text=Canvas::clip_label(setting_value(s,i),109,12);
            c.draw_label(text,212-Canvas::label_width(text,12),y+2,selected?accent:ink,12);
        }
    }
    c.panel_rect(236,158,386,242,darken(panel,.78f),with_alpha(muted,.18f),6);
    const int pages=std::max(1,std::clamp(s.steps,16,64)/16), page=std::clamp(s.gridPage,0,pages-1);
    c.draw_label("STEP GRID",247,168,accent,12);
    c.draw_label("BAR "+std::to_string(page+1)+" / "+std::to_string(pages),520,168,muted,12);
    constexpr int gridX=296, gridY=193, cellW=19, cellH=24;
    for(int step=0;step<16;++step){
        if(step%4==0)c.draw_label(std::to_string(step/4+1),gridX+step*cellW+5,184,muted,12);
        if(s.previewRunning&&s.previewDisplayStep==page*16+step)c.fill_rect(gridX+step*cellW,201,18,192,with_alpha(accent,.13f));
    }
    for(int lane=0;lane<8;++lane){
        const bool active=lane<std::clamp(s.activeLanes,5,8);
        const int y=gridY+lane*cellH+12;
        c.draw_label(shortRoles[lane],247,y+1,active?muted:darken(muted,.4f),12);
        for(int step=0;step<16;++step){
            const auto& hit=s.grid[lane][page*16+step];
            const bool on=active&&s.generated&&hit.velocity>0;
            const Argb base=on?darken(accent,.38f+.62f*hit.velocity/127.f):with_alpha(muted,step%4==0?.15f:.07f);
            c.fill_rect(gridX+step*cellW+2,y+1,14,12,base);
            if(on&&hit.delayTicks)c.fill_rect(gridX+step*cellW+3,y+13,std::min(12,static_cast<int>(hit.delayTicks)),1,t.textTitle);
        }
    }
    const char* actions[]={"GENERATE","PREVIEW","APPLY TO SONG"};
    for(int i=0;i<3;++i){
        const bool selected=s.row==17+i; const int x=18+i*205;
        c.panel_rect(x,410,194,30,selected?accent:panel,selected?accent:with_alpha(muted,.23f),5);
        std::string text=i==1&&s.previewRunning?"STOP PREVIEW":i==2&&s.confirmApply?"A AGAIN TO APPEND":actions[i];
        c.draw_label(text,x+(194-Canvas::label_width(text,12))/2,418,selected?t.background:ink,12);
    }
    c.draw_label(Canvas::clip_label(s.status,604,12),18,449,s.dirty?t.textTitle:muted,12);
    c.draw_label("D-pad: rows / columns    A + arrows: edit    L / R: bar    Select: help",18,464,muted,12);
}
} // namespace pt::ui
