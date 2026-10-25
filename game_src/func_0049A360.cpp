/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in (not matched): writes a GS packet of n vertices at ctx->packet (+0x1F8) and
 * advances it. Header (only when the quadword count, (3n + 1) / 2 + 3, is not 0): DMA cnt tag,
 * VIF FLUSH, VIF DIRECT; then a GIF tag (NLOOP 1, one register) with the PRIM value
 * ctx->prim (+0x4A) | flags | 0x10, and a REGLIST GIF tag with NLOOP n, EOP, registers ST, RGBAQ,
 * XYZF2. Each vertex: the 8 bytes at pos[i] go to vf1.xy and the 8 bytes at uv[i] to vf2.xy, VU0
 * microprogram 0x678 transforms them (vf1: the screen position, ftoi4; vf2: S, T, Q), and the
 * vertex is written as ST = vf2.xy, RGBAQ = ctx->rgba (+0xC24) with Q = vf2.z, XYZF2 = x, y (16 bits
 * each), (z >> 4) in 24 bits and (w >> 4) in the top 8. The original software-pipelines the loop
 * (it reads vertex i's result after starting vertex i+1); natively each vertex runs in turn, which
 * gives the same results. The vf1.z/w and vf2.w the original passes are the stale upper halves of
 * its GPRs (zero after the first vertex); natively they are 0 (the microprogram reads vf1.z only). */
#include "gt4_asm.h" /* __gt4_vu0 (the VU0 registers) and __gt4_vu0_callms (vcallms) */

struct Ctx {
    char pad0[0x4A];
    unsigned short prim;
    char pad4C[0x1F8 - 0x4C];
    char *packet;
    char pad1FC[0xC24 - 0x1FC];
    unsigned int rgba;
};

static inline void put32(char *p, unsigned int v) { *(unsigned int *)p = v; }
static inline void put64(char *p, unsigned long long v) { *(unsigned long long *)p = v; }
static inline unsigned int get32(const char *p)
{
    const unsigned char *b = (const unsigned char *)p;
    return b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned int)b[3] << 24);
}

extern "C" void func_0049A360(Ctx *ctx, int flags, int n, int pos, int uv)
{
    const char *src1 = (const char *)pos;
    const char *src2 = (const char *)uv;
    unsigned int rgba = ctx->rgba;
    long long prim = (long long)(int)((unsigned int)ctx->prim | (unsigned int)flags) | 0x10;
    int qwc = ((n * 3 + 1) >> 1) + 3;
    char *p = ctx->packet;
    char *v;
    float *vf1 = __gt4_vu0.vf[1];
    float *vf2 = __gt4_vu0.vf[2];

    if (qwc != 0) {
        put32(p + 0, (unsigned int)qwc | 0x10000000u);
        put32(p + 8, 0x11000000u);
        put32(p + 12, (unsigned int)qwc | 0x50000000u);
        put32(p + 4, 0);
        ctx->packet = ctx->packet + (qwc << 4) + 16;
    }
    put64(p + 0x10, 0x1400000000000001ull);
    put64(p + 0x18, 0);
    put64(p + 0x20, (unsigned long long)prim);
    put64(p + 0x38, 0x412);
    put64(p + 0x30, ((unsigned long long)(long long)n | 0x8000) | 0x3400000000000000ull);

    v = p + 0x40;
    do {
        unsigned int w[4];
        ((unsigned int *)vf1)[0] = get32(src1);
        ((unsigned int *)vf1)[1] = get32(src1 + 4);
        ((unsigned int *)vf1)[2] = 0;
        ((unsigned int *)vf1)[3] = 0;
        ((unsigned int *)vf2)[0] = get32(src2);
        ((unsigned int *)vf2)[1] = get32(src2 + 4);
        ((unsigned int *)vf2)[2] = 0;
        ((unsigned int *)vf2)[3] = 0;
        src1 += 8;
        src2 += 8;
        __gt4_vu0_callms(0x678);
        for (int k = 0; k < 4; k++)
            w[k] = ((unsigned int *)vf1)[k];
        put32(v + 0, ((unsigned int *)vf2)[0]);
        put32(v + 4, ((unsigned int *)vf2)[1]);
        put32(v + 8, rgba);
        put32(v + 12, ((unsigned int *)vf2)[2]);
        put32(v + 16, (w[0] & 0xFFFF) | (w[1] << 16));
        put32(v + 20, ((w[2] >> 4) & 0xFFFFFF) | (((w[3] >> 4) & 0xFF) << 24));
        v += 24;
    } while (--n != 0);
}
