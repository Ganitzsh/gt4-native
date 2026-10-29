/* compiler: ee-gcc2.96-nogcse */
/* Native version of 004A70E8 (draft with VU0 asm): a line from `from` to `to` (three floats each, any alignment)
 * as one GS packet. Both ends go through the view rows 24..27 (shared vectors, w = 1) and are clipped against
 * each other (004A6F50); if anything is left, each end is clamped by the screen row 22, divided by its w, scaled
 * and offset by the screen rows 20, 21 and converted to 12.4 fixed point, and the packet goes at the scratchpad's
 * cursor (after 0049FE28): DMA tag and VIF codes, a GIF tag with PRIM (the scratchpad's primitive bits, a line)
 * and the registers RGBAQ (the scratchpad colour, each byte widened to a word), XYZ2, XYZ2. Shared vectors 20..23
 * are left holding the scratchpad's screen rows (0x70002FA0), as the original leaves them. */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct Vec3 {
    float x, y, z;
};

extern "C" void func_0049E480(void *s);
extern "C" long func_004A6F50(float *a, float *b);
extern "C" void func_0049FE28(void);

/* the end p (view space, clipped) to the screen: clamp, 1/w, scale and offset, 12.4 */
static inline void to_screen(int *out, const float *p, const float *scale, const float *offset, const float *limit) {
    const float invw = gt4_vu_div(1.0f, p[3]);
    for (int k = 0; k < 4; ++k) {
        float v = __gt4_minf(p[k], limit[k]) * invw;
        v = offset[k] + v * scale[k];
        out[k] = gt4_ftoi(v, 4);
    }
}

extern "C" void func_004A70E8(Vec3 *from, Vec3 *to) {
    char *s = (char *)spr_base();
    if (*(u8 *)s)
        func_0049E480(s);
    for (int r = 0; r < 4; ++r)
        memcpy(gt4_vec(20 + r), s + 0xFA0 + 0x10 * r, 16);
    float a[3], b[3];
    memcpy(a, from, 12);
    memcpy(b, to, 12);
    float v[2][4];   /* v[0] from, v[1] to, in view space */
    gt4_rows4(v[0], gt4_vec(24), gt4_vec(25), gt4_vec(26), gt4_vec(27), a, 1.0f, 4);
    gt4_rows4(v[1], gt4_vec(24), gt4_vec(25), gt4_vec(26), gt4_vec(27), b, 1.0f, 4);
    if (func_004A6F50(v[1], v[0]) != 0)
        return;
    int p1[4], p2[4];
    const float *scale = gt4_vec(20), *offset = gt4_vec(21), *limit = gt4_vec(22);
    to_screen(p1, v[0], scale, offset, limit);
    to_screen(p2, v[1], scale, offset, limit);
    const u32 flags = (u32)((*(u16 *)(s + 0x4A) | 1) & 0xFFEF);
    const u32 color = *(u32 *)(s + 0xC20);
    u32 rgba[4];
    for (int k = 0; k < 4; ++k)
        rgba[k] = ((color >> (8 * k)) & 0xFF) * 0x01010101u;   /* pextlb, pextlh: the byte in all four */
    func_0049FE28();
    u32 *p = (u32 *)*(u32 *)(s + 0x1F8);
    p[0] = 0x10000004;
    p[1] = 0;
    p[2] = 0x11000000;
    p[3] = 0x50000004;
    *(u32 *)(s + 0x1F8) += 0x50;
    p += 4;
    p[0] = 0x8001;
    p[1] = (flags << 15) | 0x30004000;
    p[2] = 0x441;
    p[3] = 0;
    memcpy(p + 4, rgba, 16);
    memcpy(p + 8, p1, 16);
    memcpy(p + 12, p2, 16);
}
