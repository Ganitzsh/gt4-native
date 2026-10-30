/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049FA20 (from its matched source): the texture coordinates of an immediate batch (V2-32, one
 * pair per vertex, the count rounded up to 2) from `addr` into quadwords 0x40 on, through the lane mask C0C0C0C0
 * (004A72C0 sets it when +0x40 differs): lanes x, y take the pair, z its first, w is kept. The original wrote a
 * DMA ref tag to them with the VIF UNPACK code; natively it is a batch write (compat/game/gt4_gfx.h). */
#include "gt4_gfx.h"
struct S { char p0[0x40]; unsigned int color; char p44[0x1b4]; int *ptr; };
extern "C" void func_004A72C0(void *, unsigned int);
extern "C" void func_0049FA20(S *self, int addr, int count) {
    if (self->color != 0xc0c0c0c0) func_004A72C0(self, 0xc0c0c0c0);
    count = (count + 1) & ~1;
    gt4_gfx_batch_unpack(self, 0x40, 0x74, count, 1, 4, 4, 0xc0c0c0c0, 0, (const void *)(addr & 0x0fffffff));
}
