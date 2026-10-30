/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A3498 (provisional draft build/swarm/o1/004a3498_best.cpp, not matched): fills the
 * screen area (the library's 12.4 rectangle at +0x258: start x, y, end x, bottom y) with white through the
 * blend `color` (ALPHA_1's A, B, C, D) with the fixed alpha f x 128 (clamped to 0..255), no test and no
 * dither, in sprites 64 pixels wide (alpha blended, Q = 1, Z 0); the dither, blend and test groups are
 * marked dirty (+0x1D8). The original wrote them as one A+D packet; here they are a state record and a draw
 * record (compat/game/gt4_gfx.h). */
#include "gt4_gfx.h"
#include "gt4_vec.h"
struct Obj { char pad[0x1d8]; unsigned long flags; char pad2[0x18]; char *cur; char pad3[0x5c]; union { unsigned pos; struct { unsigned short x,y,end,z; } s; } coords; };

static inline void corner(struct gt4_vertex *o, unsigned int xy, int kick)
{
    o->x = (unsigned short)xy;
    o->y = (unsigned short)(xy >> 16);
    o->z = 0;
    o->fog = 0;
    o->rgba = 0xFFFFFFFF;
    o->s = o->t = 0.0f;
    o->q = 1.0f;
    o->u = o->v = 0;
    o->kick = kick;
    o->pad = 0;
}

extern "C" void func_004A3498(Obj *o,unsigned color,float f) {
 f=gt4_minf(gt4_maxf(f*128.0f,0.0f),255.0f);
 int alpha=(int)f;
 unsigned long long state[6];
 state[0] = 0; state[1] = 0x45;
 state[2] = 0; state[3] = 0x47;
 state[4] = (unsigned long long)color | ((unsigned long long)(long long)alpha << 32); state[5] = 0x42;
 gt4_gfx_set(o, state, 3);
 unsigned long a=o->coords.pos;
 unsigned long b=(o->coords.s.z<<16)|(o->coords.s.x+0x400);
 int strips = 0;
 {
  unsigned long aa = a;
  do { strips++; aa += 0x400; } while ((aa&0xffff)<o->coords.s.end);
 }
 struct gt4_vertex *out = (struct gt4_vertex *)__builtin_alloca(strips * 2 * sizeof(struct gt4_vertex));
 struct gt4_vertex *v = out;
 do {
  corner(v++, (unsigned int)a, 0);
  corner(v++, (unsigned int)b, 1);
  a+=0x400; b+=0x400;
 }while((a&0xffff)<o->coords.s.end);
 gt4_gfx_draw(o, 0x46, GT4_DRAW_PRIM | GT4_DRAW_COLOR, out, strips * 2);
 o->flags|=(4UL<<32)|0x60;
}
