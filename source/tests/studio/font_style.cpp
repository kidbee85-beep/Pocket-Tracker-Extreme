#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/canvas.h"
#include "ui/demo_font_style.h"
#include "ui/demo_fonts.h"
#include "ui/settings_store.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"
#include "ui/studio_help.h"
#include "ui/theme_io.h"
#include <array>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <new>
#include <set>
#include <vector>

namespace {thread_local bool guardAllocations=false;thread_local size_t allocations=0;}
void* operator new(size_t n){if(guardAllocations)++allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
void operator delete[](void*p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t alignment){if(guardAllocations)++allocations;void*p=nullptr;if(posix_memalign(&p,size_t(alignment),n?n:1)==0)return p;throw std::bad_alloc();}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void*p,std::align_val_t)noexcept{std::free(p);}
void operator delete(void*p,size_t,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void*p,size_t,std::align_val_t)noexcept{std::free(p);}

using namespace pt::ui;
namespace {
constexpr Argb background=0xff07111b;
uint64_t hash(const Canvas& c){uint64_t h=1469598103934665603ull;for(int i=0;i<DESIGN_W*DESIGN_H;++i){h^=c.pixels()[i];h*=1099511628211ull;}return h;}
void reference_frame(Canvas& c){
    c.clear(background);const std::string grid="00 C#4 F-3 0123456789 ABCDEFG Z!?+-.,:/%";
    const std::string label="Jazz / Soul Cafe é Ä ß … +12.5dB 92 BPM";
    for(int scale=1;scale<=4;++scale)c.draw_text(grid,7,20+scale*32,0xffa8ecd8,1,scale);
    c.draw_label(label,7,195,0xffeff3fa,12);c.draw_label(label,7,225,0xffbca9f3,18);
    {Canvas::ClipScope clip(c,17,270,601,46);c.draw_text(grid,-11,263,0x8fe8c18c,1,3);c.draw_label(label,-21,299,0xffe1f4f2,18);}
}
void canvas_checks(){
    static_assert(detail::demoFontAtlasBytes<=1536u*1024u,"Keep immutable typeface atlases within 1.5 MiB");
    // Captured independently from the frozen beta.3 canvas.cpp before typeface work.
    // It covers grid scales1..4, proportional Latin1/ellipsis, alpha and clipped edges.
    constexpr uint64_t classicGolden=7351093925236876406ull;
    // Independently rendered before expanding the original Classic + six faces.
    constexpr std::array<uint64_t,7> originalGoldens={7351093925236876406ull,8482447897106181637ull,5550261192330175671ull,13553404475441328855ull,13840162511215949307ull,10393593572569081580ull,2530385591370617908ull};
    Canvas a,b;reference_frame(a);assert(a.font_style()==0&&hash(a)==classicGolden);
    b.set_font_style(6);reference_frame(b);assert(hash(b)!=classicGolden&&a.font_style()==0);
    const auto styled=hash(b);a.set_font_style(1);reference_frame(a);assert(b.font_style()==6&&hash(b)==styled);
    for(int invalid:{-1,kDemoFontStyleCount,INT_MIN,INT_MAX}){a.set_font_style(invalid);reference_frame(a);assert(a.font_style()==0&&hash(a)==classicGolden);}
    std::set<uint64_t> signatures;
    const std::string sample="00 C#4 F-3 0123456789 ABCDEFG / Jazz é …";
    const auto width12=Canvas::label_width(sample,12),width18=Canvas::label_width(sample,18);
    const auto clipped12=Canvas::clip_label(sample,123,12),clipped18=Canvas::clip_label(sample,123,18);
    const auto gridWidth=Canvas::text_width(sample,1,3);
    for(int style=0;style<kDemoFontStyleCount;++style){
        a.set_font_style(style);reference_frame(a);assert(signatures.insert(hash(a)).second);
        if(style<int(originalGoldens.size()))assert(hash(a)==originalGoldens[size_t(style)]);
        assert(Canvas::label_width(sample,12)==width12&&Canvas::label_width(sample,18)==width18);
        assert(Canvas::clip_label(sample,123,12)==clipped12&&Canvas::clip_label(sample,123,18)==clipped18);
        assert(Canvas::text_width(sample,1,3)==gridWidth);
        for(bool label:{false,true})for(int size:{1,2,3,4})for(int edge=0;edge<3;++edge){
            a.clear(background);const int x=edge==2?619:edge==1?0:17,y=edge==2?461:edge==1?0:23,w=21,h=19;
            a.set_clip(x,y,w,h);
            if(label)a.draw_label(sample,x-13,y-7,0x8fffce96,size<3?12:18);
            else a.draw_text(sample,x-13,y-7,0x8fffce96,1,size);
            bool ink=false;
            for(int py=0;py<DESIGN_H;++py)for(int px=0;px<DESIGN_W;++px){
                const bool inside=px>=x&&px<x+w&&py>=y&&py<y+h;
                if(!inside)assert(a.pixels()[py*DESIGN_W+px]==background);
                else ink|=a.pixels()[py*DESIGN_W+px]!=background;
            }
            if(size>=2)assert(ink);a.reset_clip();
        }
        // Proportional browser labels retain Latin1 letters and punctuation at both sizes.
        // Require real, distinct ink instead of accepting blank/fallback text as coverage.
        for(int size:{12,18}){
            auto labelGlyph=[&](const std::string& text){a.clear(background);a.draw_label(text,30,30,0xffffffff,size);return hash(a);};
            const auto blank=labelGlyph("");
            for(const auto& letter:{"é","Ä","ß","…"})assert(labelGlyph(letter)!=blank);
            assert(labelGlyph("é")!=labelGlyph("e")&&labelGlyph("Ä")!=labelGlyph("A"));
            if(style>=7){
                std::set<uint64_t> readable;for(const auto& letter:{"1","I","l"})assert(readable.insert(labelGlyph(letter)).second);
                // Narrow proportional cells must keep their stems after target-size hinting.
                // Unequal blank/accent fragments would otherwise satisfy hash-only checks.
                for(const auto& letter:{"I","i","l","j","f","t","Í","í","Î","î","Ï","ï"}){
                    labelGlyph(letter);int inkRows=0;
                    for(int y=0;y<DESIGN_H;++y){bool ink=false;for(int x=0;x<DESIGN_W;++x)ink|=a.pixels()[y*DESIGN_W+x]!=background;inkRows+=ink;}
                    assert(inkRows>=6);
                }
            }
        }
        // The fonts must preserve the actual data alphabet and distinguish common ambiguous values.
        if(style)for(int scale:{2,3}){
            std::set<uint64_t> digits;
            auto glyph=[&](char ch){a.clear(background);a.draw_text(std::string(1,ch),30,30,0xffffffff,1,scale);return hash(a);};
            for(char ch='0';ch<='9';++ch)assert(digits.insert(glyph(ch)).second);
            assert(glyph('0')!=glyph('O')&&glyph('1')!=glyph('I')&&glyph('8')!=glyph('B'));
            // The ten clean faces must also distinguish lowercase l in filenames.
            if(style>=7)assert(glyph('1')!=glyph('l')&&glyph('I')!=glyph('l'));
            std::set<uint64_t> notes;for(char ch='A';ch<='G';++ch)assert(notes.insert(glyph(ch)).second);
        }
    }
    a.reset_clip();const std::string fixed="C#4 0123456789 +12.5 dB / …";
    allocations=0;guardAllocations=true;
    for(int style=0;style<kDemoFontStyleCount;++style){a.set_font_style(style);a.draw_text(fixed,12,12,0xffffffff,1,2);a.draw_label(fixed,12,50,0xffffffff,18);}
    guardAllocations=false;assert(allocations==0);
}
void settings_checks(StdFileSystem& fs){
    auto saved_json=[&](){std::string blob;assert(fs.read_file(fs.settings_path(),blob));return songcore::json::parse(blob);};
    SettingsValues original;original.abxyIndex=2;original.portraitSkin="keep-owner-skin";original.overlayName="keep-overlay";
    original.overlayStrength=77;original.buttonSoundEnabled=false;original.buttonVibroEnabled=false;
    original.rememberFolder=false;original.lastSampleFolder="pt://owner/Samples/Jazz 音";original.lastSampleSelection=original.lastSampleFolder+"/Keep.wav";
    original.lastSampleCursor=22;original.lastSampleScroll=16;original.lastSampleSort=3;
    Theme theme=theme_builtins().at(8);theme.surfaceStyle=17;
    assert(save_settings(fs,original,theme));auto stable=saved_json();assert(!stable.contains("fontStyle"));
    for(int style=0;style<kDemoFontStyleCount;++style){
        original.fontStyle=style;assert(save_settings(fs,original,theme));SettingsValues loaded;Theme after;
        assert(load_settings(fs,loaded,after)&&loaded.fontStyle==style);
        assert(save_settings(fs,loaded,after));auto j=saved_json();j.erase("fontStyle");assert(j==stable);
        assert(serialize_theme(after)==serialize_theme(theme));
    }
    // A formerly selected style must reset to Classic for missing, wrongly typed and future IDs.
    const std::vector<std::string> invalid={"-1",std::to_string(kDemoFontStyleCount),"2147483648","9223372036854775807","18446744073709551615","true","null","3.0","\"6\"","{}","[]"};
    for(const auto& raw:invalid){
        auto j=stable;j["fontStyle"]=songcore::json::parse(raw);assert(fs.write_file(fs.settings_path(),j.dump()));
        SettingsValues loaded;loaded.fontStyle=6;Theme after;assert(load_settings(fs,loaded,after)&&loaded.fontStyle==0);
        assert(save_settings(fs,loaded,after));auto back=saved_json();back.erase("fontStyle");assert(back==stable);
    }
    assert(fs.write_file(fs.settings_path(),stable.dump()));SettingsValues legacy;legacy.fontStyle=6;Theme after;
    assert(load_settings(fs,legacy,after)&&legacy.fontStyle==0);
}
void studio_checks(StdFileSystem& fs){
    auto engine=std::make_unique<AudioEngine>();engine->setDeviceSampleRate(48000);
    songcore::SongcoreHost host(engine.get(),48000);assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
    AppState s;s.project=&host.edit_project();s.studioOpen=true;s.studioTab=6;s.studioRow=2;s.settings.abxyIndex=2;
    s.settings.lastSampleFolder="/keep/Samples";s.settings.lastSampleSelection="/keep/Samples/Keep.wav";s.settings.rememberFolder=false;
    s.studioModel=11;s.studioVariation=4;s.studioHarmonics=73;s.studioTimbre=159;s.studioMorph=111;
    engine->setMasterVolume(.63f);
    std::array<int,studio::COUNT> audio{};for(int i=0;i<studio::COUNT;++i)audio[i]=engine->studioRack.control[i].load();
    std::array<int,256> synth{};for(int i=0;i<256;++i)synth[i]=engine->studioCodes[i].load();
    const auto before=songcore::serialize_project(host.project());const int projectVersion=s.projectVersion;
    auto event=[&](Button b,ButtonAction a,ButtonMods mods=ButtonMods{}){assert(studio_button({b,a,mods},s,host,*engine,fs));};
    auto change=[&](Button key){event(Button::A,ButtonAction::PRESSED);ButtonMods a;a.a=true;event(key,ButtonAction::PRESSED,a);event(key,ButtonAction::RELEASED,a);event(Button::A,ButtonAction::RELEASED);};
    const int lastFont=kDemoFontStyleCount-1;
    assert(studio_rows(6)==3);for(int i=1;i<=kDemoFontStyleCount;++i){change(Button::DPAD_RIGHT);assert(s.settings.fontStyle==i%kDemoFontStyleCount);}
    change(Button::DPAD_LEFT);assert(s.settings.fontStyle==lastFont);change(Button::DPAD_UP);assert(s.settings.fontStyle==0);change(Button::DPAD_DOWN);assert(s.settings.fontStyle==lastFont);
    s.studioRow=0;const auto name=s.theme.name;change(Button::DPAD_RIGHT);assert(s.theme.name!=name&&s.settings.fontStyle==lastFont);
    s.studioRow=1;const int surface=s.theme.surfaceStyle;change(Button::DPAD_RIGHT);assert(s.theme.surfaceStyle!=surface&&s.settings.fontStyle==lastFont);
    s.studioRow=2;event(Button::DPAD_UP,ButtonAction::PRESSED);event(Button::DPAD_UP,ButtonAction::RELEASED);assert(s.studioRow==1&&s.settings.fontStyle==lastFont);
    assert(songcore::serialize_project(host.project())==before&&s.projectVersion==projectVersion);
    assert(s.studioModel==11&&s.studioVariation==4&&s.studioHarmonics==73&&s.studioTimbre==159&&s.studioMorph==111);
    assert(s.settings.abxyIndex==2&&!s.settings.rememberFolder&&s.settings.lastSampleFolder=="/keep/Samples"&&s.settings.lastSampleSelection=="/keep/Samples/Keep.wav");
    for(int i=0;i<studio::COUNT;++i)assert(audio[i]==engine->studioRack.control[i].load());
    for(int i=0;i<256;++i)assert(synth[i]==engine->studioCodes[i].load());
    Canvas c;
    for(int font:{0,6,kDemoFontStyleCount-1}){
        s.settings.fontStyle=font;draw_studio(c,s);assert(c.font_style()==font);
        for(int topic=0;topic<8;++topic)for(int page=0;page<3;++page){
            const auto content=studio_help_content(s,topic,page);
            const auto lines=studio_help_wrapped_lines(c,content);assert(!lines.empty()&&lines.size()<=10);
            for(const auto& line:lines)assert(Canvas::label_width(line,14)<=560);
            s.studioHelp=true;s.studioHelpTopic=topic;s.studioHelpPage=page;draw_studio(c,s);assert(c.font_style()==font);
        }
        s.studioHelp=false;
    }
    // A warmed real callback remains allocation-free after all appearance edits.
    std::array<float,512> block{};for(int i=0;i<3;++i)engine->processLiveBlock(block.data(),256,2,48000);
    allocations=0;guardAllocations=true;engine->processLiveBlock(block.data(),256,2,48000);guardAllocations=false;assert(allocations==0);
    assert(songcore::serialize_project(host.project())==before);
}
}
int main(int argc,char**argv){
    assert(argc==2);const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
    StdFileSystem fs((out/"data").string());canvas_checks();settings_checks(fs);studio_checks(fs);
    std::printf("PASS: original seven golden frames, %d independent metric-stable faces, clipping/data/Latin1 glyphs, per-Canvas isolation, %d settings roundtrips plus hostile IDs/types, real Style controls/palette/material preservation, unchanged song/synth/master/user settings, font0/6/%d help bounds, %u-byte static atlases and zero glyph/callback allocations.\n",kDemoFontStyleCount,kDemoFontStyleCount,kDemoFontStyleCount-1,detail::demoFontAtlasBytes);
}
