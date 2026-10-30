/* compiler: ee-gcc2.96-nogcse */
/* Native version of 004A6788 (provisional draft build/swarm/c8/004a6788_best.cpp, not matched): a textured
 * sprite from (x0, y0) to (x1, y1) (pixels, through the 2D offset at +0x26C/+0x288; Y in half pixels in field
 * mode, +7) sampling texels (u0, v0)..(u1, v1), drawn in strips 32 pixels wide (the GS's texture cache): the
 * library's attributes (+0x4A) with TME and FST, the sprite colour (+0xC24) with Q = 1, the 2D Z and fog
 * (+0x248) on both corners. The original wrote a DIRECT packet:
 * RGBAQ and PRIM, then per strip UV, XYZF3, UV, XYZF2; here the same values are a draw record
 * (compat/game/gt4_gfx.h). */
#include "gt4_gfx.h"
typedef float f32; typedef int s32; typedef unsigned short u16; typedef unsigned char u8;
#define M2C_FIELD(p,t,o) (*(t)((char *)(p)+(o)))
extern "C" void func_0049FE28() throw();

static inline void corner(struct gt4_vertex *o, s32 x, s32 y, s32 u, s32 v, unsigned int zf, unsigned int rgba, int kick)
{
    o->x = (unsigned short)x;
    o->y = (unsigned short)y;
    gt4_vertex_zf(o, zf);
    o->rgba = rgba;
    o->s = o->t = 0.0f;
    o->q = 1.0f;
    o->u = (unsigned short)u;
    o->v = (unsigned short)v;
    o->kick = kick;
    o->pad = 0;
}

extern "C" void func_004A6788(f32 fparg0, f32 fparg1, f32 fparg2, f32 fparg3, f32 fparg4, f32 fparg5, f32 fparg6, f32 fparg7) {
    f32 temp_f1;
    f32 temp_f2;
    f32 var_f1;
    s32 temp_v0_3;
    s32 temp_v1;
    s32 temp_a4;
    s32 temp_a5;
    s32 temp_f0;
    s32 temp_f2_2;
    s32 temp_f2_3;
    s32 temp_f6;
    s32 temp_s4;
    s32 temp_t0;
    s32 temp_v0;
    s32 temp_v0_2;
    s32 var_a5;
    s32 var_a6;
    s32 var_a7;
    s32 var_s1;
    s32 var_s3;
    s32 var_t1;
    s32 var_t2;
    s32 var_v0;

    u16 flags;
    char *base = (char *)0x70002000;
    func_0049FE28();
    flags = M2C_FIELD(base, u16 *, 0x4A) | 0x116;
    var_f1 = 0x1.0000000000000p+3f;
    if (M2C_FIELD(base, u8 *, 7) == 0) {
        var_f1 = 0x1.0000000000000p+4f;
    }
    temp_f2 = (f32) M2C_FIELD(base, u16 *, 0x26C) + (f32) (M2C_FIELD(base, s32 *, 0x288) * 0x10);
    temp_f1 = (f32) M2C_FIELD(base, u16 *, 0x26E) + (f32) (M2C_FIELD(base, s32 *, 0x28C) * 0x10);
    fparg0 = temp_f2 + fparg0 * 16.0f;
    fparg2 = temp_f2 + fparg2 * 16.0f;
    fparg1 = temp_f1 + fparg1 * var_f1;
    fparg3 = temp_f1 + fparg3 * var_f1;
    fparg4 *= 16.0f;
    fparg5 *= 16.0f;
    fparg6 *= 16.0f;
    fparg7 *= 16.0f;
    var_t1 = (s32)(fparg0 + 0.5f);
    var_t2 = (s32)(fparg2 + 0.5f);
    temp_f6 = (s32)(fparg1 + 0.5f);
    temp_f2_2 = (s32)(fparg5 + 0.5f);
    temp_f2_3 = (s32)(fparg3 + 0.5f);
    var_s3 = (s32)(fparg6 + 0.5f);
    var_s1 = (s32)(fparg4 + 0.5f);
    temp_f0 = (s32)(fparg7 + 0.5f);
    var_v0 = var_t2 - 1;
    if (var_t2 < var_t1) {
        temp_v0 = var_t1;
        var_t1 = var_t2;
        var_t2 = temp_v0;
        temp_v0_2 = var_s1;
        var_s1 = var_s3;
        var_s3 = temp_v0_2;
        var_v0 = var_t2 - 1;
    }
    var_a7 = var_t1;
    var_a6 = var_s1;
    temp_t0 = var_t2 - var_a7;
    temp_a5 = (var_v0 >> 9) - (var_t1 >> 9);
    temp_s4 = var_s3 - var_a6;
    if (temp_t0 != 0) {
        /* the original wrote no DMA header when the strip count made its size 0 (never in practice) */
        const unsigned int rgba = (unsigned int)M2C_FIELD(base, s32 *, 0xC24);
        temp_a4 = M2C_FIELD(base, s32 *, 0x248);
        struct gt4_vertex *out = (struct gt4_vertex *)__builtin_alloca((temp_a5 + 1) * 2 * sizeof(struct gt4_vertex));
        struct gt4_vertex *v = out;
        var_a5 = temp_a5 - 1;
        if (var_a5 != -1) {
            do {
                temp_v1 = (var_a7 + 0x200) & ~0x1F0;
                temp_v0_3 = var_s1 + ((s32) (temp_s4 * (temp_v1 - var_t1)) / temp_t0);
                corner(v++, var_a7, temp_f6, var_a6, temp_f2_2, (unsigned int)temp_a4, rgba, 0);
                corner(v++, temp_v1, temp_f2_3, temp_v0_3, temp_f0, (unsigned int)temp_a4, rgba, 1);
                var_a7 = (s32) temp_v1;
                var_a5 -= 1;
                var_a6 = (s32) temp_v0_3;
            } while (var_a5 != -1);
        }
        corner(v++, var_a7, temp_f6, var_a6, temp_f2_2, (unsigned int)temp_a4, rgba, 0);
        corner(v++, var_t2, temp_f2_3, var_s3, temp_f0, (unsigned int)temp_a4, rgba, 1);
        gt4_gfx_draw(base, flags, GT4_DRAW_PRIM | GT4_DRAW_FOG | GT4_DRAW_COLOR | GT4_DRAW_UV, out, (temp_a5 + 1) * 2);
    }
}
