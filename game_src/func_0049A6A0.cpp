/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in for 0049A6A0 (hand-written VU0 code, no matched source; the draft it replaces
 * differed in 122 of 126 instructions): writes a GS packet of n textured vertices with their own
 * colours at ctx->packet (+0x1F8) and advances it. Header (only when the quadword count,
 * (3n + 1) / 2 + 3, is not 0): DMA cnt tag, VIF FLUSH, VIF DIRECT; then a REGLIST GIF tag (NLOOP 1)
 * with PRIM = ctx->prim (+0x4A) | flags | TME; then a REGLIST GIF tag with NLOOP n, EOP, registers
 * ST, RGBAQ, XYZF2. Each vertex, as the original's VU0 microprogram 0x678 does it: the point (x, y) at
 * pos[i] to the screen (gt4_project_2d, which gives Q = 1 / w), the (s, t) at uv[i] times Q, the four
 * floats at colors[i] to RGBA bytes (gt4_colour_2d). ST = (s, t) * Q, RGBAQ = the colour bytes with Q,
 * XYZF2 = x, y (16 bits each), (z >> 4) in 24 bits and (w >> 4) in the top 8. The original
 * software-pipelines the loop; natively each vertex runs in turn (see func_0049A360.cpp). */
#include "gt4_gfx.h"
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

extern "C" void func_0049A6A0(Ctx *ctx, int flags, int n, int pos, int uv, int colors)
{
    const int count = n;   /* for gt4_menu_band_widen */
    const char *src1 = (const char *)pos;
    const char *src2 = (const char *)uv;
    const char *src3 = (const char *)colors;
    unsigned long long prim = (unsigned long long)(long long)(int)((unsigned int)ctx->prim | (unsigned int)flags | 0x10);
    struct gt4_vertex *out = (struct gt4_vertex *)__builtin_alloca(n * sizeof(struct gt4_vertex));
    struct gt4_vertex *v = out;
    do {
        int xyzw[4];
        float rgba[4];
        const float q = gt4_project_2d(getf(src1), getf(src1 + 4), xyzw);
        const float s = getf(src2) * q, t = getf(src2 + 4) * q;
        for (int k = 0; k < 4; k++)
            rgba[k] = getf(src3 + 4 * k);
        src1 += 8;
        src2 += 8;
        src3 += 16;
        v->x = (unsigned short)xyzw[0];
        v->y = (unsigned short)xyzw[1];
        gt4_vertex_zf(v, (((unsigned int)xyzw[2] >> 4) & 0xFFFFFF) | ((((unsigned int)xyzw[3] >> 4) & 0xFF) << 24));
        v->rgba = gt4_colour_2d(rgba);
        v->s = s;
        v->t = t;
        v->q = q;
        v->u = v->v = 0;
        v->kick = 1;
        v->pad = 0;
        v++;
    } while (--n != 0);
    gt4_menu_band_widen((char *)&out[0].x, sizeof(struct gt4_vertex), count);
    gt4_gfx_draw(ctx, (unsigned int)prim, GT4_DRAW_PRIM | GT4_DRAW_FOG | GT4_DRAW_COLOR | GT4_DRAW_ST, out, count);
}
