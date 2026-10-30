/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A1BB0 (from its matched source): starts a frame in the packet buffer. The chain head: a
 * slot for the PATH3 chain's first tag (+0x208 links it), then the frame's state: every drawing state group dirty
 * (+0x1D8) and context 1's in use (+0x1E8), the mask word (+0x40), flags. The original then wrote a DMA cnt tag
 * with the VIF codes of GT4's batch layout (BASE 0, OFFSET 435, STCYCL 4/4, STMOD 0, STMASK C0C0C0C0); natively
 * the frame's batches start with a frame record (compat/game/gt4_gfx.h), which sets the same layout. */
#include "spr.h"
#include "gt4_gfx.h"
typedef unsigned int u32;
typedef unsigned long u64;
typedef unsigned char u8;
struct Spr {
 u8 b0, b1, b2, b3, b4, b5, b6, b7;
 char p0[0x38];
 u32 w40;
 char p1[0x194];
 u64 d1d8;
 char p2[8];
 u64 d1e8;
 char p3[8];
 u32 *cur;
 char p4[0xc];
 u32 *w208;
 u32 *w20c;
 char p5[0xb0];
 u8 b2c0, b2c1, b2c2;
 char p6[0xd5d];
 u32 words[5];
};
extern "C" u32 *func_004A1BB0(void) {
 Spr *c=(Spr *)spr_base();
 u32 *p=c->cur;
 u32 *start=p;
 p+=4;
 start[1]=(u32)p;
 p[0]=0x20000000;
 c->w208=p+1;
 p+=4;
 start[0]=(u32)p;
 c->cur=p;
 gt4_gfx_batch_frame(c);
 c->d1d8=0x3ff1fff1fffUL;
 c->d1e8=0x3ff00001fffUL;
 c->w40=0xc0c0c0c0;
 c->w20c=start;
 c->b0=1;
 c->b3=0;
 c->b7=0;
 c->b2c1=0;
 c->b2c2=1;
 c->words[0]=0;
 c->words[1]=0;
 c->words[2]=0;
 c->words[3]=0;
 c->words[4]=0;
 return start;
}
