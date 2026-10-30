/* compiler: ee-gcc2.96 */
/* Native code for 004A4F48 (unmatched; from the decomp's draft, corrected against the original): a texture
 * upload of the w x h rectangle at (x, y) from game memory `src` into graphics memory at byte address `a`
 * (format `psm`, buffer width `width` pixels). The original chained it on PATH3 (the transfer set-up, IMAGE
 * tags and REF tags to the pixels, in chunks of at most 0x7FF8 qwords), kept in step with the draws by the
 * caller's interrupt handshake (004A2038); natively it is an upload record in the frame's order
 * (compat/game/gt4_gfx.h). The frame's upload total (+0x1030) counts it. */
#include "spr.h"
#include "gt4_gfx.h"
typedef unsigned int u32;
struct Scratch { char pad[0x1030]; int total; };

extern "C" void func_004A4F48(u32 a, int psm, int width, int x, int y, int w, int h, u32 src) {
    Scratch *s = (Scratch *)spr_base();
    u32 packed = ((a >> 6) & 0xffff) | (((width >> 6) & 0xff) << 16) | (psm << 24);
    int n = gt4_gfx_upload(s, packed, x, y, w, h, (const void *)src);
    s->total += n;
}
