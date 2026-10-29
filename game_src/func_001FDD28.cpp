/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 001FDD28 (from its draft, checked against the original): the MMI alpha expansion as word stores. */
typedef unsigned int u32;
struct S { char p[0x90]; int height,width; };

/* Two quads of the source give the low byte of each of their words as the alpha (top byte) of two pixels side by
   side, in four quads of pixels (PSLLW 24, PEXTLW/PEXTUW, PAND 0x00FFFFFF, POR). A source quad is read whole before
   its pixels are written, as on the PS2. */
static inline void expand(u32 *out, const u32 *src) {
 for(int k=0;k<2;k++) {
  u32 a=src[0]<<24,b=src[1]<<24,c=src[2]<<24,d=src[3]<<24;
  out[0]=(out[0]&0xffffff)|a; out[1]=(out[1]&0xffffff)|a; out[2]=(out[2]&0xffffff)|b; out[3]=(out[3]&0xffffff)|b;
  out[4]=(out[4]&0xffffff)|c; out[5]=(out[5]&0xffffff)|c; out[6]=(out[6]&0xffffff)|d; out[7]=(out[7]&0xffffff)|d;
  src+=4; out+=8;
 }
}

extern "C" void func_001FDD28(S *s,void *p) {
 u32 *out=(u32 *)p;
 int h=s->height,w=s->width;
 u32 *out2=(u32 *)((char *)out+(w<<6));
 const u32 *src=(const u32 *)((char *)p+(h/2*w*4));
 for(int i=h/64-1;i>=0;i--) {
  for(int j=s->width-1;j>=0;j--) {
   expand(out,src); out+=16; src+=8;
   expand(out2,src); out2+=16; src+=8;
  }
  out=out2;
  out2=(u32 *)((char *)out2+(s->width<<6));
 }
}
