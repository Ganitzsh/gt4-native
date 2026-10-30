/* compiler: ee-gcc2.96-nogcse */
/* Native version of 0049C788 (from its draft): its MMI colour expansion as four stores, without prefetch hints.
 * Natively the batch goes as batch records (compat/game/gt4_gfx.h) instead of DMA tags and VIF codes: the same
 * formats, lane masks and cycles, expanded by gt4_gfx_batch_unpack. */
#include "gt4_gfx.h"
#include "spr.h"
typedef unsigned int u32;
typedef unsigned long u64;
struct Spr {
 unsigned char enabled,pad1[2],alpha_set; char p4[0x10]; int n,mode; char p1c[4]; float alpha; char p24[0x1c]; u32 color; char p44[6]; unsigned short flags; char p4c[0x18c]; u64 dirty; char p1e0[8]; u64 mask; char p1f0[8]; u32 cur; char p1fc[0xa24]; u32 c0,c1; char pc28[0x400]; int count;
};
extern "C" void func_0049C140(Spr *) throw();
extern "C" void func_0049E480(Spr *);
extern "C" void func_0049E548(Spr *,int,float);
extern "C" void func_0049E5F8(Spr *,int);
extern "C" void func_0049C788(u32 a,u32 b,u32 c,int n,int k) {
 Spr &s=*(Spr*)spr_base();
 int special=0;
 int aligned=~3&(n+3);
 ++s.count;
 /* positions (V2-32), then their z and w zero (the row 0 through the mask 5F5F5F5F, a cycle with no data) */
 gt4_gfx_batch_unpack(&s, 0, 0x64, aligned, 1, 4, 4, s.color, 0, (const void *)(a&0xfffffff));
 {
  unsigned int row[4];
  row[0]=row[1]=row[2]=row[3]=0;
  gt4_gfx_batch_unpack(&s, 0, 0x7C, aligned, 1, 0, 4, 0x5f5f5f5f, row, 0);
 }
 s.color=0x5f5f5f5f;
 if(c) {
  int z=(n+1)&~1;
  gt4_gfx_batch_unpack(&s, 0x40, 0x64, z, 1, 4, 4, s.color, 0, (const void *)(c&0xfffffff));
 } else { special=0; if(s.flags&0x10) special=1; }
 if(b) {
  gt4_gfx_batch_unpack(&s, 0x80, 0x6E, aligned, 1, 4, 4, s.color, 0, (const void *)(b&0xfffffff));
 } else if(!special && k>0) {
  u32 color=c?s.c1:s.c0;
  unsigned int row[4];
  row[0]=color&0xff; row[1]=(color>>8)&0xff; row[2]=(color>>16)&0xff; row[3]=(color>>24)&0xff;   /* PEXTLB, PEXTLH */
  gt4_gfx_batch_unpack(&s, 0x80, 0x7C, aligned, 1, 0, 4, 0x55555555, row, 0);
  s.color=0x55555555;
 }
 if(s.enabled) func_0049E480(&s);
 int count=s.n;
 if(!s.alpha_set || s.alpha!=0.0f) { --count; func_0049E548(&s,0xc1,0.0f); s.alpha_set=1; }
 if(s.dirty&s.mask) { func_0049E5F8(&s,0xd9); count-=5; }
 if(special) { if(k>0) func_0049C140(&s); count+=60; }
 u32 flags=s.flags|4;
 /* the strip count and length (S-8 into lane w of 0x40, 0x41, mask 3F3F3F3F), the output tag (V3-32 at 0xC0), the run */
 u32 strips=(((n*3)-6)<<8)|1;
 gt4_gfx_batch_unpack(&s, 0x40, 0x72, 2, 1, 4, 4, 0x3f3f3f3f, 0, &strips);
 s.color=0x3f3f3f3f;
 u32 tag[3];
 tag[0]=n|0x8000;
 tag[1]=(flags<<15)|0x30004000;
 tag[2]=0x412;
 gt4_gfx_batch_unpack(&s, 0xC0, 0x68, 1, 1, 4, 4, s.color, 0, tag);
 gt4_gfx_batch_run(&s, (u32)count&0xffff, (u32)s.mode&0x3ff);
}
