/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 0049DB08 (VU0 asm, VU0 microprogram 0x460): a batch of packed vertex positions skinned by four
 * bones into a VIF packet at ctx->cursor (+0x1F8), as 0049D580 does for float vertices. The command stream: groups
 * of a header byte (count in bits 0..6, bit 7: another group follows), a byte the bone loader ignores, four bone
 * index bytes (0049D970 loads the bones into shared vectors 8..23), then `count` 10-byte vertices: x, y, z as
 * 16-bit integers (bytes 0..5) and four signed-byte bone weights (bytes 6..9). 0x460 converts the position to
 * floats (scaled by `scale`) and the weights to floats / 127, then sums as 0x478.
 * Natively the values go to the batch as batch writes (compat/game/gt4_gfx.h) instead of the packet, the
 * quadwords filled as the original's UNPACK filled them (gt4_batch_v3). */
#include "gt4_gfx.h"
#include "gt4_vec.h"
typedef unsigned char u8;
typedef unsigned int u32;
typedef short s16;
typedef signed char s8;

struct Ctx {
    char pad0[0x1F8];
    u32 *cursor;
};

extern "C" u8 *func_0049D970(Ctx *ctx, u8 *cmd);

/* 0x478's position sum: bones k's rows times pos * scale * w[k], plus its translation times w[k] */
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

/* 1/127 as the microprograms' I register holds it (0x3C010204) */
static float inv127() {
    union {
        u32 u;
        float f;
    } c;
    c.u = 0x3C010204u;
    return c.f;
}

extern "C" void func_0049DB08(Ctx *ctx, u8 *cmd, int n, float scale) {
    float *positions = (float *)__builtin_alloca((n > 0 ? n : 1) * 12);
    float *out = positions;

    const float i127 = inv127();
    u8 hdr;
    do {
        hdr = cmd[0];
        const u8 *item = func_0049D970(ctx, cmd + 2);
        for (int count = hdr & 0x7F; count != 0; --count) {
            float pos[3], w[4];
            for (int k = 0; k < 3; ++k) pos[k] = (float)(s16)(item[2 * k] | item[2 * k + 1] << 8);
            for (int k = 0; k < 4; ++k) w[k] = (float)(s8)item[6 + k] * i127;
            skin_position(out, pos, w, scale);
            out += 3;
            item += 10;
        }
        cmd = (u8 *)item;
    } while (hdr & 0x80);
    gt4_batch_v3(ctx, 0, positions, n, 0);
}
