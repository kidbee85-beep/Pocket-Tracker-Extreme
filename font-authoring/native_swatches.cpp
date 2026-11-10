// SPDX-License-Identifier: GPL-3.0-or-later
// Native API specimens, deliberately not a product/menu screenshot.
#include "demo_fonts.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace pt::ui::detail;
static std::array<uint32_t,640*480> canvas;
static uint32_t bg=0x0b111c,white=0xe5f5ef,mint=0x9ce9ca,muted=0x8c9faf,line=0x273747;
void rect(int x,int y,int w,int h,uint32_t color){for(int yy=std::max(0,y);yy<std::min(480,y+h);++yy)for(int xx=std::max(0,x);xx<std::min(640,x+w);++xx)canvas[yy*640+xx]=color;}
void pixels(const uint8_t* p,int w,int h,int x,int y,uint32_t color){assert(p);for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){if(x+xx<0||y+yy<0||x+xx>=640||y+yy>=480)continue;unsigned a=p[yy*w+xx];if(!a)continue;auto& target=canvas[(y+yy)*640+x+xx];uint32_t out=0;for(int shift:{0,8,16})out|=(((color>>shift&255)*a+(target>>shift&255)*(255-a)+127)/255)<<shift;target=out;}}
uint32_t next(const std::string&s,size_t&i){unsigned char c=s[i++];if(c<128)return c;if((c&224)==192){uint32_t cp=c&31;return (cp<<6)|(uint8_t(s[i++])&63);}if((c&240)==224){uint32_t cp=c&15;cp=(cp<<6)|(uint8_t(s[i++])&63);return (cp<<6)|(uint8_t(s[i++])&63);}return 32;}
int index(uint32_t cp){return cp>=32&&cp<127?int(cp-32):cp>=160&&cp<256?95+int(cp-160):cp==0x2026?191:-1;}
void label(int style,int size,const std::string&s,int x,int y,uint32_t color){const auto*g=size>=15?uiFont18Glyphs:uiFont12Glyphs;int h=size>=15?21:14;const auto*p=demo_label_pixels(style,size);for(size_t i=0;i<s.size();){int j=index(next(s,i));if(j<0)continue;pixels(p+g[j].offset,g[j].width,h,x+g[j].left,y,color);x+=g[j].advance;}}
void grid(int style,int scale,const std::string&s,int x,int y,uint32_t color){for(unsigned char c:s){const auto*p=demo_grid_pixels(style,scale,c);if(p)pixels(p,scale*5,scale==3?17:11,x,y,color);x+=scale*5+2;}}
std::vector<uint8_t> normalized_label(int style,int size,uint32_t cp){const auto*g=size>=15?uiFont18Glyphs:uiFont12Glyphs;const auto&p=g[index(cp)];const auto*a=demo_label_pixels(style,size);int h=size>=15?21:14;std::vector<uint8_t> out(24*h);for(int y=0;y<h;++y)for(int x=0;x<p.width;++x)out[y*24+x+p.left]=a[p.offset+y*p.width+x];return out;}
void checks(){
 for(int style:{-100,-1,0,17,100}){assert(!demo_label_pixels(style,12));assert(!demo_grid_pixels(style,2,'A'));}
 int checks=0;
 for(int style=1;style<=16;++style){
  for(int scale:{-4,0,1,4,99})assert(!demo_grid_pixels(style,scale,'A'));
  for(uint32_t cp:{0u,31u,127u,255u,0x10ffffu})assert(!demo_grid_pixels(style,2,cp));
  for(int scale:{2,3})for(uint32_t cp=33;cp<=126;++cp){auto*p=demo_grid_pixels(style,scale,cp);assert(p);assert(std::any_of(p,p+(scale==2?110:255),[](uint8_t a){return a>0;}));++checks;}
  for(int size:{12,18})for(uint32_t cp:{uint32_t('A'),uint32_t('0'),0xe4u,0xf6u,0xfcu,0xe9u,0x2026u}){auto p=normalized_label(style,size,cp);assert(std::any_of(p.begin(),p.end(),[](uint8_t a){return a>0;}));++checks;}
  for(auto pair:std::array<std::array<char,2>,8>{{{{'0','O'}},{{'1','I'}},{{'1','l'}},{{'I','l'}},{{'B','8'}},{{'F','E'}},{{'F','#'}},{{'E','#'}}}}){
   for(int scale:{2,3}){assert(std::memcmp(demo_grid_pixels(style,scale,pair[0]),demo_grid_pixels(style,scale,pair[1]),scale==2?110:255));++checks;}
   for(int size:{12,18}){assert(normalized_label(style,size,pair[0])!=normalized_label(style,size,pair[1]));++checks;}
  }
 }
 std::printf("{\"native_API_checks\":%d,\"passed\":true,\"atlas_bytes\":%u,\"label_metrics\":\"existing 192 glyph offsets, widths, bearings and advances\"}\n",checks,demoFontAtlasBytes);
}
int main(int argc,char**argv){
 if(argc!=2)return 2;checks();const char*names[]={"TERMINAL89","RASTER91","ACIDVECTOR","CRACKTRO","BITROT","NULLSIGNAL","CLEAR MONO","STUDIO SANS","COMPACT GRID","SOFT PIXEL","ISO TERMINAL","WIDE SCREEN","PAPER TYPE","LCD CLEAN","RAIL MONO","NIGHT EDITOR"};
 const char*details[]={"Gated square bowls / mechanical terminals","Broad rounded shoulders / dense raster weight","Diamond bowls / triangular lowercase / long diagonals","Swept spurs / compressed counters / offset crossbars","Stair-step anatomy / deliberate stencil gates","Split-rail bowls / modular lowercase / instrument digits","Open counters / slashed zero / comfortable lowercase","Soft shoulders / double-storey lowercase / large counters","Compact squares / tall lowercase / open four","Rounded pixel corners / continuous stems","Technical forms / open bowls / clear lowercase","Wide open forms / calm proportions / readable digits","Crisp serifs / editorial lowercase / quiet texture","Unbroken digital / open counters / strong stems","Straight rails / technical terminals / explicit figures","Calm medium weight / comfortable night editing"};
 for(int style=1;style<=16;++style){
  canvas.fill(bg);for(int y=0;y<480;y+=8)for(int x=0;x<640;x+=8)if(((x/8+y/8)&1)==0)rect(x,y,1,1,0x17202c);
  rect(0,0,640,5,mint);label(style,12,"TRACKWERK / KID BUNKA / TYPE LAB",24,19,mint);
  grid(style,3,names[style-1],24,47,white);label(style,12,details[style-1],24,72,muted);
  rect(24,100,592,69,0x131e29);label(style,12,"PATTERN DATA / 15 x 17 PIXELS",36,109,muted);
  grid(style,3,"A1 C#3 07  128.00 BPM",36,132,white);
  label(style,12,"GRID / 10 x 11 PIXELS",24,188,mint);
  grid(style,2,"ABCDEFGHIJKLMNOPQRSTUVWXYZ",24,210,white);
  grid(style,2,"abcdefghijklmnopqrstuvwxyz",24,229,white);
  grid(style,2,"0123456789 !#$%&()*+,-./:;=?",24,248,white);
  rect(24,276,592,1,line);label(style,12,"PROPORTIONAL LABELS / ORIGINAL METRICS",24,291,mint);
  label(style,18,"TrackWerk  /  Synthesis & sample slicing",24,315,white);
  label(style,18,u8"ÄÖÜ äöü é è ñ ç ß  …",24,343,white);
  label(style,12,"IN 00:04.000   OUT 00:18.000   CUTOFF 07F",24,375,white);
  rect(24,401,592,1,line);label(style,12,"READABILITY",24,413,muted);
  grid(style,2,"0O 1Il B8 FE #",154,415,white);label(style,12,"0 O / 1 I l / B 8 / F E #",354,413,white);
  label(style,12,"STATIC GLYPHS / CLASSIC FONT REMAINS AVAILABLE",24,450,muted);
  std::string path=std::string(argv[1])+"/"+std::to_string(style)+"-"+names[style-1]+"-native.ppm";std::ofstream f(path,std::ios::binary);f<<"P6\n640 480\n255\n";for(auto v:canvas){f.put(char(v>>16));f.put(char(v>>8));f.put(char(v));}
 }
}
