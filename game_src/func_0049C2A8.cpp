/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049C2A8 (from its draft): its MMI colour expansion as four stores, without prefetch hints.
 * Natively the batch goes as batch records (compat/game/gt4_gfx.h) instead of DMA tags and VIF codes: the same
 * formats, lane masks and cycles, expanded by gt4_gfx_batch_unpack. */
#include "gt4_gfx.h"
#include "spr.h"
typedef unsigned int u32;
struct State {
 unsigned char flag; char p1[2]; unsigned char mode;
 char p4[0x10]; int count; int command; char p1c[4]; float value;
 char p24[0x1c]; u32 color; char p44[6]; unsigned short flags;
 char p4c[0x18c]; unsigned long a; char p1e0[8]; unsigned long b;
 char p1f0[8]; u32 *cursor; char p1fc[0xa24]; int colors[2];
 char pc28[0x400]; int frame;
};
extern "C" void func_0049C140(State *) throw();
extern "C" void func_0049E480(State *);
extern "C" void func_0049E548(State *,int,float);
extern "C" void func_0049E5F8(State *,int);
static inline State *get(State *const &p) { return p; }
extern "C" void func_0049C2A8(void *verts,void *colors,void *normals,int count,int first) {
 State *s=get((State *)spr_base());
 int lighting=0;
 int nverts=count*3;
 int aligned=(nverts+3)&~3;
 s->frame++;
 /* positions (V2-32), then their z and w zero (the row 0 through the mask 5F5F5F5F, a cycle with no data) */
 gt4_gfx_batch_unpack(s, 0, 0x64, aligned, 1, 4, 4, s->color, 0, (const void *)((u32)verts&0xfffffff));
 {
  unsigned int row[4];
  row[0]=row[1]=row[2]=row[3]=0;
  gt4_gfx_batch_unpack(s, 0, 0x7C, aligned, 1, 0, 4, 0x5f5f5f5f, row, 0);
 }
 s->color=0x5f5f5f5f;
 if(normals) {
  int n=(nverts+1)&~1;
  gt4_gfx_batch_unpack(s, 0x40, 0x64, n, 1, 4, 4, s->color, 0, (const void *)((u32)normals&0xfffffff));
 } else if(s->flags&0x10) lighting=1;
 if(colors) {
  gt4_gfx_batch_unpack(s, 0x80, 0x6E, aligned, 1, 4, 4, s->color, 0, (const void *)((u32)colors&0xfffffff));
 } else if(!lighting && first>0) {
  int color=normals ? s->colors[1] : s->colors[0];
  unsigned int row[4];
  row[0]=color&0xff; row[1]=(color>>8)&0xff; row[2]=(color>>16)&0xff; row[3]=(color>>24)&0xff;   /* PEXTLB, PEXTLH */
  gt4_gfx_batch_unpack(s, 0x80, 0x7C, aligned, 1, 0, 4, 0x55555555, row, 0);
  s->color=0x55555555;
 }
 if(s->flag) func_0049E480(s);
 int n=s->count;
 if(!s->mode || s->value!=0.0f) { --n; func_0049E548(s,0xc1,0.0f); s->mode=1; }
 if(s->a&s->b) { func_0049E5F8(s,0xd9); n-=5; }
 if(lighting) { if(first>0) func_0049C140(s); n+=60; }
 u32 flags=s->flags|4;
 int total=count*3;
 /* the strip count and lengths (S-8 into lane w of 0x40 on, mask 3F3F3F3F), the output tag (V3-32 at 0xC0), the run */
 unsigned char *strips=(unsigned char *)__builtin_alloca(count+4);
 strips[0]=count;
 for(int i=0;i<count;i++) strips[1+i]=3;
 gt4_gfx_batch_unpack(s, 0x40, 0x72, count+1, 1, 4, 4, 0x3f3f3f3f, 0, strips);
 s->color=0x3f3f3f3f;
 u32 tag[3];
 tag[0]=total|0x8000;
 tag[1]=(flags<<15)|0x30004000;
 tag[2]=0x412;
 gt4_gfx_batch_unpack(s, 0xC0, 0x68, 1, 1, 4, 4, s->color, 0, tag);
 gt4_gfx_batch_run(s, (u32)n&0xffff, (u32)s->command&0x3ff);
}
