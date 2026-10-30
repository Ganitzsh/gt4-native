/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in (not matched): writes a GS packet of n vertices at ctx->packet (+0x1F8) and
 * advances it. Header (only when the quadword count, (3n + 1) / 2 + 3, is not 0): DMA cnt tag,
 * VIF FLUSH, VIF DIRECT; then a GIF tag (NLOOP 1, one register) with the PRIM value
 * ctx->prim (+0x4A) | flags | 0x10, and a REGLIST GIF tag with NLOOP n, EOP, registers ST, RGBAQ,
 * XYZF2. Each vertex: the point (x, y) at pos[i] goes to the screen (gt4_project_2d: the original's VU0
 * microprogram 0x678), which also gives Q = 1 / w, and the vertex is written as ST = the (s, t) at
 * uv[i] times Q, RGBAQ = ctx->rgba (+0xC24) with Q, XYZF2 = x, y (16 bits each), (z >> 4) in 24 bits
 * and (w >> 4) in the top 8. The original software-pipelines the loop (it reads vertex i's result
 * after starting vertex i+1); natively each vertex runs in turn, which gives the same results. The
 * point's z the original passes the microprogram is the stale upper half of a GPR (zero after the first
 * vertex); natively it is 0. */
#include "gt4_gfx.h"
#include "gt4_vec.h"
extern "C" void gt4_menu_band_widen(char *xyz, int stride, int n); /* runtime/sdk.cpp: centred menus in the wide mode */

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
static inline float getf(const char *p)
{
    const unsigned char *b = (const unsigned char *)p;
    return __gt4_bits_float(b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned int)b[3] << 24));
}

extern "C" void func_0049A360(Ctx *ctx, int flags, int n, int pos, int uv)
{
    const int count = n;   /* for gt4_menu_band_widen */
    const char *src1 = (const char *)pos;
    const char *src2 = (const char *)uv;
    unsigned int rgba = ctx->rgba;
    long long prim = (long long)(int)((unsigned int)ctx->prim | (unsigned int)flags) | 0x10;
    struct gt4_vertex *out = (struct gt4_vertex *)__builtin_alloca(n * sizeof(struct gt4_vertex));
    struct gt4_vertex *v = out;
    do {
        int xyzw[4];
        const float q = gt4_project_2d(getf(src1), getf(src1 + 4), xyzw);
        const float s = getf(src2) * q, t = getf(src2 + 4) * q;
        src1 += 8;
        src2 += 8;
        v->x = (unsigned short)xyzw[0];
        v->y = (unsigned short)xyzw[1];
        gt4_vertex_zf(v, (((unsigned int)xyzw[2] >> 4) & 0xFFFFFF) | ((((unsigned int)xyzw[3] >> 4) & 0xFF) << 24));
        v->rgba = rgba;
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
