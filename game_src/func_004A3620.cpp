/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A3620 (provisional draft build/swarm/a3/004a3620_best.cpp, not matched): a buffer
 * blend pass. It sets context 1 to sample `tex` (TEX0_1: its base, width and format, 1024 x 1024, RGBA;
 * TEX1_1 fixed LOD; CLAMP_1 repeat), no test, no dither, the blend `blend` with the fixed alpha `alpha` x 128
 * (clamped to 0..255), then draws the area of `src` (12.4 corners at +12) in strips 64 pixels wide, each a
 * textured sprite (alpha blended, texel coordinates, colour 0x80808080) reading `tex` scaled from `src`'s size
 * to `tex`'s, `offx` added to Y and `offy` to V; the groups it set are marked dirty (+0x1D8). The original
 * wrote them as one A+D packet; here they are a state record and a draw record (compat/game/gt4_gfx.h). */
#include "gt4_gfx.h"
#include "gt4_vec.h"
typedef unsigned int u32;
typedef unsigned long u64;
struct Context { char pad[0x1d8]; u64 flags; char pad2[0x18]; char *cur; };
struct Image { unsigned short base; unsigned char bw,psm; u32 width,height; unsigned short x0,y0,x1,y1; };

static inline void corner(struct gt4_vertex *o, u32 uv, u32 xy, int kick)
{
    o->x = (unsigned short)xy;
    o->y = (unsigned short)(xy >> 16);
    o->z = 0;
    o->fog = 0;
    o->rgba = 0x80808080;
    o->s = o->t = 0.0f;
    o->q = 1.0f;
    o->u = (unsigned short)uv;
    o->v = (unsigned short)(uv >> 16);
    o->kick = kick;
    o->pad = 0;
}

extern "C" void func_004A3620(Context *ctx,Image *src,Image *tex,int blend,int offx,int offy,float alpha) {
    long a=(int)gt4_minf(gt4_maxf(alpha*128.0f,0.0f),255.0f);
    unsigned long long state[14];
    state[0] = 0; state[1] = 0x3f;
    state[2] = 0; state[3] = 0x45;
    state[4] = 0; state[5] = 0x47;
    state[6] = (unsigned long long)(u32)blend | ((unsigned long long)a << 32); state[7] = 0x42;
    state[8] = 1; state[9] = 0x14;
    state[10] = (u64)tex->base | ((u64)0x8000<<19) | (((u64)tex->bw<<14)|0x28000000) | ((u64)tex->psm<<20) | ((u64)0xa000<<18);
    state[11] = 6;
    state[12] = 0; state[13] = 8;
    gt4_gfx_set(ctx, state, 7);
    u32 x=src->x0, end=x+0x400;
    u32 y=src->y0, yend=src->y1;
    u32 top=y+offx, bottom=yend+offx;
    u32 v0=offy<<16;
    u32 v1=(((yend-y)*tex->height/src->height)+offy)<<16;
    int strips = 0;
    {
        u32 xx = x, ee = end;
        do { strips++; xx = ee; ee = xx + 0x400; } while (xx < src->x1);
    }
    struct gt4_vertex *out = (struct gt4_vertex *)__builtin_alloca(strips * 2 * sizeof(struct gt4_vertex));
    struct gt4_vertex *v = out;
    do {
        u32 u0=(x-src->x0)*tex->width/src->width;
        u32 u1=(end-src->x0)*tex->width/src->width;
        corner(v++, v0|u0, (top<<16)|x, 0);
        corner(v++, v1|u1, (bottom<<16)|end, 1);
        x=end; end=x+0x400;
    } while(x<src->x1);
    gt4_gfx_draw(ctx, 0x156, GT4_DRAW_PRIM | GT4_DRAW_COLOR | GT4_DRAW_UV, out, strips * 2);
    ctx->flags|=((u64)4<<32)|0x6e0;
}
