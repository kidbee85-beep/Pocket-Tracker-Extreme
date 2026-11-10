#pragma once
// Stable saved IDs. Typeface selection is independent of palette/material and music data.
namespace pt::ui {
inline constexpr int kDemoFontStyleCount=17;
inline int valid_demo_font_style(int style){return style>=0&&style<kDemoFontStyleCount?style:0;}
inline const char* demo_font_style_name(int style){
    static constexpr const char* names[]={"CLASSIC","TERMINAL 89","RASTER 91","ACID VECTOR","CRACKTRO","BIT ROT","NULL SIGNAL",
        "CLEAR MONO","STUDIO SANS","COMPACT GRID","SOFT PIXEL","ISO TERMINAL","WIDE SCREEN","PAPER TYPE","LCD CLEAN","RAIL MONO","NIGHT EDITOR"};
    return names[valid_demo_font_style(style)];
}
inline const char* demo_font_style_detail(int style){
    static constexpr const char* descriptions[]={
        "Original interface / unchanged metrics",
        "Sharp terminal pixels / coded after midnight",
        "Raster-era geometry / rhythm in every cell",
        "Angular vector cuts / neon machinery",
        "Chunky crack-intro lettering / dense and loud",
        "Broken stencil edges / stable readable figures",
        "Fractured transmission / controlled digital damage",
        "Open counters / clear figures / pair with CLEAR DESK",
        "Calm studio lettering / pair with STUDIO NIGHT",
        "Compact and legible / pair with GRID SLATE",
        "Softer pixel corners / pair with SOFT MINT",
        "Precise technical shapes / pair with ISO BLUE",
        "Open wide letterforms / pair with WIDE HORIZON",
        "Crisp paper-like lettering / pair with PAPER INK",
        "Unbroken digital forms / pair with LCD TEAL",
        "Straight technical rhythm / pair with RAIL AMBER",
        "Quiet medium weight / pair with NIGHT FOCUS"};
    return descriptions[valid_demo_font_style(style)];
}
}
