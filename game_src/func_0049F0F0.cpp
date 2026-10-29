/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049F0F0 (matched with VU0 asm): pushes three quadwords on the packet stack (s->top, +0x1FC,
 * moves down 0x30): +0xAF0, the colour (255, 255, 255, alpha (+0xB14)) and +0xB00; then the DMA REF tag (+0x30) and
 * VIF UNPACK (+0x3C) point at them and the VU1 address word (+0x18) is 0x2A1. */
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned int u32;

struct State {
    char pad0[0x18];
    u32 qwc;
    char pad1C[0x30 - 0x1C];
    u32 tag[4];
    char pad40[0x1FC - 0x40];
    u32 *top;
    char pad200[0xAF0 - 0x200];
    u32 vAF0[4];
    u32 vB00[4];
    u32 scale;
    u32 alpha;
};

extern "C" void func_0049F0F0(State *s) {
    u32 q[12];
    memcpy(q, s->vAF0, 16);
    q[4] = q[5] = q[6] = 0x437F0000;   /* 255.0f */
    q[7] = s->alpha;
    memcpy(q + 8, s->vB00, 16);
    u32 *p = s->top - 12;
    s->top = p;
    memcpy(p, q, sizeof q);
    s->tag[3] = 0x6C03C0C9;
    s->qwc = 0x2A1;
    s->tag[0] = 0x30000003;
    s->tag[1] = (u32)p & 0x0FFFFFFF;
    s->tag[2] = 0;
}
