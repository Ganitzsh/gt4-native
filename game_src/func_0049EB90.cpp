/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049EB90 (recovered matched source with VU0 asm): refreshes the light state the flags (+0x48)
 * mark (bit 0 0049ECF8, bit 1 0049EE00, bit 2 0049F038), then pushes six quadwords below the packet stack top
 * (+0x1FC): the three rows at +0x2E0, the light vector at +0xAC0 (times 1 / scale when scale is not 0), and the two
 * rows at +0x3A0 multiplied lane by lane by (k, k, k, alpha), k and alpha the words at +0xB10 and +0xB14 (floats).
 * Then the VU1 size (+0x18: 0x195 below distance 12 (+0xAA0), else 0x1D4) and a DMA ref tag to the six quadwords
 * with its VIF code (+0x30). The original builds (k, k, k, alpha) in VU0 vf10 and 1 / scale in vf11.x (its y the
 * word's sign extension, z and w a GPR's stale upper half), shared vectors (8..19): x, y, z, w of 10 and x, y of 11
 * are kept so. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned short u16;
typedef unsigned int u32;
typedef float f32;

struct State {
    char pad0[0x18];
    u32 qwc;
    char pad1C[0x30 - 0x1C];
    u32 tag[4];
    char pad40[0x48 - 0x40];
    u16 flags;
    char pad4A[0x1FC - 0x4A];
    f32 (*top)[4];
    char pad200[0x2E0 - 0x200];
    f32 a[3][4];
    char pad310[0x3A0 - 0x310];
    f32 e[2][4];
    char pad3C0[0xAA0 - 0x3C0];
    f32 dist;
    char padAA4[0xAC0 - 0xAA4];
    f32 light[4];
    char padAD0[0xB10 - 0xAD0];
    u32 scale;
    u32 alpha;
};

extern "C" void func_0049ECF8(State *s);
extern "C" void func_0049EE00(State *s);
extern "C" void func_0049F038(State *s);

extern "C" void func_0049EB90(State *s, f32 scale) {
    if ((u16)(s->flags & 1))
        func_0049ECF8(s);
    if (s->flags & 2)
        func_0049EE00(s);
    if (s->flags & 4)
        func_0049F038(s);
    float *v10 = gt4_vec(10);
    v10[0] = v10[1] = v10[2] = __gt4_bits_float(s->scale);
    v10[3] = __gt4_bits_float(s->alpha);
    f32 light[4];
    memcpy(light, s->light, 16);
    if (scale != 0.0f) {
        const f32 inv = 1.0f / scale;
        float *v11 = gt4_vec(11);
        v11[0] = inv;
        v11[1] = __gt4_bits_float((__gt4_float_bits(inv) & 0x80000000u) ? 0xFFFFFFFFu : 0);
        for (int k = 0; k < 4; ++k) light[k] = light[k] * inv;
    }
    f32 (*p)[4] = s->top - 6;
    s->top = p;
    memcpy(p[0], s->a, 48);
    memcpy(p[3], light, 16);
    for (int r = 0; r < 2; ++r)
        for (int k = 0; k < 4; ++k) p[4 + r][k] = s->e[r][k] * v10[k];
    if (s->dist < 6.0f)
        s->qwc = 0x195;
    else if (s->dist < 12.0f)
        s->qwc = 0x195;
    else
        s->qwc = 0x1D4;
    s->tag[0] = 0x30000006;
    s->tag[3] = 0x6C06C0C9;
    s->tag[1] = (u32)p & 0x0FFFFFFF;
    s->tag[2] = 0;
}
