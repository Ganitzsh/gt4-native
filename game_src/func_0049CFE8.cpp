/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049CFE8 (unmatched draft): pushes five quadwords below s->top (+0x1FC) and moves top down to
 * them: the matrix (shared vectors 28..31 in mode 2, else the one at +0xF50) and the quadword at +0xB00; then sets the
 * DMA tag at +0x30 (ref, 5 quadwords, at that address) with its VIF code (+0x3C) and the size +0x18 = 0x2C1. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;
typedef unsigned int u32;

struct State {
    u8 f0;
    u8 mode;
    char pad2[0x18 - 2];
    u32 f18;
    char pad1C[0x30 - 0x1C];
    u32 tag[4];
    char pad40[0x1FC - 0x40];
    float (*top)[4];
    char pad200[0xB00 - 0x200];
    float vB00[4];
    char padB10[0xF50 - 0xB10];
    float m[4][4];
};

extern "C" void func_0049CFE8(State *s) {
    float rows[4][4];
    if (s->mode == 2) {
        for (int r = 0; r < 4; ++r) memcpy(rows[r], gt4_vec(28 + r), 16);
    } else {
        memcpy(rows, s->m, 64);
    }
    float (*p)[4] = s->top - 5;
    s->top = p;
    memcpy(p, rows, 64);
    memcpy(p[4], s->vB00, 16);
    s->tag[3] = 0x6C05C0C9;
    s->f18 = 0x2C1;
    s->tag[0] = 0x30000005;
    s->tag[1] = (u32)p & 0x0FFFFFFF;
    s->tag[2] = 0;
}
