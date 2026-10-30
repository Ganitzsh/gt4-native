/* compiler: ee-gcc2.96-no-strict-aliasing */
#include "gt4_gfx.h"
/* Native version of 0049F938: its MMI colour expansion (the four bytes as four words) as four stores. */
typedef unsigned int u32;
struct State {
    char pad0[0x40]; u32 color;
    char pad44[0x1B4]; u32 *cur;
    char pad1FC[0xA24]; int colors[2];
};
extern "C" void func_0049F938(State *s, int tag, int mode) {
    int color;
    if (mode) color = s->colors[1];
    else color = s->colors[0];
    /* the colour's bytes (PEXTLB, PEXTLH) in every lane of `tag` quadwords from 0x80: the original's row (STROW)
     * through the mask 55555555 with a cycle that takes no data (STCYCL 0/4), then the cycle 4/4 again */
    unsigned int row[4];
    row[0] = color & 0xff; row[1] = (color >> 8) & 0xff; row[2] = (color >> 16) & 0xff; row[3] = (color >> 24) & 0xff;
    gt4_gfx_batch_unpack(s, 0x80, 0x7C, tag, 1, 0, 4, 0x55555555, row, 0);
    s->color = 0x55555555;
}
