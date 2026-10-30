/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049F9C8 (from its matched source): the colours of an immediate batch (V4-8, unsigned, one
 * per vertex, the count rounded up to 4) from `colors` into quadwords 0x80 on. The original wrote a DMA ref tag to
 * them with the VIF UNPACK code; natively it is a batch write (compat/game/gt4_gfx.h). */
#include "gt4_gfx.h"
struct S { char pad[0x40]; unsigned int mask; };
extern "C" void func_0049F9C8(S *s, int colors, int count) {
    const int n = (count + 3) & ~3;
    gt4_gfx_batch_unpack(s, 0x80, 0x6E, n, 1, 4, 4, s->mask, 0, (const void *)(colors & 0x0FFFFFFF));
}
