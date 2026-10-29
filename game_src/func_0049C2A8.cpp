/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049C2A8 (from its draft): its MMI colour expansion as four stores, without prefetch hints. */
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
 u32 *p=s->cursor;
 int aligned=(nverts+3)&~3;
 s->frame++;
 int qwc=(aligned*2)>>2;
 p[0]=qwc|0x30000000;
 p[1]=(u32)verts&0xfffffff;
 p[2]=0;
 p[3]=(aligned<<16)|0x6400c000;
 p+=4;
 p[0]=0x10000002; p[1]=0; p[2]=0x01000400; p[3]=0x30000000;
 p[4]=0; p[5]=0; p[6]=0; p[7]=0;
 p[8]=0x20000000; p[9]=0x5f5f5f5f;
 p[10]=(aligned<<16)|0x7c00c000; p[11]=0x01000404;
 p+=12;
 s->color=0x5f5f5f5f;
 if(normals) {
  int n=(nverts+1)&~1;
  qwc=n>>1;
  p[0]=qwc|0x30000000; p[1]=(u32)normals&0xfffffff;
  p[2]=0; p[3]=(n<<16)|0x6400c040;
  p+=4;
 } else if(s->flags&0x10) lighting=1;
 if(colors) {
  qwc=aligned>>2;
  p[0]=qwc|0x30000000; p[1]=(u32)colors&0xfffffff;
  p[2]=0; p[3]=(aligned<<16)|0x6e00c080;
  p+=4;
 } else if(!lighting && first>0) {
  int color=normals ? s->colors[1] : s->colors[0];
  p[0]=0x10000002; p[1]=0; p[2]=0x01000400; p[3]=0x30000000;
  {p[4]=color&0xff; p[5]=(color>>8)&0xff; p[6]=(color>>16)&0xff; p[7]=(color>>24)&0xff;}   /* the colour's bytes as four words (PEXTLB, PEXTLH) */
  p[8]=0x20000000; p[9]=0x55555555;
  p[10]=(aligned<<16)|0x7c00c080; p[11]=0x01000404;
  p+=12;
  s->color=0x55555555;
 }
 s->cursor=p;
 if(s->flag) func_0049E480(s);
 int n=s->count;
 if(!s->mode || s->value!=0.0f) { --n; func_0049E548(s,0xc1,0.0f); s->mode=1; }
 if(s->a&s->b) { func_0049E5F8(s,0xd9); n-=5; }
 if(lighting) { if(first>0) func_0049C140(s); n+=60; }
 u32 flags=s->flags|4;
 int total=count*3;
 u32 *out=s->cursor; u32 *start=out;
 ++out; *out++=0;
 if(s->color!=0x3f3f3f3f) { *out++=0x20000000; *out++=0x3f3f3f3f; s->color=0x3f3f3f3f; }
 *out++=((count+1)<<16)|0x7200c040;
 unsigned char *q=(unsigned char *)out;
 *q++=count;
 for(int i=0;i<count;i++) *q++=3;
 out=(u32 *)(((u32)q+3)&~3);
 *out++=0x6801c0c0;
 *out++=total|0x8000;
 *out++=(flags<<15)|0x30004000;
 *out++=0x412;
 *out++=s->command|0x04000000;
 *out++=n|0x14000000;
 while((u32)out&15) *out++=0;
 *start=(((u32)((char *)out-(char *)start)>>4)-1)|0x10000000;
 s->cursor=out;
}
