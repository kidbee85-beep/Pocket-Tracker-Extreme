#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/help_text.h"
#include "ui/helpers.h"
#include "ui/input_dispatcher.h"
#include "ui/layout.h"
#include "ui/settings_store.h"
#include "ui/std_filesystem.h"
#include "ui/theme_io.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace {

using namespace pt::ui;

constexpr std::array styles = {
    StepGridStyle::OFF, StepGridStyle::BEATS, StepGridStyle::GRID, StepGridStyle::DOTS,
};

void check_persistence(StdFileSystem& files) {
    Theme theme;
    SettingsValues settings;
    const auto legacy = R"({"appTheme":{"name":"OLD","surfaceStyle":3},"visualizer":2,"abxy":2})";
    assert(files.write_file(files.settings_path(), legacy));
    assert(load_settings(files, settings, theme));
    assert(theme.name == "OLD" && theme.surfaceStyle == 3 && settings.abxyIndex == 2);
    assert(theme.visualizerType == VisualizerType::OCTA);
    assert(theme.chainGrid == StepGridStyle::OFF && theme.phraseGrid == StepGridStyle::GRID);
    assert(parse_theme(R"({"chainGrid":"unknown","phraseGrid":-2})", theme));
    assert(theme.chainGrid == StepGridStyle::OFF && theme.phraseGrid == StepGridStyle::GRID);

    const std::string savedTheme = files.themes_directory() + "/Grid.ptt";
    for (StepGridStyle chain : styles) {
        for (StepGridStyle phrase : styles) {
            theme.chainGrid = chain;
            theme.phraseGrid = phrase;
            assert(save_settings(files, settings, theme));
            SettingsValues readSettings;
            Theme restored;
            assert(load_settings(files, readSettings, restored));
            assert(restored.chainGrid == chain && restored.phraseGrid == phrase);
            assert(readSettings.abxyIndex == settings.abxyIndex);
            assert(save_settings_if_changed(files, readSettings, restored) == SettingsWrite::UNCHANGED);
            assert(save_theme_file(files, savedTheme, theme));
            restored.chainGrid = chain == StepGridStyle::OFF ? StepGridStyle::DOTS : StepGridStyle::OFF;
            restored.phraseGrid = phrase == StepGridStyle::GRID ? StepGridStyle::OFF : StepGridStyle::GRID;
            restored.visualizerType = VisualizerType::OCTA_FULL;
            assert(load_theme_file(files, savedTheme, restored));
            assert(restored.chainGrid == chain && restored.phraseGrid == phrase);
            assert(restored.visualizerType == VisualizerType::OCTA_FULL);
        }
    }
}

void check_controls(StdFileSystem& files) {
    auto engine = std::make_unique<AudioEngine>();
    engine->setDeviceSampleRate(48000);
    songcore::SongcoreHost host(engine.get(), 48000);
    assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
    const std::string unchanged = songcore::serialize_project(host.project());
    AppState state;
    state.project = &host.edit_project();
    state.currentScreen = ScreenType::SETTINGS;
    state.settingsCursorRow = static_cast<int>(SettingsRow::THEME);
    state.settingsCursorColumn = 1;
    InputDispatcher input(state, host, files);

    input.on_button_a();
    assert(state.themeEditor.isOpen && state.themeEditor.cursorRow == THEME_ROW_THEME);
    input.on_dpad_down();
    assert(state.themeEditor.cursorRow == THEME_ROW_CHAIN_GRID);
    assert(help_topic(state) == HelpTopic::THEME_CHAIN_GRID);
    input.on_dpad_right();
    assert(state.themeEditor.cursorChannel == 0 && state.theme.chainGrid == StepGridStyle::OFF);
    input.on_button_a();
    assert(state.theme.chainGrid == StepGridStyle::BEATS);
    input.on_a_right();
    assert(state.theme.chainGrid == StepGridStyle::GRID);
    input.on_a_right();
    assert(state.theme.chainGrid == StepGridStyle::DOTS);
    input.on_a_right();
    assert(state.theme.chainGrid == StepGridStyle::OFF);
    input.on_a_left();
    assert(state.theme.chainGrid == StepGridStyle::DOTS);
    input.on_dpad_down();
    assert(state.themeEditor.cursorRow == THEME_ROW_PHRASE_GRID);
    assert(help_topic(state) == HelpTopic::THEME_PHRASE_GRID);
    assert(state.theme.phraseGrid == StepGridStyle::GRID);
    input.on_a_left();
    assert(state.theme.chainGrid == StepGridStyle::DOTS && state.theme.phraseGrid == StepGridStyle::BEATS);

    input.on_dpad_up();
    input.on_dpad_up();
    const auto previous = state.theme.name;
    input.on_a_right();
    assert(previous != state.theme.name);
    assert(state.theme.chainGrid == StepGridStyle::DOTS && state.theme.phraseGrid == StepGridStyle::BEATS);
    input.on_button_b();
    assert(!state.themeEditor.isOpen && state.currentScreen == ScreenType::SETTINGS);
    assert(songcore::serialize_project(host.project()) == unchanged);

    // Changing the view never changes where phrase slots or notes are placed.
    std::string expected;
    for (StepGridStyle style : styles) {
        assert(host.push_project(unchanged));
        state.project = &host.edit_project();
        state.currentChain = 0;
        state.currentPhrase = 0;
        state.theme.chainGrid = style;
        state.theme.phraseGrid = style;
        state.currentScreen = ScreenType::CHAIN;
        state.cursorColumn = 1;
        state.cursorRow = 3;
        input.on_button_a();
        assert(host.project().chains[0].phraseRefs[3] >= 0);
        state.currentScreen = ScreenType::PHRASE;
        state.cursorColumn = 1;
        state.cursorRow = 7;
        input.on_button_a();
        assert(songcore::step_has_note(host.project().phrases[0].steps[7]));
        const auto edited = songcore::serialize_project(host.project());
        if (expected.empty()) expected = edited;
        assert(edited == expected);
    }
}

constexpr int row_top(int row) { return EDITOR_Y + 2 * ROW_HEIGHT + 14 + row * ROW_HEIGHT; }

Argb pixel(const Canvas& canvas, int x, int y) { return canvas.pixels()[y * DESIGN_W + x]; }

int count_ink(const Canvas& canvas, int x, int y, int width, int height, Argb color) {
    int count = 0;
    for (int dy = 0; dy < height; ++dy)
        for (int dx = 0; dx < width; ++dx)
            count += pixel(canvas, x + dx, y + dy) == color;
    return count;
}

void check_rendering() {
    auto project = songcore::make_default_project();
    project.phrases[0].steps[5].note = songcore::Note::C4();
    project.chains[0].phraseRefs[5] = 0;
    AppState state;
    state.project = &project;
    state.theme = theme_classic();
    state.cursorColumn = 1;
    state.cursorRow = 5;
    state.isPlaying = true;
    state.playheads[0] = {0, 0, 7, 0, 7};

    TrackerLayout layout;
    auto canvas = std::make_unique<Canvas>();
    for (ScreenType screen : {ScreenType::CHAIN, ScreenType::PHRASE}) {
        state.currentScreen = screen;
        for (StepGridStyle style : styles) {
            state.theme.chainGrid = screen == ScreenType::CHAIN ? style : StepGridStyle::OFF;
            state.theme.phraseGrid = screen == ScreenType::PHRASE ? style : StepGridStyle::OFF;
            layout.draw(*canvas, state);
            constexpr int gridDot = SIDE_SPACER + 480, gridGap = gridDot + 6;
            constexpr int divider = SIDE_SPACER + 44;
            for (int beat : {0, 4, 8, 12}) {
                assert((pixel(*canvas, gridDot, row_top(beat)) != state.theme.rowEvery4th)
                       == (style != StepGridStyle::OFF));
                const bool continuous = style == StepGridStyle::BEATS || style == StepGridStyle::GRID;
                assert((pixel(*canvas, gridGap, row_top(beat)) != state.theme.rowEvery4th) == continuous);
            }
            const bool dividers = style == StepGridStyle::GRID || style == StepGridStyle::DOTS;
            assert((pixel(*canvas, divider, row_top(2) + 5) != state.theme.background) == dividers);
            assert((pixel(*canvas, divider, row_top(2) + 8) != state.theme.background)
                   == (style == StepGridStyle::GRID));
            constexpr int cellX = SIDE_SPACER + 50;
            constexpr int playheadX = SIDE_SPACER + 10 + CHAR_W;
            assert(count_ink(*canvas, cellX, row_top(5), 30, ROW_HEIGHT, state.theme.rowCursor) > 0);
            assert(count_ink(*canvas, playheadX, row_top(7), CHAR_W, ROW_HEIGHT, state.theme.textPlayhead) > 0);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto output = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(output);
    StdFileSystem files(output.string());
    check_persistence(files);
    check_controls(files);
    check_rendering();
    std::puts("PASS: independent Chain/Phrase guides, all 16 persisted combinations, theme presets, "
              "input/help, unchanged edits, beat and column pixels, cursor and playhead");
}
