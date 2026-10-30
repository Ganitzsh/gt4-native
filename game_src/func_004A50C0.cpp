/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A50C0 (from its source): a texture upload of the w x h rectangle at (x, y) from
 * game memory `data` into graphics memory at byte address `a` (format `psm`, buffer width `width` pixels),
 * at once: the original sent the transfer set-up and the pixels on DMA channel 2 and waited for it; natively
 * it is an upload that takes effect before the frame being built (compat/game/gt4_gfx.h). */
#include "gt4_gfx.h"
extern "C" void func_004A50C0(unsigned int a, int psm, int width, int x, int y, int w, int h, void *data) {
    unsigned int desc = ((a >> 6) & 0xffff) | (((width >> 6) & 0xff) << 16) | (psm << 24);
    gt4_gfx_upload_now(desc, x, y, w, h, data);
}
