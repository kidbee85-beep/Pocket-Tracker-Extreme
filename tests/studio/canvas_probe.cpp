#include "ui/canvas.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace pt::ui;
int main(){Canvas c;c.clear(0xFFFFFFFF);c.panel_rect(20,20,20,20,0x88000000,0x88000000);assert(c.pixels()[30*640+30]==0xFF777777);assert(c.pixels()[20*640+20]==0xFFFFFFFF);c.surface_rect(0,0,640,480,0xFF000000);for(int i=0;i<640*480;++i)assert(c.pixels()[i]==0xFF000000);c.clear(0xFF000000);c.set_clip(40,50,30,20);c.surface_rect(0,0,640,480,0xFF101521);for(int y=0;y<480;++y)for(int x=0;x<640;++x)if(x<40||x>=70||y<50||y>=70)assert(c.pixels()[y*640+x]==0xFF000000);c.reset_clip();c.surface_rect(0,0,640,480,0xFF101521);std::vector<uint32_t> snapshot(c.pixels(),c.pixels()+640*480);c.clear(0xFF00FF00);c.surface_rect(0,0,640,480,0xFF101521);assert(std::equal(snapshot.begin(),snapshot.end(),c.pixels()));for(int i=0;i<1000;++i){int x=(i*73%720)-40,y=(i*13%560)-40;Canvas::ClipScope clip(c,(i*41%720)-40,(i*31%560)-40,80,50);c.draw_text("0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ gxqjy",x,y,0xAAF0F8FF,2,2+i%2);c.panel_rect(x,y,1+i%70,1+i%33,0x88000000,0x99102030,i%9);}puts("PASS: alpha panels composite once; OLED black exact; surface clip/cache stable; 1000 clipped text/panel draws under ASAN/UBSAN");}
