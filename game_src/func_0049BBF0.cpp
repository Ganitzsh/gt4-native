/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049BBF0 (matched): draws the immediate batch 0049BBB0 opened and 0049BE10 filled, if it has
 * vertices: the transform and the batch's GS state when they changed, then its strip count and lengths (S-8 into
 * lane w from 0x40 on, under the lane mask 3F3F3F3F), the output packet's GIF tag (NLOOP the vertex count, EOP,
 * the primitive with the library's flags, ST RGBAQ XYZF2) at 0xC0 and the run of the renderer. The original wrote
 * these as VIF codes behind a DMA tag at the library's cursor; natively they go as batch records
 * (compat/game/gt4_gfx.h), as in 0049C2A8. */
#include "gt4_gfx.h"
#include "spr.h"
typedef unsigned int u32;
struct State {
 unsigned char flag; char p1[2]; unsigned char mode;
 char p4[0x10]; int count; char p18[8]; float value;
 char p24[0x1c]; u32 color; char p44[6]; unsigned short flags;
 char p4c[0x18c]; unsigned long a; char p1e0[8]; unsigned long b;
 char p1f0[8]; unsigned *cursor; char p1fc[0xe2c]; int frame;
};
extern unsigned char D_00624C70;
extern unsigned D_0084AF30;
extern int D_0084AF34;
extern unsigned char D_0084AF40[];
extern "C" void func_0049E480(State *);
extern "C" void func_0049E548(State *, int, float);
extern "C" void func_0049E5F8(State *, int);
static inline State *get(State *const &p) { return p; }
extern "C" void func_0049BBF0() {
 State *s=get((State *)spr_base());
 if(D_00624C70) {
  D_00624C70=0;
  if(D_0084AF30) {
   s->frame++;
   if(s->flag) func_0049E480(s);
   int n=s->count;
   if(!s->mode || s->value!=0.0f) {
    --n;
    func_0049E548(s,0xc1,0.0f);
    s->mode=1;
   }
   if(s->a & s->b) {
    func_0049E5F8(s,0xd9);
    n-=5;
   }
   u32 f=s->flags | 4;
   unsigned char *strips=(unsigned char *)__builtin_alloca(D_0084AF34+4);
   strips[0]=D_0084AF34;
   for(int i=0;i<D_0084AF34;i++) strips[1+i]=D_0084AF40[i]*3-6;
   gt4_gfx_batch_unpack(s, 0x40, 0x72, D_0084AF34+1, 1, 4, 4, 0x3f3f3f3f, 0, strips);
   s->color=0x3f3f3f3f;
   u32 tag[3];
   tag[0]=D_0084AF30|0x8000;
   tag[1]=(f<<15)|0x30004000;
   tag[2]=0x412;
   gt4_gfx_batch_unpack(s, 0xC0, 0x68, 1, 1, 4, 4, s->color, 0, tag);
   gt4_gfx_batch_run(s, (u32)n&0xffff, GT4_KEEP_ITOP);
  }
 }
}
