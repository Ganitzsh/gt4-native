/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049E780 (VU0 asm): pushes sixteen quadwords on the packet stack (s->top, +0x1FC, moves down
 * 0x100): the eight light quadwords at +0x2E0 (the last five times (scale, scale, scale, alpha), +0xB10/+0xB14),
 * three light colours (+0x360..0x380, or with flag 0x80 of +0x48 the x, y, z of +0xAD0 each in all four lanes),
 * the four quadwords at +0x3A0 times the same scale, and the colour (255, 255, 255, alpha); then the DMA REF tag
 * (+0x30) and VIF UNPACK (+0x3C) point at them, and the VU1 address word (+0x18) is 0xB6, 0xF1 or 0x12C as the
 * distance (+0xAA0) is under 6, under 12 or more. Leaves shared vectors 8..18 as the original does (16 the colour,
 * 17 the scale, 18 the +0xAD0 vector only with flag 0x80). */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned short u16;
typedef unsigned int u32;

struct State {
    char pad0[0x18];
    u32 qwc;
    char pad1C[0x30 - 0x1C];
    u32 tag[4];
    char pad40[0x48 - 0x40];
    u16 flags;
    char pad4A[0x1FC - 0x4A];
    float *top;
    char pad200[0x2E0 - 0x200];
    float a[8][4];
    float b[3][4];
    char pad390[0x3A0 - 0x390];
    float c[4][4];
    char pad3E0[0xAA0 - 0x3E0];
    float dist;
    char padAA4[0xAD0 - 0xAA4];
    float d[4];
    char padAE0[0xB10 - 0xAE0];
    float scale;
    float alpha;
};

extern "C" void func_0049E780(State *s) {
    float colour[4], scale[4];
    colour[0] = colour[1] = colour[2] = 255.0f;
    memcpy(&colour[3], &s->alpha, 4);
    memcpy(&scale[0], &s->scale, 4);
    memcpy(&scale[1], &s->scale, 4);
    memcpy(&scale[2], &s->scale, 4);
    memcpy(&scale[3], &s->alpha, 4);
    float out[16][4];
    for (int r = 0; r < 8; ++r)
        for (int k = 0; k < 4; ++k) out[r][k] = r < 3 ? s->a[r][k] : s->a[r][k] * scale[k];
    if (s->flags & 0x80) {
        float d[4];
        memcpy(d, s->d, 16);
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 4; ++k) out[8 + r][k] = 0.0f + d[r];
        memcpy(gt4_vec(18), d, 16);
    } else {
        memcpy(out[8], s->b, 48);
    }
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k) out[11 + r][k] = s->c[r][k] * scale[k];
    memcpy(out[15], colour, 16);
    float *p = s->top - 0x40;
    s->top = p;
    memcpy(p, out, sizeof out);
    memcpy(gt4_vec(8), out[7], 16);
    for (int r = 0; r < 7; ++r) memcpy(gt4_vec(9 + r), out[8 + r], 16);
    memcpy(gt4_vec(16), colour, 16);
    memcpy(gt4_vec(17), scale, 16);
    if (s->dist < 6.0f)
        s->qwc = 0xB6;
    else if (s->dist < 12.0f)
        s->qwc = 0xF1;
    else
        s->qwc = 0x12C;
    s->tag[3] = 0x6C10C0C9;
    s->tag[0] = 0x30000010;
    s->tag[1] = (u32)p & 0x0FFFFFFF;
    s->tag[2] = 0;
}
