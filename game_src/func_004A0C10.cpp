/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 004A0C10 (matched): shows the frame buffer at graphics memory word `a`, in format `b`, `c`
 * pixels wide (packed as DISPFB: a >> 11 in 2048-word pages, c >> 6 in 64-pixel units). The original wrote the
 * library's display values to the GS's display registers (PMODE, SMODE2, DISPFB2, DISPLAY2, BGCOLOR, and
 * DISPFB1/DISPLAY1 shifted by the field offset in mode 1) with a `sync`; here they go to the runtime's display
 * (compat/game/gt4_gfx.h gt4_gfx_display), which the host shows from the next VBlank on. */
#include "spr.h"
#include "gt4_gfx.h"
typedef unsigned int u32;
typedef unsigned long u64;
struct Sub { char pad[0x58]; u64 f58,f60,f68,f70,f78; char pad80[8]; unsigned short f88,f8a; };
struct Spr { char pad[0x10]; unsigned char mode,x,y; char pad13[0x50-0x13]; Sub sub; };
extern "C" void func_004A0C10(u32 a,int b,int c) {
    Spr *s=(Spr *)spr_base();
    Sub *p=&s->sub;
    p->f68=(u64)(a>>11) | ((u64)((c>>6)&63)<<9) | ((u64)(b&15)<<15) | ((u64)(p->f8a&0x7ff)<<32) | ((u64)(p->f88&0x7ff)<<43);
    unsigned long long v[7];
    v[0]=p->f58;   /* PMODE */
    v[1]=p->f60;   /* SMODE2 */
    v[4]=p->f68;   /* DISPFB2 */
    v[5]=p->f70;   /* DISPLAY2 */
    v[6]=p->f78;   /* BGCOLOR */
    unsigned int which=0x73;
    if(s->mode==1) {
        v[2]=p->f68;
        v[3]=p->f70+((u64)s->y<<12)+s->x;
        which|=0x0C;
    }
    gt4_gfx_display(which,v);
}
