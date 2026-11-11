#pragma once

// The palette and the four built-ins. ⚠️ The field names are the `.ptt` file's keys — a rename breaks
// the format.

#include <cstdint>
#include <string>
#include <vector>

namespace pt::ui {

using Argb = uint32_t;  // 0xAARRGGBB

enum class VisualizerType { SCOPE, FLAT, OCTA, OCTA_FULL, SPECTRUM, SPECTRUM_PEAKS };

enum class StepGridStyle { OFF, BEATS, GRID, DOTS };

inline constexpr int STEP_GRID_STYLE_COUNT = 4;

inline const char* step_grid_style_name(StepGridStyle style) {
    switch (style) {
        case StepGridStyle::OFF:   return "OFF";
        case StepGridStyle::BEATS: return "BEATS";
        case StepGridStyle::GRID:  return "GRID";
        case StepGridStyle::DOTS:  return "DOTS";
    }
    return "OFF";
}

struct Theme {
    std::string name = "CLASSIC";

    // ── Row backgrounds ──────────────────────────────────────────────────────────────────────────
    Argb background   = 0xFF0A0A0A;  // module fill + default row
    Argb rowEvery4th  = 0xFF151515;  // beat-accent rows (every 4th)

    // The palette's accent: the cursor bar, every "you are here" mark, the EQ curve. `background` is
    // the ink on it.
    Argb rowCursor    = 0xFFFFFF00;
    // Paints nothing: the seed of `textPlayhead`'s default for a `.ptt` without TXT PLAY.
    Argb rowPlayback  = 0xFF004400;

    // A second bright ground, near the cursor's (`rowEvery4th` is the ink on it).
    Argb rowSelection = 0xFF00CC00;

    // ── Text roles ───────────────────────────────────────────────────────────────────────────────
    Argb textTitle  = 0xFF00FFFF;  // screen headers (cyan)
    Argb textParam  = 0xFF808080;  // inactive param label
    Argb textValue  = 0xFFFFFFFF;  // inactive param value
    Argb textEmpty  = 0xFF666666;  // empty / placeholder

    // ⚠️ Drawn by nothing, but still `.ptt` keys — parsed and written, so they stay.
    Argb textCursor = 0xFFFFFF00;
    Argb textSelection = 0xFF00FF00;  // = vizWave

    // The `>` playback marker. Its default is lifted from `rowPlayback`; once in a file, used as typed.
    Argb textPlayhead = 0xFF00E000;

    // ── Visualizer (oscilloscope bar) ────────────────────────────────────────────────────────────
    Argb vizBackground = 0xFF0A0A0A;
    Argb vizCenterLine = 0xFF333333;
    Argb vizWave       = 0xFF00FF00;  // waveform line / bar fill

    // ── The EQ editor's panel: CLASSIC's values of `derive_borrowed_colors`, which is the authority ──
    Argb eqBg     = 0xFF0A0A0A;  // = vizBackground
    Argb eqFill   = 0xFF222222;  // = darken(textParam, 0.27f)
    Argb eqBorder = 0xFF808080;  // = textParam — the spectrum outline AND the 0 dB line
    Argb eqTxt    = 0xFF333333;  // = vizCenterLine

    // ── Mixer dBFS meters ────────────────────────────────────────────────────────────────────────
    Argb meterBackground = 0xFF1A1A1A;
    Argb meterLow        = 0xFF00CC00;
    Argb meterMid        = 0xFFCCCC00;
    Argb meterHigh       = 0xFFCC0000;
    Argb meterBorder     = 0xFF444444;

    // ── Visualizer mode ──────────────────────────────────────────────────────────────────────────
    VisualizerType visualizerType = VisualizerType::SCOPE;
    int surfaceStyle = 0;
    StepGridStyle chainGrid = StepGridStyle::OFF;
    StepGridStyle phraseGrid = StepGridStyle::GRID;
};

// Append only: the integer is persisted by .ptt files and settings.json.
inline constexpr int kSurfaceStyleCount=18;
inline const char* surface_style_name(int style) {
    static const char* names[] = {"Fine Grain", "Brushed Metal", "Carbon Weave", "Paper Fiber",
                                  "Circuit", "Precision Grid", "Silk", "Flat",
                                  "Ceramic", "Micro Perforation", "Etched Contours", "Anodized",
                                  "Bunka Crosshatch", "Raster Glass", "Vector Horizon",
                                  "Isometric Tiles", "Pixel Constellation", "Bunka Circuit"};
    return names[style < 0 ? 0 : style >= kSurfaceStyleCount ? kSurfaceStyleCount-1 : style];
}

// Scale the RGB channels by `factor`; alpha kept.
inline Argb darken(Argb c, float factor) {
    auto ch = [&](int shift) {
        const int v = static_cast<int>(static_cast<float>((c >> shift) & 0xFF) * factor);
        return static_cast<Argb>(v < 0 ? 0 : (v > 255 ? 255 : v));
    };
    return (c & 0xFF000000u) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// A dark seed lifted to a readable ink of the same hue (pure black: `fallback`). Only ever a default,
// never applied to a typed value.
inline Argb lift_seed_to_ink(Argb seed, Argb fallback) {
    constexpr int TARGET = 0xE0;
    const int r = (seed >> 16) & 0xFF, g = (seed >> 8) & 0xFF, b = seed & 0xFF;
    const int peak = (r > g ? r : g) > b ? (r > g ? r : g) : b;
    if (peak == 0) return fallback;
    if (peak >= TARGET) return 0xFF000000u | (seed & 0x00FFFFFFu);
    return darken(seed, static_cast<float>(TARGET) / static_cast<float>(peak));
}

// The default of six keys, derived from the rest of the palette. `parse_theme` fills in what a file
// lacks and `serialize_theme` omits what still equals it.
// ⚠️ Every producer of a Theme calls it last.
inline void derive_borrowed_colors(Theme& t) {
    t.eqBg     = t.vizBackground;
    t.eqFill   = darken(t.textParam, 0.27f);
    t.eqBorder = t.textParam;
    t.eqTxt    = t.vizCenterLine;

    t.textSelection = t.vizWave;
    t.textPlayhead  = lift_seed_to_ink(t.rowPlayback, t.textCursor);
}

// ─── The theme editor's rows ─────────────────────────────────────────────────────────────────────
// Four fields have none: `meterBorder`, `rowPlayback` (a seed), `textCursor` and `textSelection`
// (unused). ⚠️ A row's position is its number in the help text and the tests: append.

struct ThemeColorRow {
    const char* label;
    Argb Theme::* field;
};

inline const std::vector<ThemeColorRow>& theme_color_rows() {
    static const std::vector<ThemeColorRow> rows = {
        {"BACKGROUND", &Theme::background},
        {"ROW 4TH",    &Theme::rowEvery4th},
        {"ROW CURSOR", &Theme::rowCursor},
        {"ROW SELECT", &Theme::rowSelection},
        {"TXT TITLE",  &Theme::textTitle},
        {"TXT PARAM",  &Theme::textParam},
        {"TXT VALUE",  &Theme::textValue},
        {"TXT EMPTY",  &Theme::textEmpty},
        {"TXT PLAY",   &Theme::textPlayhead},
        {"VIZ BG",     &Theme::vizBackground},
        {"VIZ LINE",   &Theme::vizCenterLine},
        {"VIZ WAVE",   &Theme::vizWave},
        {"MTR BG",     &Theme::meterBackground},
        {"MTR LOW",    &Theme::meterLow},
        {"MTR MID",    &Theme::meterMid},
        {"MTR HIGH",   &Theme::meterHigh},
        {"EQ FILL",    &Theme::eqFill},
        {"EQ BORDER",  &Theme::eqBorder},
        {"EQ TXT",     &Theme::eqTxt},
        {"EQ BG",      &Theme::eqBg},
    };
    return rows;
}

inline Theme theme_studio() {
    Theme t;
    t.name="STUDIO";t.background=0xFF101521;t.rowEvery4th=0xFF1B2435;
    t.rowCursor=0xFF65E4C3;t.rowSelection=0xFFAD8DFF;t.textTitle=0xFF91AFFF;
    t.textParam=0xFFA8B6CC;t.textValue=0xFFEDF2FF;t.textEmpty=0xFF75859E;
    t.vizBackground=0xFF101521;t.vizCenterLine=0xFF354357;t.vizWave=0xFF65E4C3;
    t.meterLow=0xFF65E4C3;t.meterMid=0xFFF2CD79;t.meterHigh=0xFFF5809B;
    derive_borrowed_colors(t);
    return t;
}

inline Theme theme_palette(const char* name, Argb ground, Argb panel, Argb accent,
                           Argb title, Argb selection) {
    Theme t = theme_studio();
    t.name = name;
    t.background = ground;
    t.rowEvery4th = panel;
    t.rowCursor = accent;
    t.rowSelection = selection;
    t.textTitle = title;
    t.vizBackground = ground;
    t.vizWave = accent;
    t.rowPlayback = darken(accent, .3f);
    t.vizCenterLine = darken(title, .3f);
    t.meterBackground = panel;
    t.meterBorder = darken(title, .35f);
    t.meterLow = accent;
    derive_borrowed_colors(t);
    return t;
}

inline Theme theme_classic() { Theme t;derive_borrowed_colors(t);return t;}

inline Theme theme_amber() {
    Theme t;
    t.name          = "AMBER";
    t.background    = 0xFF0A0808;
    t.rowEvery4th   = 0xFF151212;
    t.rowCursor     = 0xFFFFBB00;
    t.rowPlayback   = 0xFF332200;
    t.rowSelection  = 0xFFCC6600;
    t.textTitle     = 0xFFFFBB00;
    t.textParam     = 0xFF806040;
    t.textValue     = 0xFFEECC88;
    t.textEmpty     = 0xFF664422;
    t.vizBackground = 0xFF0A0808;
    t.vizCenterLine = 0xFF382404;
    t.vizWave       = 0xFFFF8800;
    t.meterBackground = 0xFF1A1515;
    t.meterLow      = 0xFFCC8800;
    t.meterMid      = 0xFFCC4400;
    t.meterHigh     = 0xFFCC0000;
    derive_borrowed_colors(t);
    return t;
}

inline Theme theme_blue() {
    Theme t;
    t.name          = "BLUE";
    t.rowCursor     = 0xFF66AEDC;
    t.rowPlayback   = 0xFF002266;
    t.rowSelection  = 0xFF3E7FA8;
    t.textTitle     = 0xFF88CEFF;
    t.textParam     = 0xFF4486AA;
    t.textValue     = 0xFFAADDFF;
    t.textEmpty     = 0xFF224466;
    t.vizCenterLine = 0xFF112244;
    t.vizWave       = 0xFF0082BA;
    t.meterBackground = 0xFF151515;
    t.meterLow      = 0xFF0082BA;
    t.meterMid      = 0xFF004499;
    t.meterHigh     = 0xFF6050A0;
    derive_borrowed_colors(t);
    // After the derive, which would overwrite these two.
    t.eqBorder      = 0xFF4488AA;
    t.textPlayhead  = 0xFF224466;
    return t;
}

inline Theme theme_mono() {
    Theme t;
    t.name          = "MONO";
    t.rowCursor     = 0xFFE8E8E8;
    t.rowPlayback   = 0xFF444444;
    t.rowSelection  = 0xFFA8A8A8;
    t.textTitle     = 0xFFFFFFFF;
    t.textParam     = 0xFF8B8E8E;   // a shade off TXT VALUE on purpose
    t.textValue     = 0xFFC0C0C0;
    t.textEmpty     = 0xFF444444;
    t.vizCenterLine = 0xFF222222;
    t.vizWave       = 0xFFCCCCCC;
    t.meterLow      = 0xFFB0B0B0;
    t.meterMid      = 0xFF808080;
    t.meterHigh     = 0xFF444444;
    derive_borrowed_colors(t);
    return t;
}

// AMBER on a black ground, for OLED screens.
inline Theme theme_amber_oled() {
    Theme t = theme_amber();
    t.name          = "AMBER OLED";
    t.background    = 0xFF000000;
    t.textValue     = 0xFFCC8800;
    t.vizBackground = 0xFF000000;
    derive_borrowed_colors(t);
    return t;
}

// BLUE on a black ground, for OLED screens.
inline Theme theme_blue_oled() {
    Theme t = theme_blue();
    t.name            = "BLUE OLED";
    t.background      = 0xFF000000;
    t.rowEvery4th     = 0xFF0A0A0A;
    t.vizBackground   = 0xFF000000;
    t.meterBackground = 0xFF0A0A0A;
    derive_borrowed_colors(t);
    // After the derive, which would overwrite these two.
    t.eqBorder      = 0xFF4488AA;
    t.textPlayhead  = 0xFF224466;
    return t;
}

inline Theme theme_bubblegum() {
    Theme t;
    t.name          = "BUBBLEGUM";
    t.background    = 0xFF0D100F;
    t.rowEvery4th   = 0xFF081F1C;
    t.rowCursor     = 0xFF00BBAA;
    t.rowPlayback   = 0xFF002266;
    t.rowSelection  = 0xFFFF8DAC;
    t.textTitle     = 0xFF39B0A1;
    t.textParam     = 0xFF9C7F85;
    t.textValue     = 0xFFBC949D;
    t.textEmpty     = 0xFF5E7A75;
    t.vizBackground = 0xFF0D100F;
    t.vizCenterLine = 0xFF213330;
    t.vizWave       = 0xFFBC6A80;
    t.meterBackground = 0xFF11201E;
    t.meterLow      = 0xFF00C3B1;
    t.meterMid      = 0xFFAF9000;
    t.meterHigh     = 0xFFC84D74;
    t.meterBorder   = 0xFF333535;
    derive_borrowed_colors(t);
    // After the derive, which would overwrite these.
    t.eqFill        = 0xFF233935;
    t.eqBorder      = 0xFFC6788C;
    t.eqTxt         = 0xFF557772;
    t.textPlayhead  = 0xFF489086;
    return t;
}

// The built-ins, in cycle order. `visualizerType` is the user's, not the theme's: a swap keeps it.
// ⚠️ A name is what `settings.json` stores, so a renamed built-in reads back as CLASSIC.
inline std::vector<Theme> theme_builtins() {
    std::vector<Theme> themes = {theme_studio(),
            theme_palette("MIDNIGHT", 0xFF0B1020, 0xFF1A2542, 0xFF80B7FF, 0xFFB4CFFF, 0xFFBC9BFF),
            theme_palette("PLUM", 0xFF191222, 0xFF2E203C, 0xFFD6A5FF, 0xFFE9CFFF, 0xFF81DDCC),
            theme_palette("MOSS", 0xFF101A17, 0xFF1E3028, 0xFFAAD995, 0xFFD8E9BF, 0xFFE7C28E),
            theme_palette("COPPER", 0xFF1B1512, 0xFF33261F, 0xFFFFBC87, 0xFFFFDBBC, 0xFF9ACBC7),
            theme_palette("GLACIER", 0xFF101B23, 0xFF20333F, 0xFF8EE5EF, 0xFFC6E7FF, 0xFFB6B5FF),
            theme_palette("ROSE", 0xFF20141C, 0xFF382330, 0xFFFFAFC7, 0xFFFFD7E3, 0xFFB6C4FF),
            theme_palette("GRAPHITE", 0xFF14171B, 0xFF272D35, 0xFFD8E5F0, 0xFFF0F4FA, 0xFF9CBFD1),
            theme_palette("ULTRAVIOLET", 0xFF131328, 0xFF252444, 0xFFA6A3FF, 0xFFD7D5FF, 0xFFF6B4D0),
            theme_classic(), theme_amber(), theme_blue(), theme_mono(),
            theme_bubblegum(), theme_blue_oled(), theme_amber_oled(),
            theme_palette("CARBON", 0xFF11151A, 0xFF202A32, 0xFF69E1BE, 0xFFDEEAF2, 0xFF9DB2F6),
            theme_palette("TITANIUM", 0xFF20252B, 0xFF343E48, 0xFFB7D5ED, 0xFFF1F5F8, 0xFFE6BE83),
            theme_palette("SAHARA", 0xFF29231C, 0xFF3C3428, 0xFFF2CB87, 0xFFF8E9CD, 0xFF9BCFBB),
            theme_palette("DEEP CIRCUIT", 0xFF0D191A, 0xFF173338, 0xFF73E9D9, 0xFFCCFAF0, 0xFFEDBC8F),
            theme_palette("VELVET", 0xFF21182A, 0xFF38263D, 0xFFF2B7D9, 0xFFFFE1F1, 0xFFB3B8FF),
            theme_palette("BLUEPRINT", 0xFF101E36, 0xFF1B3551, 0xFF9ADCF5, 0xFFE2F3FF, 0xFFFFCE96),
            theme_palette("EMBER", 0xFF241919, 0xFF3C2826, 0xFFFFAC83, 0xFFFFE3C6, 0xFFF2D38C),
            theme_palette("OBSIDIAN", 0xFF101116, 0xFF222431, 0xFFBCB8F9, 0xFFE7E6FF, 0xFF8FE1D3),
            theme_palette("PORCELAIN NOIR", 0xFF161C22, 0xFF29313A, 0xFFC7E2E4, 0xFFF0F5F5, 0xFFE7BB91),
            theme_palette("AURORA", 0xFF101A23, 0xFF20333F, 0xFF86E9CA, 0xFFD0E9FF, 0xFFD8B4ED),
            theme_palette("OXBLOOD", 0xFF21171D, 0xFF392731, 0xFFEEB89F, 0xFFFFE6D8, 0xFFB5CFDD),
            theme_palette("PETROL", 0xFF101E21, 0xFF20383C, 0xFFA9DCCB, 0xFFDCF4ED, 0xFFE6C188),
            theme_palette("MOONSTONE", 0xFF1B1D28, 0xFF2D3143, 0xFFBABEEB, 0xFFE7EAFE, 0xFFA6D8D0),
            theme_palette("BRASS CONSOLE", 0xFF201E18, 0xFF353127, 0xFFE7C38E, 0xFFF5E9D5, 0xFFA2CCC0),
            theme_palette("OCEAN GLASS", 0xFF111C2D, 0xFF20354B, 0xFF8ED9EB, 0xFFDFF3FF, 0xFFCEB8ED),
            theme_palette("FOREST VELVET", 0xFF171F1C, 0xFF28372F, 0xFFB4D7A7, 0xFFE7F0DD, 0xFFE0BB9C),
            theme_palette("BUNKA LAB", 0xFF101523, 0xFF222B40, 0xFF8DE7CF, 0xFFE1F4F2, 0xFFBCA9F3),
            theme_palette("VIOLET VECTOR", 0xFF171329, 0xFF2E2546, 0xFFC4ADFF, 0xFFF0E8FF, 0xFF96DFDD),
            theme_palette("RASTER BLOOM", 0xFF121E24, 0xFF22353C, 0xFF9CE9DE, 0xFFE5F8F3, 0xFFE8BEA7),
            theme_palette("CHROME DUSK", 0xFF1D1925, 0xFF322D40, 0xFFE6BDCF, 0xFFFFEDF4, 0xFFA9CBEC),
            theme_palette("ISO STATION", 0xFF131C24, 0xFF263544, 0xFFA8D4F1, 0xFFE7F3FF, 0xFFEAC38E),
            theme_palette("PIXEL GARDEN", 0xFF141F1D, 0xFF273B34, 0xFFB2E6B5, 0xFFECF8DD, 0xFFE0BE8C),
            theme_palette("COPPER ROUTES", 0xFF211A18, 0xFF3B2D28, 0xFFF2C393, 0xFFFFEAD2, 0xFFA0D8CC),
            theme_palette("NIGHT ASSEMBLY", 0xFF111621, 0xFF252D3E, 0xFFB7C4F4, 0xFFE9EEFF, 0xFF9DE4D4),
            // Append only: calm companion palettes for the ten clean typefaces.
            theme_palette("CLEAR DESK", 0xFF10171D, 0xFF202D38, 0xFFCEE88A, 0xFFF1F5EA, 0xFF88D4E8),
            theme_palette("STUDIO NIGHT", 0xFF121621, 0xFF242C3D, 0xFFA7C9FF, 0xFFEDF3FF, 0xFFB5D8CF),
            theme_palette("GRID SLATE", 0xFF141A20, 0xFF28343D, 0xFFD7E4ED, 0xFFF3F6F8, 0xFF96C4D9),
            theme_palette("SOFT MINT", 0xFF111C1A, 0xFF223630, 0xFFADE1C3, 0xFFEEF8F2, 0xFFE3CB9A),
            theme_palette("ISO BLUE", 0xFF101925, 0xFF21364A, 0xFF9DCEE9, 0xFFEEF6FC, 0xFFE7CB9B),
            theme_palette("WIDE HORIZON", 0xFF171823, 0xFF303044, 0xFFC9BCEB, 0xFFF4F0FC, 0xFF9FD6DD),
            theme_palette("PAPER INK", 0xFF201F1B, 0xFF38362F, 0xFFE7D7B1, 0xFFFAF3E3, 0xFFB5D7CA),
            theme_palette("LCD TEAL", 0xFF0C1C1F, 0xFF1D353B, 0xFF97DED9, 0xFFE6F7F4, 0xFFE2C79C),
            theme_palette("RAIL AMBER", 0xFF1D1813, 0xFF352B21, 0xFFE6C080, 0xFFFBF0DC, 0xFFA3C8D6),
            theme_palette("NIGHT FOCUS", 0xFF13161C, 0xFF262D36, 0xFFBBC9DE, 0xFFF1F4F8, 0xFF9BCDC1)};
    const int styles[] = {0,5,6,3,1,5,6,1,4,7,1,5,7,6,7,7,2,1,3,4,6,5,0,2,8,10,11,9,8,11,10,9,
                          12,14,13,12,15,16,17,17, 7,8,5,6,5,14,3,7,1,7};
    static_assert(sizeof(styles)/sizeof(styles[0])==50,"Each built-in palette needs a material");
    for(size_t i=0;i<themes.size();++i) themes[i].surfaceStyle=styles[i];
    return themes;
}

// A first launch's look: BLUE with the OCTA bars. (`Theme{}` is CLASSIC.)
inline Theme theme_default() {
    Theme t          = theme_studio();
    t.visualizerType = VisualizerType::OCTA;
    return t;
}

// A built-in by name, keeping `visualizer`; unknown reads as CLASSIC.
inline Theme theme_by_name(const std::string& name, VisualizerType visualizer) {
    Theme found = theme_classic();
    for (const Theme& t : theme_builtins()) {
        if (t.name == name) { found = t; break; }
    }
    found.visualizerType = visualizer;
    return found;
}

}  // namespace pt::ui
