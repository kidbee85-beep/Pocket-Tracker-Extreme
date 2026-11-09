// Build on macOS: clang++ -std=c++17 -O2 generate_ui_font.cpp -framework CoreText -framework CoreGraphics -framework CoreFoundation -o generate-ui-font
// Usage: generate-ui-font LiberationMono-Bold.ttf > native/ui/font_hd.h
// Uses the existing Liberation OFL licence. Raster tables keep 10/15-pixel cells.
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

struct Raster { std::array<unsigned char,64*64> ink{}; int top=64,bottom=0,left=64,right=0; };
Raster raster(CTFontRef font,int cp) {
 Raster result; auto space=CGColorSpaceCreateDeviceGray();
 auto context=CGBitmapContextCreate(result.ink.data(),64,64,8,64,space,kCGImageAlphaNone);
 CGColorSpaceRelease(space);
 CGContextSetGrayFillColor(context,1,1);
 CGContextSetAllowsFontSmoothing(context,false); CGContextSetShouldSmoothFonts(context,false);
 CGContextSetShouldAntialias(context,true); CGContextSetShouldSubpixelPositionFonts(context,false);
 UniChar character=cp;CGGlyph glyph=0;CTFontGetGlyphsForCharacters(font,&character,&glyph,1);
 CGPoint pos={16,20};CTFontDrawGlyphs(font,&glyph,&pos,1,context);CGContextRelease(context);
 for(int y=0;y<64;++y)for(int x=0;x<64;++x)if(result.ink[y*64+x]) {
  result.left=std::min(result.left,x);result.right=std::max(result.right,x+1);
  result.top=std::min(result.top,y);result.bottom=std::max(result.bottom,y+1);
 }
 return result;
}
int main(int argc,char**argv) {
 if(argc!=2)return 1;
 std::ifstream f(argv[1],std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});
 auto provider=CGDataProviderCreateWithData(nullptr,bytes.data(),bytes.size(),nullptr);
 auto graphics=CGFontCreateWithDataProvider(provider);CGDataProviderRelease(provider);if(!graphics)return 2;
 puts("// Liberation Mono Bold (SIL OFL-1.1), CoreText grayscale raster, fitted without stretching.\n// Reproduce with work/design-004/generate_ui_font.cpp and a LiberationMono-Bold.ttf source.\n#pragma once\n#include <cstdint>");
 for(int cell:{10,15}) {
  const int height=cell==15?17:11;
  CTFontRef font=nullptr;std::vector<Raster> glyphs;int top=64,bottom=0;
  // Fit the complete printable ASCII ink, not an assumed baseline: preserve every descender.
  for(float size=cell*1.35f;size>5;size-=.05f) {
   if(font)CFRelease(font);font=CTFontCreateWithGraphicsFont(graphics,size,nullptr,nullptr);
   glyphs.clear();top=64;bottom=0;int widest=0;
   for(int cp=32;cp<127;++cp) {
    auto g=raster(font,cp);if(cp!=32){if((cp>='0'&&cp<='9')||(cp>='A'&&cp<='Z')||(cp>='a'&&cp<='z')){top=std::min(top,g.top);bottom=std::max(bottom,g.bottom);widest=std::max(widest,g.right-g.left);}}glyphs.push_back(g);
   }
   if(bottom-top<=height && widest<=cell)break;
  }
  fprintf(stderr,"cell%d chosenEm%.2f height%d top%d bottom%d\n",cell,CTFontGetSize(font),bottom-top,top,bottom);
  const int offset=(height-(bottom-top))/2;
  printf("inline constexpr int studioFont%dHeight=%d;\ninline constexpr uint8_t studioFont%d[95][%d]={\n",cell,height,cell,cell*height);
  for(int cp=32;cp<127;++cp) {
   auto g=glyphs[cp-32];
   // Tall punctuation is fitted separately instead of shrinking all letters.
   for(float size=CTFontGetSize(font)-.05f;(g.bottom-g.top>height||g.right-g.left>cell)&&size>5;size-=.05f){
    auto fallback=CTFontCreateWithGraphicsFont(graphics,size,nullptr,nullptr);g=raster(fallback,cp);CFRelease(fallback);
   }
   printf("{"); const int xoff=(cell-(g.right-g.left))/2;
   const int yoff=std::clamp(g.top-top+offset,0,std::max(0,height-(g.bottom-g.top)));
   for(int y=0;y<height;++y)for(int x=0;x<cell;++x) {
    const int sx=x-xoff+g.left,sy=y-yoff+g.top;
    unsigned v=sx>=g.left&&sx<g.right&&sy>=g.top&&sy<g.bottom?g.ink[sy*64+sx]:0;
    // Strong grayscale coverage, no LCD subpixel fringes on a rotated Android display.
    v=v<8?0:static_cast<unsigned>(std::lround(std::pow(v/255.,.82)*255.));
    printf("%u,",v);
   }
   puts("},");
  }
  puts("};");CFRelease(font);
 }
 CGFontRelease(graphics);
}
