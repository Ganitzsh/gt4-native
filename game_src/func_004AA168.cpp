/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in for 004AA168 (no matched source yet): sets the current colour. The 0-255 colour
 * goes to +0xC20 and its GS form to +0xC24: R, G, B each scaled to 0-128 by (c * 129) >> 8 (the
 * original does it on the pextlb lanes with 64-bit dsll/daddu/dsrl, which never carry between
 * lanes, then ppacb), alpha unchanged. With the context active (+6), the colour is sent at once
 * (004ABD40 with +0xC2C, +0xC28 and the alpha). Returns 0 otherwise, as the original's v0 (the
 * active byte) is then 0; the provisional draft returned whatever was in its register. */
#include "spr.h"

struct Work {
    char pad0[6];
    unsigned char active;
    char pad7[0xc20 - 7];
    unsigned int color, scaled;
    int y, x;
};
extern "C" int func_004ABD40(int, int, unsigned int);

extern "C" int func_004AA168(unsigned int color) {
    Work *p = (Work *)spr_base();
    p->color = color;
    unsigned int alpha = color & 0xff000000;
    unsigned int scaled = alpha;
    for (int k = 0; k < 24; k += 8)
        scaled |= ((((color >> k) & 0xff) * 129) >> 8) << k;
    p->scaled = scaled;
    if (p->active)
        return func_004ABD40(p->x, p->y, alpha);
    return 0;
}
