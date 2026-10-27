/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 004961B8 (unmatched; from the decomp's draft, corrected against the original): projects
 * the point (x, y, z) with the VU0 matrix in vf24-vf27, clip-tests it, and when visible builds a 16-qword
 * GIF packet (func_004A5B90) of two screen-space quads around it sized by `size`, clamped against the
 * scratchpad viewport at 0x70002FA0/0x70002FB0; returns 1 if drawn, 0 if clipped.
 * The draft scaled the last half-width by 1.6f (0x3FCCCCCD); the original loads 0x3FCCCCCC (lui 0x3FCC at 0x496438 /
 * ori 0xCCCC before `mul.s $f1, $f1, $f0`, the store to +0x80), one ulp lower. */
#include "spr.h"
typedef unsigned int U __attribute__((mode(TI)));
extern "C" U *func_004A5B90(int);
static inline unsigned bits(float f) { union { float f; unsigned i; } u; u.f=f; return u.i; }
static inline unsigned long pair(float x,float y) { unsigned long v;  __asm__("pextlw %0, %1, %2":"=r"(v):"r"(y),"r"(x));return v; }
extern "C" int func_004961B8(float x,float y,float z,float size) {

unsigned long a,b; U v;
  __asm__("pextlw %0, %1, %2":"=r"(a):"r"(z),"r"(x));
  __asm__("pextlw %0, %1, %2":"=r"(b):"r"(0x3f800000),"r"(y));
  __asm__("pextlw %0, %1, %2":"=r"(v):"r"(b),"r"(a));
__asm__ volatile("qmtc2.ni %0, $vf1"::"r"(v));
__asm__ volatile("vmulax.xyzw ACC, $vf24, $vf1x");
__asm__ volatile("vmadday.xyzw ACC, $vf25, $vf1y");
__asm__ volatile("vmaddaz.xyzw ACC, $vf26, $vf1z");
__asm__ volatile("vmaddw.xyzw $vf1, $vf27, $vf0w");
__asm__ volatile("vclipw.xyzw $vf1, $vf1w");
struct { int x,y,z,w; } clip;
__asm__ volatile("sqc2 $vf1, %0":"=m"(clip));
__asm__ volatile("vnop");
__asm__ volatile("vnop");
__asm__ volatile("vnop");
int sign=clip.w;
__asm__ volatile("vnop");
unsigned flags; __asm__ volatile("cfc2.i %0, $vi18":"=r"(flags));
if(sign<0) flags=((flags>>1)|0x2a)^((flags<<1)|0x15);
if(flags&63) return 0;
U *s=(U*)spr_base();
__asm__ volatile("lqc2 $vf3, %0"::"m"(s[0xfa0/16]));
__asm__ volatile("lqc2 $vf4, %0"::"m"(s[0xfb0/16]));
__asm__ volatile("vdiv        Q, $vf0w, $vf1w");
__asm__ volatile("vmini.xyzw  $vf1, $vf1, $vf22");
__asm__ volatile("vwaitq");
__asm__ volatile("vmulq.xyzw  $vf1, $vf1, Q");
__asm__ volatile("vmulaw.xyzw ACC, $vf21, $vf0w");
__asm__ volatile("vmadd.xyzw  $vf1, $vf1, $vf20");
U *p=func_004A5B90(16);
unsigned *w=(unsigned*)p;
w[0]=2;w[1]=0x10000000;w[2]=0xee;w[12]=0x8006;w[13]=0x20aa4000;w[14]=0x43;w[6]=63;w[8]=0x80808080;w[10]=1;w[3]=0;w[15]=0;w[4]=1;w[5]=0;w[7]=0;w[9]=0;w[11]=0;
__asm__ volatile("vsub.x      $vf2, $vf4, $vf3");
__asm__ volatile("vadd.y      $vf2, $vf4, $vf3");
float dx=0.58f,dy=0.5f; dx=size*dx; dy=size*dy;
__asm__ volatile("qmtc2.ni %0, $vf5"::"r"(pair(dx,dy)));
__asm__ volatile("vadd.xyzw   $vf6, $vf1, $vf5");
__asm__ volatile("vftoi4.xyzw $vf6, $vf6");
__asm__ volatile("sqc2 $vf6, 0(%0)"::"r"((char*)p+0x50));
__asm__ volatile("vsub.xyzw   $vf6, $vf1, $vf5");
__asm__ volatile("vmax.xy     $vf6, $vf6, $vf2");
__asm__ volatile("vftoi4.xyzw $vf6, $vf6");
__asm__ volatile("sqc2 $vf6, 0(%0)"::"r"((char*)p+0xf0));
dy=-dy;
__asm__ volatile("qmtc2.ni %0, $vf5"::"r"(pair(dx,dy)));
__asm__ volatile("vadd.xyzw   $vf6, $vf1, $vf5");
__asm__ volatile("vmaxx.y     $vf6, $vf6, $vf2x");
__asm__ volatile("vftoi4.xyzw $vf6, $vf6");
__asm__ volatile("sqc2 $vf6, 0(%0)"::"r"((char*)p+0xd0));
__asm__ volatile("vsub.xyzw   $vf6, $vf1, $vf5");
__asm__ volatile("vmax.x      $vf6, $vf6, $vf2");
__asm__ volatile("vftoi4.xyzw $vf6, $vf6");
__asm__ volatile("sqc2 $vf6, 0(%0)"::"r"((char*)p+0x70));
dx=dx+dx;
unsigned long flat;
  __asm__("pextlw %0, $0, %1":"=r"(flat):"r"(dx));
__asm__ volatile("qmtc2.ni %0, $vf5"::"r"(flat));
__asm__ volatile("vadd.xyzw   $vf6, $vf1, $vf5");
__asm__ volatile("vftoi4.xyzw $vf6, $vf6");
__asm__ volatile("sqc2 $vf6, 0(%0)"::"r"((char*)p+0x90));
__asm__ volatile("vsub.xyzw   $vf6, $vf1, $vf5");
__asm__ volatile("vmax.x      $vf6, $vf6, $vf2");
__asm__ volatile("vftoi4.xyzw $vf6, $vf6");
__asm__ volatile("sqc2 $vf6, 0(%0)"::"r"((char*)p+0xb0));
__asm__ volatile("vsub.xyzw   $vf1, $vf1, $vf4");
__asm__ volatile("vadd.x      $vf1, $vf1, $vf3");
__asm__ volatile("vsub.y      $vf1, $vf1, $vf3");
dx=0.58f;dy=0.45f;dx=size*dx;dy=size*dy;
__asm__ volatile("qmtc2.ni %0, $vf5"::"r"(pair(dx,dy)));
__asm__ volatile("vadd.xyzw   $vf6, $vf1, $vf5");
__asm__ volatile("vftoi4.xyzw $vf6, $vf6");
__asm__ volatile("sqc2 $vf6, 0(%0)"::"r"((char*)p+0x40));
__asm__ volatile("vsub.xyzw   $vf6, $vf1, $vf5");
__asm__ volatile("vmaxx.xy    $vf6, $vf6, $vf0x");
__asm__ volatile("vftoi4.xyzw $vf6, $vf6");
__asm__ volatile("sqc2 $vf6, 0(%0)"::"r"((char*)p+0xe0));
dy=-dy;
__asm__ volatile("qmtc2.ni %0, $vf5"::"r"(pair(dx,dy)));
__asm__ volatile("vadd.xyzw   $vf6, $vf1, $vf5");
__asm__ volatile("vmaxx.y     $vf6, $vf6, $vf0x");
__asm__ volatile("vftoi4.xyzw $vf6, $vf6");
__asm__ volatile("sqc2 $vf6, 0(%0)"::"r"((char*)p+0xc0));
__asm__ volatile("vsub.xyzw   $vf6, $vf1, $vf5");
__asm__ volatile("vmaxx.x     $vf6, $vf6, $vf0x");
__asm__ volatile("vftoi4.xyzw $vf6, $vf6");
__asm__ volatile("sqc2 $vf6, 0(%0)"::"r"((char*)p+0x60));
dx=dx*0x1.999998p+0f; /* 0x3FCCCCCC */
__asm__ volatile("qmtc2.ni %0, $vf5"::"r"(dx));
__asm__ volatile("vadd.xyzw   $vf6, $vf1, $vf5");
__asm__ volatile("vftoi4.xyzw $vf6, $vf6");
__asm__ volatile("sqc2 $vf6, 0(%0)"::"r"((char*)p+0x80));
__asm__ volatile("vsub.xyzw   $vf6, $vf1, $vf5");
__asm__ volatile("vmaxx.x     $vf6, $vf6, $vf0x");
__asm__ volatile("vftoi4.xyzw $vf6, $vf6");
__asm__ volatile("sqc2 $vf6, 0(%0)"::"r"((char*)p+0xa0));
return 1;
}
