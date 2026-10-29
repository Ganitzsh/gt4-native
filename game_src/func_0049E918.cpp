/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049E918 (VU0 asm): pushes nine quadwords on the packet stack (s->top, +0x1FC, moves down
 * 0x90): three rows whose low halves are those of +0x2E0..0x300 and high halves those of +0x360..0x380 (with flag
 * 0x80 of +0x48: z and w both the x, y, z of +0xAD0 in turn), the three quadwords at +0x310 and the two at +0x3A0
 * times (scale, scale, scale, alpha) (+0xB10/+0xB14), and the colour (255, 255, 255, alpha); then the DMA REF tag
 * (+0x30) and VIF UNPACK (+0x3C) point at them, and the VU1 address word (+0x18) is 0x23, 0x52 or 0x83 as the
 * distance (+0xAA0) is under 6, under 12 or more. Leaves shared vector 9 = the scale, as the original does. */
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
    u32 *top;
    char pad200[0x2E0 - 0x200];
    u32 a[3][4];
    float c[3][4];
    char pad340[0x360 - 0x340];
    u32 b[3][4];
    char pad390[0x3A0 - 0x390];
    float e[2][4];
    char pad3C0[0xAA0 - 0x3C0];
    float dist;
    char padAA4[0xAD0 - 0xAA4];
    u32 d[4];
    char padAE0[0xB10 - 0xAE0];
    float scale;
    float alpha;
};

extern "C" void func_0049E918(State *s) {
    float colour[4], scale[4];
    colour[0] = colour[1] = colour[2] = 255.0f;
    memcpy(&colour[3], &s->alpha, 4);
    memcpy(&scale[0], &s->scale, 4);
    memcpy(&scale[1], &s->scale, 4);
    memcpy(&scale[2], &s->scale, 4);
    memcpy(&scale[3], &s->alpha, 4);
    float rows[6][4];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k) rows[r][k] = s->c[r][k] * scale[k];
    for (int r = 0; r < 2; ++r)
        for (int k = 0; k < 4; ++k) rows[3 + r][k] = s->e[r][k] * scale[k];
    memcpy(rows[5], colour, 16);

    u32 *p = s->top - 0x24;
    s->top = p;
    for (int r = 0; r < 3; ++r) {
        p[4 * r] = s->a[r][0];
        p[4 * r + 1] = s->a[r][1];
    }
    if (s->flags & 0x80) {
        for (int r = 0; r < 3; ++r) {
            p[4 * r + 2] = s->d[r];
            p[4 * r + 3] = s->d[r];
        }
    } else {
        for (int r = 0; r < 3; ++r) {
            p[4 * r + 2] = s->b[r][0];
            p[4 * r + 3] = s->b[r][1];
        }
    }
    memcpy(p + 12, rows, sizeof rows);
    memcpy(gt4_vec(9), scale, 16);
    if (s->dist < 6.0f)
        s->qwc = 0x23;
    else if (s->dist < 12.0f)
        s->qwc = 0x52;
    else
        s->qwc = 0x83;
    s->tag[3] = 0x6C09C0C9;
    s->tag[0] = 0x30000009;
    s->tag[1] = (u32)p & 0x0FFFFFFF;
    s->tag[2] = 0;
}
