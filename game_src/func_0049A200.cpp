/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in for 0049A200 (hand-written VU0 code, no matched source; the draft it replaces
 * differed in 92 of 94 instructions): writes a GS packet of n untextured vertices in the current
 * colour at ctx->packet (+0x1F8) and advances it. Header (only when the quadword count,
 * (n + 1) / 2 + 3, is not 0): DMA cnt tag, VIF FLUSH, VIF DIRECT; then a REGLIST GIF tag (NLOOP 1)
 * with PRIM = (ctx->prim (+0x4A) | flags) without TME and bit 0, and RGBAQ = the 0-255 colour
 * (+0xC20) with Q = 1.0; then a REGLIST GIF tag with NLOOP n, EOP, register XYZF2. Each vertex: the
 * 8 bytes at pos[i] go to vf1.xy, VU0 microprogram 0x678 transforms them (screen position, ftoi4)
 * and XYZF2 = x, y (16 bits each), (z >> 4) in 24 bits and (w >> 4) in the top 8. The original
 * software-pipelines the loop; natively each vertex runs in turn (see func_0049A360.cpp). */
#include "gt4_asm.h" /* __gt4_vu0 (the VU0 registers) and __gt4_vu0_callms (vcallms) */

struct Ctx {
    char pad0[0x4A];
    unsigned short prim;
    char pad4C[0x1F8 - 0x4C];
    char *packet;
    char pad1FC[0xC20 - 0x1FC];
    unsigned int color;
};

static inline void put32(char *p, unsigned int v) { *(unsigned int *)p = v; }
static inline void put64(char *p, unsigned long long v) { *(unsigned long long *)p = v; }
static inline unsigned int get32(const char *p)
{
    const unsigned char *b = (const unsigned char *)p;
    return b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned int)b[3] << 24);
}

extern "C" void func_0049A200(Ctx *ctx, int flags, int n, int pos)
{
    const char *src = (const char *)pos;
    unsigned int color = ctx->color;
    unsigned long long prim = (unsigned long long)(long long)(int)(((unsigned int)ctx->prim | (unsigned int)flags) & ~0x11u);
    int qwc = ((n + 1) >> 1) + 3;
    char *p = ctx->packet;
    unsigned int *vf1 = (unsigned int *)__gt4_vu0.vf[1];

    if (qwc != 0) {
        put32(p + 0, (unsigned int)qwc | 0x10000000u);
        put32(p + 8, 0x11000000u);
        put32(p + 12, (unsigned int)qwc | 0x50000000u);
        put32(p + 4, 0);
        ctx->packet = ctx->packet + (qwc << 4) + 16;
    }
    put64(p + 0x10, 0x2400000000000001ull);
    put64(p + 0x18, 0x10);
    put64(p + 0x20, prim);
    put32(p + 0x28, color);
    put32(p + 0x2C, 0x3F800000u);
    put64(p + 0x38, 4);
    put64(p + 0x30, ((unsigned long long)(long long)n | 0x8000) | 0x1400000000000000ull);

    char *v = p + 0x40;
    do {
        vf1[0] = get32(src);
        vf1[1] = get32(src + 4);
        vf1[2] = 0;
        vf1[3] = 0;
        src += 8;
        __gt4_vu0_callms(0x678);
        put32(v + 0, (vf1[0] & 0xFFFF) | (vf1[1] << 16));
        put32(v + 4, ((vf1[2] >> 4) & 0xFFFFFF) | (((vf1[3] >> 4) & 0xFF) << 24));
        v += 8;
    } while (--n != 0);
}
