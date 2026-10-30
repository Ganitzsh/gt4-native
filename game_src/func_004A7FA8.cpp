/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 004A7FA8 (matched): a colour grade through three tone curves (ta, tb, tc: red, green, blue). It
 * builds the 256-entry 24-bit palette of the curves (in the GS's CLUT order) at graphics memory buf + 0x800 and
 * runs the screen grid (004A81A8) that maps the frame through it. The original sent the palette as a DIRECT
 * packet in the frame's list (its pixels inside the packet); here it is an upload record (compat/game/gt4_gfx.h)
 * whose pixels stay in a ring of palettes until the frame is sent (runtime/frame.cpp reads them then). */
#include "spr.h"
#include "gt4_gfx.h"
typedef unsigned int u32;
extern "C" int func_004AA558(int);
extern "C" void func_0049FE28() throw();
extern "C" void func_004A8F78(int);
extern "C" void func_004A81A8(int, int, int, int, int, int, long, int, int, int);
extern "C" int func_004AA6B8(int);
struct Buf { unsigned short fbp; unsigned char psm; unsigned char pad; int w; int h; };
struct Spr { char p0[0x170]; unsigned short a; unsigned char b; char p173[0x1C0 - 0x173]; long c; char p1c8[0x260 - 0x1C8]; Buf buf; };

/* a frame grades a few times at most (main view, mirror): 16 palettes are kept */
static unsigned char g_palettes[16][0x300] __attribute__((aligned(16)));
static unsigned int g_next;

extern "C" int func_004A7FA8(int x, int y, int z, unsigned char *ta, unsigned char *tb, unsigned char *tc)
{
    int buf = func_004AA558(0x1000);
    int p;
    int q;
    unsigned char *out;
    unsigned char *pixels;
    int i;

    func_0049FE28();
    p = buf + 0x800;
    q = buf + 0x900;
    pixels = g_palettes[g_next++ & 15];
    out = pixels;
    i = 0;
    do {
        int idx = (i & 0xE7) | ((i & 8) << 1) | ((i & 0x10) >> 1);
        i++;
        out[0] = ta[idx];
        out[1] = tb[idx];
        out[2] = tc[idx];
        out += 3;
    } while (i < 0x100);
    gt4_gfx_upload(spr_base(), ((u32)p >> 6 & 0x3FFF) | 0x01010000, 0, 0, 16, 16, pixels);
    func_004A8F78(q);
    Spr *s = (Spr *)spr_base();
    Buf *fb = &s->buf;
    func_004A81A8((int)(s->c & 0x3fff) << 6, s->a << 11, s->b, buf, p, q,
                  ((long)fb->h - 1) << 48 | ((long)fb->w - 1) << 16, x, y, z);
    return func_004AA6B8(buf);
}
