/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A3910 (provisional draft build/swarm/a2/004a3910_best.cpp, not matched): context 1's
 * origin (XYOFFSET_1) back to the main buffer's (+0x26C, +0x26E) plus half a pixel, marked dirty (+0x1D8).
 * The original wrote it as an A+D packet; here it is a state record (compat/game/gt4_gfx.h). */
#include "gt4_gfx.h"
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned long u64;
struct G { char p0[0x1D8]; u64 f1D8; char p1E0[0x18]; char *f1F8; char p1FC[0x70]; u16 f26C, f26E; };
extern "C" void func_004A3910(G *g) {
 u32 a=g->f26C+8, b=g->f26E+8;
 unsigned long long state[2];
 state[0] = (unsigned long long)a | ((unsigned long long)b << 32);
 state[1] = 0x18;
 gt4_gfx_set(g, state, 1);
 g->f1D8 |= 2;
}
