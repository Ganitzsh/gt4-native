/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in for 0049A6A0 (hand-written VU0 code, no matched source; the draft it replaces
 * differed in 122 of 126 instructions): writes a GS packet of n textured vertices with their own
 * colours at ctx->packet (+0x1F8) and advances it. Header (only when the quadword count,
 * (3n + 1) / 2 + 3, is not 0): DMA cnt tag, VIF FLUSH, VIF DIRECT; then a REGLIST GIF tag (NLOOP 1)
 * with PRIM = ctx->prim (+0x4A) | flags | TME; then a REGLIST GIF tag with NLOOP n, EOP, registers
 * ST, RGBAQ, XYZF2. Each vertex: the 8 bytes at pos[i] go to vf1.xy, the 8 at uv[i] to vf2.xy and
 * the 16 at colors[i] (4 floats) to vf3; VU0 microprogram 0x678 transforms them (vf1: the screen
 * position, ftoi4; vf2: S, T, Q; vf3: the colour scaled by vf15, ftoi0). ST = vf2.xy, RGBAQ = the
 * low bytes of vf3's lanes with Q = vf2.z, XYZF2 = x, y (16 bits each), (z >> 4) in 24 bits and
 * (w >> 4) in the top 8. The original software-pipelines the loop; natively each vertex runs in
 * turn (see func_0049A360.cpp). */
#include "gt4_asm.h" /* __gt4_vu0 (the VU0 registers) and __gt4_vu0_callms (vcallms) */

struct Ctx {
    char pad0[0x4A];
    unsigned short prim;
    char pad4C[0x1F8 - 0x4C];
    char *packet;
};

static inline void put32(char *p, unsigned int v) { *(unsigned int *)p = v; }
static inline void put64(char *p, unsigned long long v) { *(unsigned long long *)p = v; }
static inline unsigned int get32(const char *p)
{
    const unsigned char *b = (const unsigned char *)p;
    return b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned int)b[3] << 24);
}

extern "C" void func_0049A6A0(Ctx *ctx, int flags, int n, int pos, int uv, int colors)
{
    const char *src1 = (const char *)pos;
    const char *src2 = (const char *)uv;
    const char *src3 = (const char *)colors;
    unsigned long long prim = (unsigned long long)(long long)(int)((unsigned int)ctx->prim | (unsigned int)flags | 0x10);
    int qwc = ((n * 3 + 1) >> 1) + 3;
    char *p = ctx->packet;
    unsigned int *vf1 = (unsigned int *)__gt4_vu0.vf[1];
    unsigned int *vf2 = (unsigned int *)__gt4_vu0.vf[2];
    unsigned int *vf3 = (unsigned int *)__gt4_vu0.vf[3];

    if (qwc != 0) {
        put32(p + 0, (unsigned int)qwc | 0x10000000u);
        put32(p + 8, 0x11000000u);
        put32(p + 12, (unsigned int)qwc | 0x50000000u);
        put32(p + 4, 0);
        ctx->packet = ctx->packet + (qwc << 4) + 16;
    }
    put64(p + 0x10, 0x1400000000000001ull);
    put64(p + 0x18, 0);
    put64(p + 0x20, prim);
    put64(p + 0x38, 0x412);
    put64(p + 0x30, ((unsigned long long)(long long)n | 0x8000) | 0x3400000000000000ull);

    char *v = p + 0x40;
    do {
        vf1[0] = get32(src1);
        vf1[1] = get32(src1 + 4);
        vf1[2] = 0;
        vf1[3] = 0;
        vf2[0] = get32(src2);
        vf2[1] = get32(src2 + 4);
        vf2[2] = 0;
        vf2[3] = 0;
        for (int k = 0; k < 4; k++)
            vf3[k] = get32(src3 + 4 * k);
        src1 += 8;
        src2 += 8;
        src3 += 16;
        __gt4_vu0_callms(0x678);
        put32(v + 0, vf2[0]);
        put32(v + 4, vf2[1]);
        put32(v + 8, (vf3[0] & 0xFF) | (vf3[1] & 0xFF) << 8 | (vf3[2] & 0xFF) << 16 | (vf3[3] & 0xFF) << 24);
        put32(v + 12, vf2[2]);
        put32(v + 16, (vf1[0] & 0xFFFF) | (vf1[1] << 16));
        put32(v + 20, ((vf1[2] >> 4) & 0xFFFFFF) | (((vf1[3] >> 4) & 0xFF) << 24));
        v += 24;
    } while (--n != 0);
}
