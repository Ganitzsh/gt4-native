/* compiler: ee-gcc2.96-nogcse */
/* Native version of 004A4DE0 (provisional draft drafts/004a4de0, checked against the original): a texture
 * upload of the w x h rectangle at (x, y) from game memory `src` into graphics memory at byte address `a`
 * (format `psm`, buffer width `width` pixels), in the frame's order. The original put it in the VIF1 stream
 * (DIRECTHL: the transfer set-up, then IMAGE tags with REF tags to the pixels, in chunks of at most 0x7FF8
 * qwords); natively it is an upload record (compat/game/gt4_gfx.h). The count at +0x102C adds its size. */
#include "gt4_gfx.h"
typedef unsigned int u32;
struct Spr { char pad[0x102c]; int count; };
extern "C" void func_004A4DE0(u32 a, int psm, int width, int x, int y, int w, int h, u32 src) {
    u32 packed = ((a >> 6) & 0xffff) | (((width >> 6) & 255) << 16) | (psm << 24);
    Spr *spr = (Spr *)0x70002000;
    spr->count += gt4_gfx_upload(spr, packed, x, y, w, h, (const void *)src);
}
