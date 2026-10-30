/* compiler: ee-gcc2.96-nsa-nosib-as2004g */
/* Native version of 0049D580 (matched with VU0 asm, VU0 microprogram 0x478): a batch of float vertex positions
 * skinned by four bones into a VIF packet at ctx->cursor (+0x1F8). Header: a DMA cnt tag (its quadword count
 * written last), 0, and a VIF UNPACK V3-32 of n vertices (n << 16 | 0x6800C000); then the n skinned positions
 * (three floats each), padded with zero words to 16 bytes; the cursor moves past it. The command stream: groups
 * of a halfword header (count in bits 0..14, bit 15: another group follows), a halfword the bone loader ignores,
 * four bone index bytes (0049D970 loads the bones into shared vectors 8..23), then `count` 28-byte vertices:
 * position (three floats, unaligned) and four bone weights. Each position goes through 0x478's sum (here with
 * scale 1): bone k's rows times the position times weight k, plus its translation times weight k.
 * Natively the values go to the batch as batch writes (compat/game/gt4_gfx.h) instead of the packet, the
 * quadwords filled as the original's UNPACK filled them (gt4_batch_v3). */
#include "gt4_gfx.h"
#include "gt4_vec.h"
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned char u8;

struct Ctx {
    char pad0[0x1F8];
    u32 *cursor;
};

extern "C" u8 *func_0049D970(Ctx *ctx, u8 *cmd);

/* VU0 microprogram 0x478 on one vertex: out = sum over bones k of (rows 8+4k..10+4k) * (pos * scale * w[k])
 * + row 11+4k * w[k], in the program's order */
static void skin_position(float *out, const float *pos, const float *w, float scale) {
    float p[3], q[4][3];
    for (int k = 0; k < 3; ++k) p[k] = pos[k] * scale;
    for (int b = 0; b < 4; ++b)
        for (int k = 0; k < 3; ++k) q[b][k] = p[k] * w[b];
    float r[3];
    for (int k = 0; k < 3; ++k) {
        float acc = gt4_vec(8)[k] * q[0][0];
        acc += gt4_vec(9)[k] * q[0][1];
        acc += gt4_vec(10)[k] * q[0][2];
        acc += gt4_vec(11)[k] * w[0];
        acc += gt4_vec(12)[k] * q[1][0];
        acc += gt4_vec(13)[k] * q[1][1];
        acc += gt4_vec(14)[k] * q[1][2];
        acc += gt4_vec(15)[k] * w[1];
        acc += gt4_vec(16)[k] * q[2][0];
        acc += gt4_vec(17)[k] * q[2][1];
        acc += gt4_vec(18)[k] * q[2][2];
        acc += gt4_vec(19)[k] * w[2];
        acc += gt4_vec(20)[k] * q[3][0];
        acc += gt4_vec(21)[k] * q[3][1];
        acc += gt4_vec(22)[k] * q[3][2];
        acc += gt4_vec(23)[k] * w[3];
        r[k] = acc;
    }
    for (int k = 0; k < 3; ++k) out[k] = r[k];
}

extern "C" void func_0049D580(Ctx *ctx, u16 *cmd, int n) {
    float *positions = (float *)__builtin_alloca((n > 0 ? n : 1) * 12);
    float *out = positions;

    u16 hdr;
    do {
        hdr = cmd[0];
        const u8 *item = func_0049D970(ctx, (u8 *)(cmd + 2));
        for (int count = hdr & 0x7FFF; count != 0; --count) {
            const float *f = (const float *)item;
            skin_position(out, f, f + 3, 1.0f);
            out += 3;
            item += 0x1C;
        }
        cmd = (u16 *)item;
    } while (hdr & 0x8000);
    gt4_batch_v3(ctx, 0, positions, n, 0);
}
