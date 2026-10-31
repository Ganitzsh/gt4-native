/* compiler: ee-gcc2.96-no-strict-aliasing */
/* native stand-in, not matched: written from the assembly (no kept draft). Builds a VIF1/GS DMA packet in
   the scratchpad's packet buffer (word 0x1F8 of the 0x70002000 work area): a GS register set-up block
   (scissor, frames, offsets, alpha, test, zbuf...), then three passes of 8x4 textured sprites through a
   CLUT (each with its own RGBAQ), and eight TEX0_2/PRIM/UV/XYZ entries, closed by an UNPACK header with
   the screen rectangle derived from the scissor value; finally flags bits in the work area's word 0x1D8.
   Natively the set-up block becomes state records and the grid (program 0x3D50 on that block) is drawn by
   gt4_gfx_grid (compat/game/gt4_gfx.h): the packet is built in place as before, then replaced by the records.
   Callers (func_004A7D90, func_004A7FA8) declare it (int, int, int, int, int, int, long, int, int, int). */
#include "gt4_gfx.h"
typedef unsigned int u32;
typedef int s32;
typedef unsigned long u64;   /* 64-bit in the EE data model, natively too */
typedef long s64;

struct Packet {
    char *f0;
    u64 *ptr;
    int count;
    int fC;
    int total;
};

extern "C" void func_004A8FB0(Packet *p, char *buf);
extern "C" void func_004A9060(Packet *p, int prim);
extern "C" void func_004A90A8(Packet *p, u64 nreg, u64 regs, u64 nloop);
extern "C" int func_004A9108(Packet *p, long reg, long value);
extern "C" int func_004A9170(Packet *p, u64 a, u64 b);
extern "C" void func_004A90F0(Packet *p);
extern "C" char *func_004A91B8(Packet *p);

/* the EE's plzcw on one word: the number of leading bits equal to the sign bit, minus one */
static inline s64 plzcw32(u32 x)
{
    u32 s = x >> 31;
    s64 n = 0;
    int bit;
    for (bit = 30; bit >= 0 && ((x >> bit) & 1) == s; bit--)
        n++;
    return n;
}

static inline void ad(char *q, u64 value, u64 reg)
{
    ((u64 *)q)[0] = value;
    ((u64 *)q)[1] = reg;
}

/* a 4x8 grid of sprites: UV/XYZ3, UV/XYZ2 pairs */
static void sprites(Packet *p, u64 uv0, u64 uv1, u64 uv2, u64 uv3)
{
    s64 i;
    for (i = 0; i < 8; i++) {
        s64 xy = ((i >> 2) << 24) + ((i & 3) << 8);
        s64 u = i << 8;
        func_004A9170(p, u + uv0, xy + 0x80008000L);
        func_004A9170(p, u + uv1, xy + 0x81008080L);
        func_004A9170(p, u + uv2, xy + 0x80008080L);
        func_004A9170(p, u + uv3, xy + 0x81008100L);
    }
}

extern "C" void func_004A81A8(u32 a0, u32 a1, s32 a2, u32 a3, u32 t0, u32 t1, u64 scissor, u32 rgb0,
                              s32 rgb2, s32 rgb1)
{
    char *spr = (char *)0x70002000;
    char *t4 = *(char **)(spr + 0x1F8);
    u32 base0 = a0 >> 11;
    u32 fbp1 = a3 >> 11;
    u32 cbp = t1 >> 6;
    u64 frame;
    Packet pk;
    char *hdr;
    u32 *s3;
    int tex0_idx;
    s32 total4;
    char *end;
    s32 qwc;

    ((u32 *)t4)[0] = 0x10000012;
    ((u32 *)t4)[2] = 0x06008000;
    ((u32 *)t4)[6] = 0x13000000;
    ((u32 *)t4)[7] = 0x51000011;
    ((u32 *)t4)[1] = 0;
    ((u32 *)t4)[3] = 0;
    ((u32 *)t4)[4] = 0;
    ((u32 *)t4)[5] = 0;
    ad(t4 + 0x20, 0x1000000000008010UL, 0xE);
    ad(t4 + 0x30, 0x0FFF00000FFF0000UL, 0x40);
    ad(t4 + 0x40, scissor, 0x41);
    frame = (u64)(u32)(a2 << 16);
    ad(t4 + 0x50, frame | (fbp1 & 0x1FF) | 0xFF00000000000000UL, 0x4C);
    ad(t4 + 0x60, frame | ((a1 >> 11) & 0x1FF) | 0xFF00000001000000UL, 0x4D);
    ad(t4 + 0x70, 0x0000800000008000UL, 0x18);
    ad(t4 + 0x80, (u64)0xFE00 << 30, 0x19);
    ad(t4 + 0x90, ((u64)0x80 << 32) | 0x68, 0x42);
    ad(t4 + 0xA0, 1, 0x1A);
    ad(t4 + 0xB0, 0, 0x09);
    ad(t4 + 0xC0, 1, 0x14);
    ad(t4 + 0xD0, 1, 0x15);
    ad(t4 + 0xE0, 0x31002, 0x47);
    ad(t4 + 0xF0, 0x31002, 0x48);
    ad(t4 + 0x100, (u64)0x8000 << 17, 0x4E);
    ad(t4 + 0x110, (u64)0x8000 << 17, 0x4F);
    ad(t4 + 0x120, 0, 0x49);

    hdr = t4 + 0x130;
    *(char **)(spr + 0x1F8) = hdr;
    s3 = (u32 *)(t4 + 0x140);
    func_004A8FB0(&pk, t4 + 0x1A0);

    /* pass 1 */
    func_004A9060(&pk, 0x116);
    tex0_idx = func_004A9108(&pk, 6, ((u64)(t0 >> 6) << 37) | 0xA000000580000000UL);
    func_004A9108(&pk, 1, (u64)rgb0);
    func_004A9108(&pk, 8, 0);
    func_004A90A8(&pk, 8, 0xFFFFFFFF53D353D3UL, 8);
    sprites(&pk, 0, 0x04000080, 0x100000, 0x04100080);

    /* pass 2 */
    func_004A9060(&pk, 0x156);
    func_004A9108(&pk, 1, (u64)(u32)rgb1);
    func_004A90A8(&pk, 8, 0xFFFFFFFF53D353D3UL, 8);
    sprites(&pk, 0x80, 0x04000100, 0x100080, 0x04100100);

    /* pass 3 */
    func_004A9060(&pk, 0x156);
    func_004A9108(&pk, 1, (u64)(u32)rgb2);
    func_004A9108(&pk, 8, 0x3F73);
    func_004A90A8(&pk, 8, 0xFFFFFFFF53D353D3UL, 8);
    sprites(&pk, 0x200040, 0x042000C0, 0x300040, 0x043000C0);

    /* TEX0_2, PRIM, UV/XYZ3, UV/XYZ2 x 8 */
    func_004A90A8(&pk, 6, 0xFFFFFFFFFF53D307UL, 8);
    {
        u64 cb = (u64)(u32)(fbp1 << 5);
        u64 tb = (u64)cbp;
        s64 i;
        total4 = pk.total;
        for (i = 0; i < 8; i++) {
            u64 idx = (i & 1) | ((i & 2) << 1) | ((i & 4) >> 1);
            u64 tex = ((cb + (idx << 2)) << 37) | tb | 0xA000000D4D304000UL;
            func_004A9170(&pk, tex, 0x316);
            func_004A9170(&pk, 0, 0);
            func_004A9170(&pk, 0x02000080, 0);
        }
    }
    func_004A90F0(&pk);
    end = func_004A91B8(&pk);

    /* DMA cnt + UNPACK header of the block, closing VIF codes after it */
    qwc = (s32)(end - hdr) / 16;
    *(char **)(spr + 0x1F8) = end;
    ((u32 *)end)[0] = 0x150007AA;
    ((u32 *)end)[1] = 0x11000000;
    ((u32 *)end)[3] = 0x06000000;
    ((u32 *)end)[2] = 0;
    *(char **)(spr + 0x1F8) = end + 0x10;
    ((u32 *)hdr)[0] = qwc | 0x10000000;
    ((u32 *)hdr)[1] = 0;
    ((u32 *)hdr)[2] = 0;
    ((u32 *)hdr)[3] = ((pk.total + 6) << 16) | 0x6C00C000;

    {
        s64 y0 = (s64)(((scissor >> 32) & 0xFFF) << 4);   /* SCAY0 * 16 */
        u64 y0hi = y0 & 0xFE00;
        s64 y0blk = y0 >> 9;
        u64 x1 = (scissor >> 16) & 0xFFF;                /* SCAX1 */
        u64 y1 = (scissor >> 48) & 0xFFF;                /* SCAY1 */
        s64 x0 = (s64)((scissor & 0xFFF) << 4);           /* SCAX0 * 16 */
        u64 x0hi = x0 & 0xFC00;
        s64 x0blk = x0 >> 10;
        s32 w, h, odd;
        u32 v;
        s64 lz, sh;
        u64 m, fbits, addr;

        s3[1] = pk.fC + 6;
        w = (s32)((s64)(x1 >> 6) - x0blk);
        h = (s32)((s64)(y1 >> 5) - y0blk);
        s3[2] = (pk.total + 1) / 2;
        s3[0x48 / 4] = (s32)(((y0hi + 0x3F80) << 16) | x0hi);
        s3[0x58 / 4] = (s32)(((y0hi + 0x4180) << 16) | (x0hi + 0x80));
        s3[0x4C / 4] = (u32)(s32)(y0hi + 0x3780) << 16;
        s3[0x5C / 4] = (u32)(s32)(y0hi + 0x3980) << 16;
        s3[0x38 / 4] = w;
        s3[0x3C / 4] = h;
        odd = 0;
        if ((w & 1) == 0)
            odd = ((h ^ 1) & 1) != 0;

        /* a float built by hand: -(((a2 - w - 1) << 5)) * 2^-92 */
        v = (u32)(a2 - w - 1) << 5;
        lz = (plzcw32(v) & 0x1F) - 7;
        sh = (s32)lz;
        m = ((u64)v << (sh & 63)) & 0x7FFFFF;
        fbits = (((u64)(0x3A - lz)) << 23) | m | 0xFFFFFFFF80000000UL;
        addr = (((u64)base0 + x0blk + y0blk * 10) << 5) + 0x9D308000UL;

        s3[0x14 / 4] = total4;
        s3[0xC / 4] = odd;
        s3[0x20 / 4] = (s32)addr;
        s3[0x24 / 4] = 0x94000000;
        s3[0x28 / 4] = (s32)fbits;
        s3[0x10 / 4] = tex0_idx;
    }
    *(u64 *)(spr + 0x1D8) |= 0x3107FB07FBUL;


    /* natively: the set-up's register pairs (t4 + 0x30 .. 0x120) as state, then the grid on its block (the qwords
       the UNPACK would have sent, from hdr + 16); the packet area is reused for the records */
    {
        const int pairs = 16;
        const int blockQwords = pk.total + 6;
        unsigned long long *state = (unsigned long long *)__builtin_alloca(pairs * 16);
        char *block = (char *)__builtin_alloca(blockQwords * 16);
        for (int k = 0; k < pairs; k++) {
            state[2 * k] = ((u64 *)(t4 + 0x30 + 16 * k))[0];
            state[2 * k + 1] = ((u64 *)(t4 + 0x30 + 16 * k))[1];
        }
        for (int k = 0; k < blockQwords * 16; k++) block[k] = (hdr + 16)[k];
        *(char **)(spr + 0x1F8) = t4;
        /* a pass of its own (include/gt4/gfx.h kGrade): the source frame (a0), the destination (FRAME_2), the curve
           palette (t0), the weight columns of the three passes (R: rgb0, B: rgb1, G: rgb2) */
        if (gt4_gfx_grade(spr, base0, (a1 >> 11) & 0x1FF, (u32)a2, t0 >> 6, rgb0, (u32)rgb2, (u32)rgb1, scissor))
            return;
        gt4_gfx_set(spr, state, pairs);
        gt4_gfx_grid(spr, 0x3D50, block, blockQwords);
        gt4_gfx_effect_end(spr);
    }
}
