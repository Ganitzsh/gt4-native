/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A6AF8 (provisional draft build/swarm/o2/004a6af8_best.cpp, not matched): a 2D line from
 * (x0, y0) to (x1, y1) (pixels, through the 2D offset; Y in half pixels in field mode) in the fill colour (+0xC20,
 * Q = 1) at the 2D Z and fog (+0x248), with the library's attributes (+0x4A) without texturing. The original wrote
 * a DIRECT packet (REGLIST RGBAQ, PRIM, XYZF3, XYZF2); here the same values are a draw record
 * (compat/game/gt4_gfx.h). */
#include "spr.h"
#include "gt4_gfx.h"
typedef unsigned int u32;
struct Spr {
    char p0[0x4A]; unsigned short flags4A; char p4C[0x1F8 - 0x4C];
    char *cur; char p1FC[0x248 - 0x1FC]; u32 u248; char p24C[0x26C - 0x24C];
    unsigned short x26C; unsigned short y26E; char p270[0x288 - 0x270];
    int offX; int offY; char p290[0xC20 - 0x290]; u32 uC20;
};
extern "C" void func_0049FE28() throw();
extern "C" void func_004A6AF8(int x0, int y0, int x1, int y1) {
    Spr *s = (Spr *)spr_base();
    int shift = (((unsigned char *)s)[7] != 0) ? 3 : 4;
    func_0049FE28();
    int xb = s->x26C + s->offX * 16;
    int yb = s->y26E + s->offY * 16;
    x0 = xb + x0 * 16;
    y0 = yb + (y0 << shift);
    x1 = xb + x1 * 16;
    y1 = yb + (y1 << shift);
    const u32 fl = (s->flags4A | 1) & 0xffef;
    const u32 ends[2] = {(u32)(y0 << 16) | (x0 & 0xffff), (u32)(y1 << 16) | (x1 & 0xffff)};
    struct gt4_vertex v[2];
    for (int k = 0; k < 2; k++) {
        v[k].x = (unsigned short)ends[k];
        v[k].y = (unsigned short)(ends[k] >> 16);
        gt4_vertex_zf(&v[k], s->u248);
        v[k].rgba = s->uC20;
        v[k].s = v[k].t = 0.0f;
        v[k].q = 1.0f;
        v[k].u = v[k].v = 0;
        v[k].kick = k;
        v[k].pad = 0;
    }
    gt4_gfx_draw(s, fl, GT4_DRAW_PRIM | GT4_DRAW_FOG | GT4_DRAW_COLOR, v, 2);
}
