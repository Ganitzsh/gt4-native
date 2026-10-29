/* compiler: ee-gcc2.96-nogcse */
/* Native version of 0049C788 (from its draft): its MMI colour expansion as four stores, without prefetch hints. */
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
 u32 *p=(u32*)s.cur;
 int aligned=~3&(n+3);
 ++s.count;
 int words=(aligned*2)>>2;
 p[0]=words|0x30000000;
 p[1]=a&0xfffffff;
 int high=aligned<<16;
 p[3]=high|0x6400c000;
 p[2]=0;
 p+=4;
 p[0]=0x10000002;
 p[1]=0;
 p[2]=0x01000400;
 p[3]=0x30000000;
 p[4]=0; p[5]=0; p[6]=0; p[7]=0;
 p[8]=0x20000000;
 p[9]=0x5f5f5f5f;
 p[10]=high|0x7c00c000;
 p[11]=0x01000404;
 p+=12;
 s.color=0x5f5f5f5f;
 if(c) {
  int z=(n+1)&~1;
  words=z>>1;
  p[0]=words|0x30000000;
  p[1]=c&0xfffffff;
  p[3]=(z<<16)|0x6400c040;
  p[2]=0;
  p+=4;
 } else { special=0; if(s.flags&0x10) special=1; }
 if(b) {
  words=aligned>>2;
  p[0]=words|0x30000000;
  p[1]=b&0xfffffff;
  p[3]=high|0x6e00c080;
  p[2]=0;
  p+=4;
 } else if(!special && k>0) {
  u32 color=c?s.c1:s.c0;
  p[0]=0x10000002;
  p[2]=0x01000400;
  p[3]=0x30000000;
  p[1]=0;
  {p[4]=color&0xff; p[5]=(color>>8)&0xff; p[6]=(color>>16)&0xff; p[7]=(color>>24)&0xff;}   /* the colour's bytes as four words (PEXTLB, PEXTLH) */
  p[8]=0x20000000;
  p[9]=0x55555555;
  p[10]=high|0x7c00c080;
  p[11]=0x01000404;
  p+=12;
  s.color=0x55555555;
 }
 s.cur=(u32)p;
 if(s.enabled) func_0049E480(&s);
 int count=s.n;
 if(!s.alpha_set || s.alpha!=0.0f) { --count; func_0049E548(&s,0xc1,0.0f); s.alpha_set=1; }
 if(s.dirty&s.mask) { func_0049E5F8(&s,0xd9); count-=5; }
 if(special) { if(k>0) func_0049C140(&s); count+=60; }
 u32 flags=s.flags|4;
 u32 *start=(u32*)s.cur;
 u32 *q=start;
 ++q; *q++=0;
 if(s.color!=0x3f3f3f3f) { *q++=0x20000000; *q++=0x3f3f3f3f; s.color=0x3f3f3f3f; }
 *q++=0x7202c040;
 *q++=(((n*3)-6)<<8)|1;
 *q++=0x6801c0c0;
 *q++=n|0x8000;
 *q++=(flags<<15)|0x30004000;
 *q++=0x412;
 *q++=s.mode|0x04000000;
 *q=count|0x14000000;
 while((u32)++q&15) *q=0;
 *start=((((u32)q-(u32)start)>>4)-1)|0x10000000;
 s.cur=(u32)q;
}
