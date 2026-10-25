/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft build/swarm/o2/004a9470_best.cpp (argument widths, argcheck.py width/retwidth): func_004A9108 takes (B *, long, long) and func_004A90A8 (Packet *, u64, u64, u64): GIF tag fields, 64-bit natively; the draft declared them int, which shifted the later arguments. */
#include "spr.h"
typedef unsigned int u32;
typedef unsigned long u64;
struct Packet { char *begin, *ptr; int n, headers, count; char pad[12]; };
struct State { char pad[0x1d8]; u64 flags; char pad2[24]; char *ptr; };
struct Pair { u64 data, reg; };
extern "C" {
void func_004A8FB0(Packet *, char *) throw();
void func_004A9060(Packet *, int) throw();
int func_004A9108(Packet *, long, u64) throw();
void func_004A90A8(Packet *, u64, u64, u64) throw();
int func_004A9170(Packet *, u64, u64) throw();
void func_004A9020(Packet *) throw();
void func_004A90F0(Packet *) throw();
char *func_004A91B8(Packet *) throw();
}
extern "C" void func_004A9470(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f,u32 g,u64 scissor) {
 Packet packet;
 a>>=6;b>>=11;c>>=11;d>>=11;e>>=11;f>>=6;g>>=6;
 State *st=(State *)spr_base();
 char *base=st->ptr;
 u32 *p=(u32*)base;
 p[0]=0x10000014;p[1]=0;p[2]=0x06008000;p[3]=0;
 p[4]=0;p[5]=0;p[6]=0x13000000;p[7]=0x51000013;
 u64 *q=(u64*)(base+0x20);
 q[0]=0x1000000000008012UL; q[1]=0xe;
 q[2]=0x0fff00000fff0000UL; q[3]=0x40;
 q[4]=scissor; q[5]=0x41;
 q[6]=(e&0x1ff)|0x00ffffff00010000UL; q[7]=0x4c;
 q[8]=0x0000800000008000UL; q[9]=0x18;
 q[10]=0x00003f8000000000UL; q[11]=0x19;
 q[12]=0x0000008000000068UL; q[13]=0x42;
 q[14]=1; q[15]=0x1a;
 q[16]=0; q[17]=9;
 q[18]=1; q[19]=0x14;
 q[20]=1; q[21]=0x15;
 q[22]=0x31002; q[23]=0x47;
 q[24]=0x31002; q[25]=0x48;
 q[26]=0x101000000UL; q[27]=0x4e;
 q[28]=0x101000000UL; q[29]=0x4f;
 q[30]=0; q[31]=0x49;
 q[32]=0; q[33]=0x4a;
 q[34]=0; q[35]=0x4b;
 q[36]=0x80000000UL; q[37]=1;
 char *start=(char*)(q+38);
 st->ptr=start;
 func_004A8FB0(&packet,base+0x1c0);
 u32 *meta=(u32*)(base+0x160);
 u64 tex=(u64)f<<37 | 0xa000000e40000000UL;
 u64 tex2=(u64)g | 0xa000000d4d304000UL;
 func_004A9060(&packet,0x116);
 int tex_index=func_004A9108(&packet,6,tex);
 func_004A9108(&packet,8,0);
 func_004A90A8(&packet,4,0x53d3,32);
 for(int i=0;i<8;i++) {
  int x=((i>>2)<<24)|((i&3)<<8);
  int y=i<<8;
  func_004A9170(&packet,(u64)(u32)(y+0x0),(u64)(u32)(x+0x80008000));
  func_004A9170(&packet,(u64)(u32)(y+0x4000080),(u64)(u32)(x+0x81008080));
  func_004A9170(&packet,(u64)(u32)(y+0x100000),(u64)(u32)(x+0x80008080));
  func_004A9170(&packet,(u64)(u32)(y+0x4100080),(u64)(u32)(x+0x81008100));
 }
 for(int i=0;i<8;i++) {
  int x=((i>>2)<<24)|((i&3)<<8);
  int y=i<<8;
  func_004A9170(&packet,(u64)(u32)(y+0x80),(u64)(u32)(x+0x82008000));
  func_004A9170(&packet,(u64)(u32)(y+0x4000100),(u64)(u32)(x+0x83008080));
  func_004A9170(&packet,(u64)(u32)(y+0x100080),(u64)(u32)(x+0x82008080));
  func_004A9170(&packet,(u64)(u32)(y+0x4100100),(u64)(u32)(x+0x83008100));
 }
 func_004A9020(&packet);
 func_004A9108(&packet,8,0x3f73);
 func_004A90A8(&packet,4,0x53d3,16);
 for(int i=0;i<8;i++) {
  int x=((i>>2)<<24)|((i&3)<<8);
  int y=i<<8;
  func_004A9170(&packet,(u64)(u32)(y+0x200040),(u64)(u32)(x+0x84008000));
  func_004A9170(&packet,(u64)(u32)(y+0x42000c0),(u64)(u32)(x+0x85008080));
  func_004A9170(&packet,(u64)(u32)(y+0x300040),(u64)(u32)(x+0x84008080));
  func_004A9170(&packet,(u64)(u32)(y+0x43000c0),(u64)(u32)(x+0x85008100));
 }
 func_004A9060(&packet,0x316);
 int index0=func_004A9108(&packet,0x4d,0x00ffffff00000000UL);
 func_004A90A8(&packet,6,0x53d3f7,8);
 for(int i=0;i<8;i++) {
  int x=i<<7;
  u32 tile=((e+0)<<5)+(((i&1)|((i&2)<<1)|((i&4)>>1))<<2);
  func_004A9170(&packet,tex2|((u64)tile<<37),0);
  func_004A9170(&packet,0,(u64)(u32)(x+0x3f800000));
  func_004A9170(&packet,0x02000080,(u64)(u32)(x+0x41800080));
 }
 func_004A9060(&packet,0x316);
 int index1=func_004A9108(&packet,0x4d,0x00ffffff00000000UL);
 func_004A90A8(&packet,6,0x53d3f7,8);
 for(int i=0;i<8;i++) {
  int x=i<<7;
  u32 tile=((e+1)<<5)+(((i&1)|((i&2)<<1)|((i&4)>>1))<<2);
  func_004A9170(&packet,tex2|((u64)tile<<37),0);
  func_004A9170(&packet,0,(u64)(u32)(x+0x3f800000));
  func_004A9170(&packet,0x02000080,(u64)(u32)(x+0x41800080));
 }
 func_004A9060(&packet,0x316);
 int index2=func_004A9108(&packet,0x4d,0x00ffffff00000000UL);
 func_004A90A8(&packet,6,0x53d3f7,8);
 for(int i=0;i<8;i++) {
  int x=i<<7;
  u32 tile=((e+2)<<5)+(((i&1)|((i&2)<<1)|((i&4)>>1))<<2);
  func_004A9170(&packet,tex2|((u64)tile<<37),0);
  func_004A9170(&packet,0,(u64)(u32)(x+0x3f800000));
  func_004A9170(&packet,0x02000080,(u64)(u32)(x+0x41800080));
 }
 func_004A90F0(&packet);
 char *end=func_004A91B8(&packet);
 int size=(end-start)/16;
 st->ptr=end;
 ((u32*)end)[0]=0x150007d4;
 ((u32*)end)[1]=0x11000000;
 ((u32*)end)[2]=0;
 ((u32*)end)[3]=0x06000000;
 st->ptr=end+16;
 ((u32*)start)[0]=size|0x10000000;
 ((u32*)start)[1]=0;
 ((u32*)start)[2]=0;
 ((u32*)start)[3]=((packet.headers+packet.count+6)<<16)|0x6c00c000;
 int width=((scissor>>16&0xfff)>>6)-((scissor&0xfff)>>6)+1;
 int height=((scissor>>48&0xfff)>>5)-((scissor>>32&0xfff)>>5)+1;
 int area=width*height;
 meta[0]=6;
 meta[1]=packet.headers+6;
 meta[2]=(packet.count+1)/2;
 meta[3]=area&1;
 meta[4]=tex_index;
 meta[5]=index0;
 meta[6]=index2;
 meta[7]=index1;
 meta[8]=a|0xa9308000;
 meta[9]=0xa0000000;
 meta[10]=area-1;
 meta[12]=b|0x40000000;
 meta[13]=0x34800000;
 meta[16]=c|0x40000000;
 meta[17]=0x34800000;
 meta[20]=d|0x40000000;
 meta[21]=0x34800000;
 st->flags|=0x003007df07dfUL;
}
