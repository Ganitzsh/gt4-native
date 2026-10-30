/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A30B0 (provisional draft build/swarm/a5/004a30b0_best.cpp, not matched): clears the
 * rectangle r (its 12.4 corners at +12: start x, y, then end x, end y) of context 1's buffers: the test
 * `value` (TEST_1: e.g. a failing alpha test that writes Z only), the Z buffer the library keeps (+0x188)
 * with Z writes as `mode` (+0x2D4) says, then untextured sprites in `color` (Q = 1) at depth `z` in strips
 * 64 pixels wide; the test and Z buffer groups are marked dirty (+0x1D8) for the next flush. The original wrote
 * them as one DIRECT packet (A+D, then a REGLIST of XYZ3/XYZ2 pairs); here they are a state record and a draw
 * record (compat/game/gt4_gfx.h). */
#include "gt4_gfx.h"
typedef unsigned long u64;
typedef unsigned int u32;
struct Obj { char p0[0x188]; u64 state; char p190[0x48]; u64 flags; char p1e0[0x18]; char *cur; char p1fc[0xd8]; unsigned char mode; };
struct Rect { char pad[12]; union { u32 xy; unsigned short v[2]; } start; unsigned short endx,endy; };

static inline void corner(struct gt4_vertex *o, u32 xy, u32 z, u32 color, int kick)
{
    o->x = (unsigned short)xy;
    o->y = (unsigned short)(xy >> 16);
    o->z = z;
    o->rgba = color;
    o->s = o->t = 0.0f;
    o->q = 1.0f;
    o->u = o->v = 0;
    o->fog = 0;
    o->kick = kick;
    o->pad = 0;
}

extern "C" void func_004A30B0(Obj *o,u64 value,u32 color,u32 z,Rect *r) {
 unsigned long long state[4];
 state[0] = value;
 state[1] = 0x47;
 state[2] = (o->state & 0xfffffffeffffffffUL)|((u64)o->mode<<32);
 state[3] = 0x4e;
 gt4_gfx_set(o, state, 2);
 u32 a=r->start.xy;
 u32 b=(r->start.v[0]+0x400)|(r->endy<<16);
 unsigned int end=r->endx;
 int strips = 0;
 {
  u32 x = a;
  do { strips++; x += 0x400; } while ((x&0xffff)<end);
 }
 struct gt4_vertex *out = (struct gt4_vertex *)__builtin_alloca(strips * 2 * sizeof(struct gt4_vertex));
 struct gt4_vertex *v = out;
 do {
  corner(v++, a, z, color, 0);
  corner(v++, b, z, color, 1);
  a+=0x400;
  b+=0x400;
 } while ((a&0xffff)<end);
 gt4_gfx_draw(o, 6, GT4_DRAW_PRIM | GT4_DRAW_COLOR, out, strips * 2);
 o->flags|=0x48;
}
