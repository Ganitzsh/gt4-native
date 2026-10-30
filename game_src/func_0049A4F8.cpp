/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in for 0049A4F8 (hand-written VU0 code, no matched source; the draft it replaces
 * differed in 107 of 112 instructions): writes a GS packet of n untextured vertices with their own
 * colours at ctx->packet (+0x1F8) and advances it. Header (only when the quadword count, n + 3, is
 * not 0): DMA cnt tag, VIF FLUSH, VIF DIRECT; then a REGLIST GIF tag (NLOOP 1) with PRIM =
 * (ctx->prim (+0x4A) | flags) without TME and bit 0; then a REGLIST GIF tag with NLOOP n, EOP,
 * registers RGBAQ, XYZF2. Each vertex: the point (x, y) at pos[i] goes to the screen (gt4_project_2d)
 * and the four floats at colors[i] become RGBA bytes (gt4_colour_2d: clamped to 0..1, scaled by shared
 * vector 15), as the original's VU0 microprogram 0x678 does. RGBAQ = those bytes with Q = 1.0,
 * XYZF2 = x, y (16 bits each), (z >> 4) in 24 bits and (w >> 4) in the top 8. The original
 * software-pipelines the loop; natively each vertex runs in turn (see func_0049A360.cpp). */
#include "gt4_vec.h"
extern "C" void gt4_menu_band_widen(char *xyz, int stride, int n); /* runtime/sdk.cpp: centred menus in the wide mode */

struct Ctx {
    char pad0[0x4A];
    unsigned short prim;
    char pad4C[0x1F8 - 0x4C];
    char *packet;
};

static inline void put32(char *p, unsigned int v) { *(unsigned int *)p = v; }
static inline void put64(char *p, unsigned long long v) { *(unsigned long long *)p = v; }
static inline float getf(const char *p)
{
    const unsigned char *b = (const unsigned char *)p;
    return __gt4_bits_float(b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned int)b[3] << 24));
}

extern "C" void func_0049A4F8(Ctx *ctx, int flags, int n, int pos, int colors)
{
    const int count = n;   /* for gt4_menu_band_widen */
    const char *src1 = (const char *)pos;
    const char *src3 = (const char *)colors;
    unsigned long long prim = (unsigned long long)(long long)(int)(((unsigned int)ctx->prim | (unsigned int)flags) & ~0x11u);
    int qwc = n + 3;
    char *p = ctx->packet;

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
        int xyzw[4];
        float rgba[4];
        gt4_project_2d(getf(src1), getf(src1 + 4), xyzw);
        for (int k = 0; k < 4; k++)
            rgba[k] = getf(src3 + 4 * k);
        src1 += 8;
        src3 += 16;
        put32(v + 0, gt4_colour_2d(rgba));
        put32(v + 4, 0x3F800000u);
        put32(v + 8, ((unsigned int)xyzw[0] & 0xFFFF) | ((unsigned int)xyzw[1] << 16));
        put32(v + 12, (((unsigned int)xyzw[2] >> 4) & 0xFFFFFF) | ((((unsigned int)xyzw[3] >> 4) & 0xFF) << 24));
        v += 16;
    } while (--n != 0);
    gt4_menu_band_widen(p + 0x40 + 8, 16, count);
}
