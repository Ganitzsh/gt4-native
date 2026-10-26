/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in for 0049A4F8 (hand-written VU0 code, no matched source; the draft it replaces
 * differed in 107 of 112 instructions): writes a GS packet of n untextured vertices with their own
 * colours at ctx->packet (+0x1F8) and advances it. Header (only when the quadword count, n + 3, is
 * not 0): DMA cnt tag, VIF FLUSH, VIF DIRECT; then a REGLIST GIF tag (NLOOP 1) with PRIM =
 * (ctx->prim (+0x4A) | flags) without TME and bit 0; then a REGLIST GIF tag with NLOOP n, EOP,
 * registers RGBAQ, XYZF2. Each vertex: the 8 bytes at pos[i] go to vf1.xy and the 16 at
 * colors[i] (4 floats) to vf3; VU0 microprogram 0x678 transforms the position (ftoi4) and scales
 * the colour by vf15 (ftoi0). RGBAQ = the low bytes of vf3's four lanes (ppach + ppacb) with
 * Q = 1.0, XYZF2 = x, y (16 bits each), (z >> 4) in 24 bits and (w >> 4) in the top 8. The original
 * software-pipelines the loop; natively each vertex runs in turn (see func_0049A360.cpp). */
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

extern "C" void func_0049A4F8(Ctx *ctx, int flags, int n, int pos, int colors)
{
    const char *src1 = (const char *)pos;
    const char *src3 = (const char *)colors;
    unsigned long long prim = (unsigned long long)(long long)(int)(((unsigned int)ctx->prim | (unsigned int)flags) & ~0x11u);
    int qwc = n + 3;
    char *p = ctx->packet;
    unsigned int *vf1 = (unsigned int *)__gt4_vu0.vf[1];
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
    put64(p + 0x38, 0x41);
    put64(p + 0x30, ((unsigned long long)(long long)n | 0x8000) | 0x2400000000000000ull);

    char *v = p + 0x40;
    do {
        vf1[0] = get32(src1);
        vf1[1] = get32(src1 + 4);
        vf1[2] = 0;
        vf1[3] = 0;
        for (int k = 0; k < 4; k++)
            vf3[k] = get32(src3 + 4 * k);
        src1 += 8;
        src3 += 16;
        __gt4_vu0_callms(0x678);
        put32(v + 0, (vf3[0] & 0xFF) | (vf3[1] & 0xFF) << 8 | (vf3[2] & 0xFF) << 16 | (vf3[3] & 0xFF) << 24);
        put32(v + 4, 0x3F800000u);
        put32(v + 8, (vf1[0] & 0xFFFF) | (vf1[1] << 16));
        put32(v + 12, ((vf1[2] >> 4) & 0xFFFFFF) | (((vf1[3] >> 4) & 0xFF) << 24));
        v += 16;
    } while (--n != 0);
}
