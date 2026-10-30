/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003E8438 (provisional draft with VU0 asm): appends a GS packet of n vertices at obj's packet
 * cursor: a GIF tag, the colour rgba0 (rgba2 from the third vertex on, each with Q = 1), then per vertex its ST
 * words and its XYZF2 word. The position (three floats, a stride of 12) goes through the matrix in shared vectors
 * 24..27 (w = 1), is clamped lane by lane by shared vector 22 (MINI, before the division), divided by its w
 * (DIV Q, taken before the clamp), scaled by 20 and offset by 21 (func_0049E3E0 loads 20..23 from the
 * scratchpad), and converted to 12.4 fixed point: x and y the low 16 bits, z >> 4 in 24 bits, w >> 4 in the top
 * 8 (F). */
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
    u64 *tag;
    u32 *pkt;
    s32 i;

    func_0049E3E0();
    tag = obj->cursor;
    pkt = (u32 *)(tag + 2);
    tag[0] = ((u64)(n * 2 + 2) << 60) | 0x0400000000008001UL;
    tag[1] = 0x42424242421C2C21UL;
    pkt[0] = rgba0;
    pkt[1] = 0x3F800000;
    pkt += 2;
    for (i = 0; i < n; i++) {
        float p[3], v[4];
        u32 c[4];
        int k;

        p[0] = pos->x;
        p[1] = pos->y;
        p[2] = pos->z;
        gt4_rows4(v, gt4_vec(24), gt4_vec(25), gt4_vec(26), gt4_vec(27), p, 1.0f, 4);
        const float q = gt4_vu_div(1.0f, v[3]);
        if (i == 2) {
            pkt[0] = rgba2;
            pkt[1] = 0x3F800000;
            pkt += 2;
        }
        pkt[0] = st[0];
        pkt[1] = st[1];
        pkt += 2;
        const float *scale = gt4_vec(20), *offset = gt4_vec(21), *clamp = gt4_vec(22);
        for (k = 0; k < 4; k++) {
            const float t = gt4_minf(v[k], clamp[k]) * q;
            c[k] = (u32)gt4_ftoi(offset[k] + t * scale[k], 4);
        }
        *(u64 *)pkt = ((u64)(c[3] >> 4) << 56) | ((u64)((c[2] >> 4) & 0xFFFFFF) << 32) | (u64)(c[0] & 0xFFFF) |
                      ((u64)(c[1] & 0xFFFF) << 16);
        pkt += 2;
        pos++;
        st += 2;
    }
    obj->cursor = (u64 *)pkt;
}
