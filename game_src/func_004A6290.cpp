/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A6290 (provisional draft drafts/004a6290, not matched): an untextured sprite in the
 * fill colour (+0xC20, Q = 1) from (x0, y0) to (x1, y1) (pixels, through the 2D offset; Y in half pixels in
 * field mode) in strips 64 pixels wide, at the 2D Z and fog (+0x248), with the library's attributes (+0x4A)
 * without texturing. The original wrote a DIRECT packet (A+D: RGBAQ, PRIM, XYZF3/XYZF2 pairs); here the same
 * values are a draw record (compat/game/gt4_gfx.h). */
#include "spr.h"
#include "gt4_gfx.h"
typedef unsigned int u32;
struct Spr {
    char p0[7]; unsigned char flag7; char p8[0x4A - 8]; unsigned short flags4A; char p4C[0x1F8 - 0x4C];
    u32 cur; char p1FC[0x248 - 0x1FC]; u32 u248; char p24C[0x26C - 0x24C];
    unsigned short x26C; unsigned short y26E; char p270[0x288 - 0x270];
    int offX; int offY; char p290[0xC20 - 0x290]; u32 uC20;
};
extern "C" void func_0049FE28() throw();

static inline void corner(struct gt4_vertex *o, u32 xy, u32 zf, u32 color, int kick)
{
    o->x = (unsigned short)xy;
    o->y = (unsigned short)(xy >> 16);
    gt4_vertex_zf(o, zf);
    o->rgba = color;
    o->s = o->t = 0.0f;
    o->q = 1.0f;
    o->u = o->v = 0;
    o->kick = kick;
    o->pad = 0;
}

extern "C" void func_004A6290(int x0, int y0, int x1, int y1) {
    func_0049FE28();
    if (x1 < x0) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    int n = ((x1 - 1) >> 6) - (x0 >> 6);
    if (n < 0) return;   /* an empty rectangle (the original's strip loop would not end) */
    Spr *s = (Spr *)spr_base();
    x1 *= 16;
    x0 *= 16;
    int shift = s->flag7 != 0 ? 3 : 4;
    y0 <<= shift;
    y1 <<= shift;
    int by = s->y26E + (s->offY << 4);
    int bx = s->x26C + (s->offX << 4);
    u32 a = ((by + y0) << 16) | bx;
    u32 b = ((by + y1) << 16) | bx;
    u32 z = s->u248;
    u32 color = s->uC20;
    struct gt4_vertex *out = (struct gt4_vertex *)__builtin_alloca((n + 1) * 2 * sizeof(struct gt4_vertex));
    struct gt4_vertex *v = out;
    int strips = n + 1;
    while (n--) {
        int nx = (x0 + 0x400) & ~0x3f0;
        corner(v++, a + x0, z, color, 0);
        corner(v++, b + nx, z, color, 1);
        x0 = nx;
    }
    corner(v++, a + x0, z, color, 0);
    corner(v++, b + x1, z, color, 1);
    gt4_gfx_draw(s, (s->flags4A | 6) & 0xffef, GT4_DRAW_PRIM | GT4_DRAW_FOG | GT4_DRAW_COLOR, out, strips * 2);
}
