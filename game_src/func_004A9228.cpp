/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A9228 (provisional draft drafts/004a9228, not matched): uploads the 16 x 16 palette at
 * D_0084B080 (32-bit texels) to graphics memory at byte address `addr` (one 64-pixel row wide); the first call
 * also builds it (004A91D0, once: D_0062A034). The original sent the transfer set-up and a REF to the pixels
 * through DIRECTHL; here it is an upload record (compat/game/gt4_gfx.h). */
#include "spr.h"
#include "gt4_gfx.h"
typedef unsigned int u32;
extern char D_0084B080[];
extern char D_0062A034;
extern "C" void func_004A91D0(void) throw();

extern "C" void func_004A9228(u32 addr) {
    gt4_gfx_upload(spr_base(), ((addr >> 6) & 0x3FFF) | 0x10000, 0, 0, 16, 16, D_0084B080);
    if (!D_0062A034) {
        D_0062A034 = 1;
        return func_004A91D0();
    }
}
