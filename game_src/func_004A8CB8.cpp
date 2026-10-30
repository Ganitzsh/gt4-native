/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A8CB8 (provisional draft build/native_drafts/func_004A8CB8.cpp, not matched): a colour
 * grade's palette, 256 24-bit entries in the GS's CLUT order, to graphics memory at `addr` (one 64-pixel row
 * wide). Entry k (its index swizzled) is black up to `low`; above it the level, scaled to 0..1 between `low` and
 * `high`, goes through the curve 004A8AD0, is divided by the channel's `red`, `green` or `blue` and through the
 * curve 004A8BB0, times 255, clamped to 0..`limit` (itself 0..255). The original sent the palette as a DIRECT
 * packet in the frame's list (its pixels inside the packet); here it is an upload record (compat/game/gt4_gfx.h)
 * whose pixels stay in a ring of palettes until the frame is sent (runtime/frame.cpp reads them then). */
#include "spr.h"
#include "gt4_gfx.h"
typedef unsigned int u32;
extern "C" void func_0049FE28() throw();
extern "C" float func_004A8AD0(float) throw();
extern "C" float func_004A8BB0(float) throw();

/* max.s and min.s */
static inline float fmax0(float a, float b) { return a > b ? a : b; }
static inline float fmin0(float a, float b) { return a < b ? a : b; }

/* a frame grades a few times at most (main view, mirror): 16 palettes are kept */
static unsigned char g_palettes[16][0x300] __attribute__((aligned(16)));
static unsigned int g_next;

extern "C" void func_004A8CB8(u32 addr, float red, float green, float blue, float low, float high, float limit)
{
    func_0049FE28();
    unsigned char *pixels = g_palettes[g_next++ & 15];
    unsigned char *out = pixels;
    const float scale = low != high ? 1.0f / (high - low) : 0.0f;
    red = 1.0f / fmax0(red, 0.0f);
    green = 1.0f / fmax0(green, 0.0f);
    blue = 1.0f / fmax0(blue, 0.0f);
    limit = fmin0(fmax0(limit, 0.0f), 255.0f);
    int idx = 0;
    int i = 0;
    do {
        const float v = (float)idx * 0x1.0101p-8f;   /* idx / 255 */
        int r = 0, g = 0, b = 0;
        i++;
        if (low < v) {
            const float c = func_004A8AD0(fmin0((v - low) * scale, 1.0f));
            const float rr = fmin0(fmax0(func_004A8BB0(red * c) * 255.0f, 0.0f), limit);
            const float gg = fmin0(fmax0(func_004A8BB0(green * c) * 255.0f, 0.0f), limit);
            const float bb = fmin0(fmax0(func_004A8BB0(blue * c) * 255.0f, 0.0f), limit);
            r = (int)rr;   /* cvt.w.s: toward zero */
            g = (int)gg;
            b = (int)bb;
        }
        idx = (i & 0xE7) | ((i & 8) << 1) | ((i & 0x10) >> 1);
        out[0] = (unsigned char)r;
        out[1] = (unsigned char)g;
        out[2] = (unsigned char)b;
        out += 3;
    } while (i < 256);
    gt4_gfx_upload(spr_base(), (addr >> 6 & 0x3FFF) | 0x01010000, 0, 0, 16, 16, pixels);
}
