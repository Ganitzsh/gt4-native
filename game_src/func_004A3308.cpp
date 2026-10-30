/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A3308 (provisional draft build/swarm/a1/004a3308_best.cpp, not matched): makes the
 * second buffer (+0x24C: base, format, width) context 1's target: FRAME_1 for it, SCISSOR_1 the library's
 * scissor (+0x190) scaled from the screen (+0x260 / +0x268) to the buffer (+0x250 / +0x254), XYOFFSET_1 its
 * origin (+0x258) plus half a pixel; the three groups are marked dirty (+0x1D8). The original wrote them as an
 * A+D packet; here they are a state record (compat/game/gt4_gfx.h). */
#include "gt4_gfx.h"
typedef unsigned long u64;
typedef unsigned int u32;
struct Obj {
    char pad0[0x190]; u64 rect;
    char pad198[0x40]; u64 flags;
    char pad1e0[0x18]; char *packet;
    char pad1fc[0x50]; unsigned short base; unsigned char fmt, width;
    int sx,sy; unsigned short ox,oy;
    char pad25c[8]; int dx,dy;
};
extern "C" void func_004A3308(Obj *self) {
    u64 rect=self->rect;
    int xmin=((int)(rect & 0x7ff)*self->sx)/self->dx;
    int xmax=(((int)(rect>>16 & 0x7ff)*self->sx+self->sx)/self->dx)-1;
    int ymin=((int)(rect>>32 & 0x7ff)*self->sy)/self->dy;
    int ymax=(((int)(rect>>48 & 0x7ff)*self->sy+self->sy)/self->dy)-1;
    u32 tex=(self->width<<24)|(self->fmt<<16)|(self->base>>5);
    int ox=self->ox+8, oy=self->oy+8;
    unsigned long long state[6];
    state[0] = (unsigned long long)(long long)(int)tex;
    state[1] = 0x4C;
    state[2] = (unsigned long long)(u32)((xmax<<16)|xmin) | ((unsigned long long)(u32)((ymax<<16)|ymin) << 32);
    state[3] = 0x40;
    state[4] = (unsigned long long)(u32)ox | ((unsigned long long)(u32)oy << 32);
    state[5] = 0x18;
    gt4_gfx_set(self, state, 3);
    self->flags |= 0x13;
}
