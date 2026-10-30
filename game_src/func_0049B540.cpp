/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049B540 (provisional draft drafts/0049b540, not matched): a 2D triangle fan of n points
 * (xy: pixel pairs through the 2D offset, Y in half pixels in field mode), with texel coordinates (uv, x 16)
 * and per-point colours when given, else the sprite colour (+0xC24, textured) or the fill colour (+0xC20); the
 * library's attributes (+0x4A) with a Gouraud fan (and TME, FST with uv), the 2D Z and fog (+0x248). The original
 * wrote a DIRECT packet (A+D colour and PRIM, REGLIST XYZF3 for the first two points, XYZF2 after); here the same
 * values are a draw record (compat/game/gt4_gfx.h). */
#include "spr.h"
#include "gt4_gfx.h"
typedef unsigned int u32;
struct Spr {
    char p0[7]; unsigned char b7; char p8[0x4A - 8]; unsigned short flags4A; char p4C[0x1F8 - 0x4C];
    char *cur; char p1FC[0x248 - 0x1FC]; u32 u248; char p24C[0x26C - 0x24C];
    unsigned short x26C; unsigned short y26E; char p270[0x288 - 0x270];
    int offX; int offY; char p290[0xC20 - 0x290]; u32 uC20; u32 uC24;
};
extern "C" void func_0049FE28() throw();
extern "C" void func_0049B540(int n, float *xy, float *uv, u32 *colors) {
    Spr *s = (Spr *)spr_base();
    float sy;
    if (s->b7)
        sy = 8.0f;
    else
        sy = 16.0f;
    func_0049FE28();
    u32 prim = (unsigned short)(s->flags4A | 0xD);
    if (uv)
        prim |= 0x110;
    float ox = (float)s->offX * 16.0f;
    float oy = (float)s->offY * 16.0f;
    u32 x0 = s->x26C;
    u32 y0 = s->y26E;
    u32 z = s->u248;
    const u32 fixed = uv ? s->uC24 : s->uC20;
    struct gt4_vertex *out = (struct gt4_vertex *)__builtin_alloca((n > 0 ? n : 1) * sizeof(struct gt4_vertex));
    for (int i = 0; i < n; i++) {
        struct gt4_vertex *o = &out[i];
        o->u = o->v = 0;
        if (uv) {
            int v = (int)(uv[1] * 16.0f);
            int u = (int)(uv[0] * 16.0f);
            uv += 2;
            o->u = (unsigned short)u;
            o->v = (unsigned short)v;
        }
        o->rgba = colors ? *colors++ : fixed;
        o->s = o->t = 0.0f;
        o->q = 1.0f;
        float x = xy[0];
        float y = xy[1];
        xy += 2;
        int ix = (int)(ox + x * 16.0f);
        int iy = (int)(oy + y * sy);
        const u32 w = ((x0 + ix) & 0xFFFF) | ((y0 + iy) << 16);
        o->x = (unsigned short)w;
        o->y = (unsigned short)(w >> 16);
        gt4_vertex_zf(o, z);
        o->kick = i >= 2;
        o->pad = 0;
    }
    gt4_gfx_draw(s, prim, GT4_DRAW_PRIM | GT4_DRAW_FOG | GT4_DRAW_COLOR | (uv ? GT4_DRAW_UV : 0), out, n);
}
