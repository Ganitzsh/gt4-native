/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003E8438 (provisional draft with VU0 asm): appends a GS packet of n vertices at obj's packet
 * cursor: a GIF tag, the colour rgba0 (rgba2 from the third vertex on, each with Q = 1), then per vertex its ST
 * words and its XYZF2 word. The position (three floats, a stride of 12) goes through the matrix in shared vectors
 * 24..27 (w = 1), is clamped lane by lane by shared vector 22 (MINI, before the division), divided by its w
 * (DIV Q, taken before the clamp), scaled by 20 and offset by 21 (func_0049E3E0 loads 20..23 from the
 * scratchpad), and converted to 12.4 fixed point: x and y the low 16 bits, z >> 4 in 24 bits, w >> 4 in the top
 * 8 (F). Natively the vertices are a draw record (compat/game/gt4_gfx.h) that continues the primitive
 * func_003E7F08 set; the first two only start the strip. */
#include "gt4_gfx.h"
#include "gt4_vec.h"
typedef int s32;
typedef unsigned int u32;
typedef unsigned long u64;
typedef float f32;

struct Vec3 {
    f32 x, y, z;
};

struct Packet {
    char pad[0x58];
    u64 *cursor;
};

extern "C" void func_0049E3E0(void);

extern "C" void func_003E8438(Packet *obj, Vec3 *pos, u32 *st, u32 rgba0, u32 rgba2, s32 n)
{
    s32 i;
    struct gt4_vertex *out = (struct gt4_vertex *)__builtin_alloca((n > 0 ? n : 1) * sizeof(struct gt4_vertex));

    (void)obj;
    func_0049E3E0();
    for (i = 0; i < n; i++) {
        float p[3], v[4];
        u32 c[4];
        int k;

        p[0] = pos->x;
        p[1] = pos->y;
        p[2] = pos->z;
        gt4_rows4(v, gt4_vec(24), gt4_vec(25), gt4_vec(26), gt4_vec(27), p, 1.0f, 4);
        const float q = gt4_vu_div(1.0f, v[3]);
        struct gt4_vertex *o = &out[i];
        o->rgba = i < 2 ? rgba0 : rgba2;
        o->q = 1.0f;
        o->s = __gt4_bits_float(st[0]);
        o->t = __gt4_bits_float(st[1]);
        const float *scale = gt4_vec(20), *offset = gt4_vec(21), *clamp = gt4_vec(22);
        for (k = 0; k < 4; k++) {
            const float t = gt4_minf(v[k], clamp[k]) * q;
            c[k] = (u32)gt4_ftoi(offset[k] + t * scale[k], 4);
        }
        o->x = (unsigned short)c[0];
        o->y = (unsigned short)c[1];
        o->z = (c[2] >> 4) & 0xFFFFFF;
        o->fog = (unsigned char)(c[3] >> 4);
        o->u = o->v = 0;
        o->kick = i >= 2;
        o->pad = 0;
        pos++;
        st += 2;
    }
    gt4_gfx_draw((void *)0x70002000, 0, GT4_DRAW_FOG | GT4_DRAW_COLOR | GT4_DRAW_ST, out, n);
}
