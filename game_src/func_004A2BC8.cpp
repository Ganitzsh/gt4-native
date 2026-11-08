/* Native copy of 004A2BC8 from gt4-decomp's src/network/unit_00494578/func_004A2BC8.cpp, the source the native game
 * compiled until now: GT4's library sets the drawing origin's sub-pixel part, XYOFFSET = base + (int)((ox + sx * px) *
 * 16) per axis (at most 15), with ox / oy the field part (004A2B58, from the display mode) and sx * px / sy * py the
 * supersampling part (004A2C58's jitter passes), and marks both contexts' origin group dirty (0x20002). The register
 * only holds the sum, so this copy also sends the parts (R6, include/gt4/gfx.h OriginParts) for the host to tell them
 * apart: the field part, the supersampling part as this function computes it with ox = oy = 0, and the sum it stores.
 * The stores themselves are the original's. */
#include "gt4_gfx.h"
#include "spr.h"
typedef unsigned long long u64;
struct Spr {
    char pad[0x178]; short x; short pad1; short y;
    char pad2[0x1D8 - 0x17E]; u64 flags;
    char pad3[0x26C - 0x1E0]; unsigned short baseX, baseY;
    char pad4[0x2A8 - 0x270]; float sx, sy, ox, oy, px, py;
};

/* (int)(f * 16) as 004A2BC8 converts, 16 -> 15 */
static inline int sixteenths(float f)
{
    int i = (int)(f * 16.0f);
    if (i == 16) i = 15;
    return i;
}

extern "C" void func_004A2BC8(Spr *s)
{
    float u = (s->ox + s->sx * s->px) * 16.0f;
    float v = (s->oy + s->sy * s->py) * 16.0f;
    s->flags |= 0x20002;
    int iu = (int)u;
    if (iu == 16) iu = 15;
    int iv = (int)v;
    if (iv == 16) iv = 15;
    s->x = s->baseX + iu;
    s->y = s->baseY + iv;
    gt4_gfx_origin_parts(spr_base(), 3, sixteenths(s->ox), sixteenths(s->oy), sixteenths(s->sx * s->px),
                         sixteenths(s->sy * s->py), iu, iv);
}
