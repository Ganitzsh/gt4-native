/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049F170 (VU0 asm): pushes six quadwords on the packet stack (s->top, +0x1FC, moves down
 * 0x60): the view matrix at +0xB20 (four rows) and the two quadwords at +0xBB0 times +0xBA0, lane by lane; then the
 * DMA REF tag (+0x30) and VIF UNPACK (+0x3C) point at them and the VU1 address word (+0x18) is 0x215. */
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned int u32;

struct State {
    char pad0[0x18];
    u32 f18;
    char pad1C[0x30 - 0x1C];
    u32 tag[4];
    char pad40[0x1FC - 0x40];
    float *top;
    char padB00[0xB20 - 0x200];
    float m[4][4];
    char padB60[0xBA0 - 0xB60];
    float scale[4];
    float v[2][4];
};

extern "C" void func_0049F170(State *s) {
    float *p = s->top - 0x18;
    s->top = p;
    float q[6][4];
    memcpy(q, s->m, 64);
    for (int r = 0; r < 2; ++r)
        for (int k = 0; k < 4; ++k) q[4 + r][k] = s->v[r][k] * s->scale[k];
    memcpy(p, q, sizeof q);
    s->f18 = 0x215;
    s->tag[3] = 0x6C06C0C9;
    s->tag[0] = 0x30000006;
    s->tag[1] = (u32)p & 0x0FFFFFFF;
    s->tag[2] = 0;
}
