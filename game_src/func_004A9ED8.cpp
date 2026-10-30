/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A9ED8 (provisional draft drafts/004a9ed8, not matched): the screen grid passes' drawing
 * state: both contexts' scissor (context 2's `scissor`), context 1's frame buffer `fbp` (24-bit, keeping the top
 * byte), origins at 2048 (context 2's 2048 x 1.0 in Y), the blend 0x68 with fixed alpha 0x80, attributes from
 * PRIM, context 2's clamp, fixed LOD, tests 0x31002, Z buffers 0x101000000 (Z writes off), no per-pixel blend or
 * alpha forcing, and the vertex colour 0x80000000. The original wrote them as an A+D packet behind MSKPATH3 and
 * FLUSHA; here they are state records and the colour an attribute-only draw record (compat/game/gt4_gfx.h). */
#include "spr.h"
#include "gt4_gfx.h"
typedef unsigned int u32;
typedef unsigned long long u64;
extern "C" void func_004A9ED8(int fbp, int unused, u64 scissor)
{
    void *s = spr_base();
    unsigned long long state[34];
    int n = 0;
#define PAIR(v, r) do { state[2 * n] = (v); state[2 * n + 1] = (r); n++; } while (0)
    PAIR(0x0FFF00000FFF0000ULL, 0x40);
    PAIR(scissor, 0x41);
    PAIR((u64)(fbp & 0x1FF) | 0x00FFFFFF00010000ULL, 0x4C);
    PAIR(0x0000800000008000ULL, 0x18);
    PAIR(0x00003F8000000000ULL, 0x19);
    PAIR(0x0000008000000068ULL, 0x42);
    PAIR(1, 0x1A);
    PAIR(0, 0x09);
    PAIR(1, 0x14);
    PAIR(1, 0x15);
    PAIR(0x31002, 0x47);
    PAIR(0x31002, 0x48);
    PAIR(0x101000000ULL, 0x4E);
    PAIR(0x101000000ULL, 0x4F);
    PAIR(0, 0x49);
    PAIR(0, 0x4A);
    PAIR(0, 0x4B);
#undef PAIR
    gt4_gfx_set(s, state, n);
    struct gt4_vertex colour;
    colour.x = colour.y = 0;
    colour.z = 0;
    colour.rgba = 0x80000000;
    colour.s = colour.t = 0.0f;
    colour.q = 0.0f;
    colour.u = colour.v = 0;
    colour.fog = 0;
    colour.kick = 0;
    colour.pad = 0;
    gt4_gfx_draw(s, 0, GT4_DRAW_COLOR | GT4_DRAW_ATTRIBUTES_ONLY, &colour, 1);
}
