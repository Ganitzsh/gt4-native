/* compiler: ee-gcc2.96-no-strict-aliasing */
struct E { float x,y; void set(float a,float b) { x=a; y=b; } void sx(float a) { x=a; } void sy(float a) { y=a; } float &X(){return x;} float &Y(){return y;} };
static inline float minval(float a,float b) { return a < b ? a : b; }
static inline float halfmin(float a,float b) { a=minval(a,b); a*=0.5f; return a; }
static inline void rect(E*p,float l,float t,float r,float b) { p[0].x=l; p[0].y=t; p[1].x=r; p[1].y=t; p[2].x=l; p[2].y=b; p[3].x=r; p[3].y=b; }
struct V { short delta,pad; void (*fn)(void*,int,E*,int,int); };
struct VC { short delta,pad; void (*fn)(void*,void*); };
struct S { int first; V *v; };
struct Face { char pad[0xA4]; void *color; char pad2[8]; float radius; };
extern "C" void mWidget__getWindowSize(void*,float*,float*);
extern "C" void func_00285690(S *, int, float, float, float, float, float) throw();
static inline void draw(S*s,E*p) { V *v=s->v+54; v->fn((char*)s+v->delta,4,p,0,0); }
extern "C" void func_002857C0(Face *self,S *ctx) {
 float w,h;
 E p[4];
 mWidget__getWindowSize(self,&w,&h);
 float radius=self->radius;
 float half=w<h?w:h;
 half*=0.5f;
 float r=minval(radius,half);
 int bx=1;
 VC *v=(VC*)(ctx->v+51);
 v->fn((char*)ctx+v->delta,self->color);
 if (!(w*0.5f<=r)) bx=0;
 int by=1;
 if (!(h*0.5f<=r)) by=0;
 if (bx && by) {
  func_00285690(ctx, 16, 0.0f, 6.283183574676514f, r, r, r);
 } else {
  func_00285690(ctx, 8, 0.0f, 1.570796f, r, w-r, h-r);
  func_00285690(ctx, 8, 1.570796f, 3.141591787338257f, r, r, h-r);
  func_00285690(ctx, 8, 3.141591787338257f, 4.712387561798096f, r, r, r);
  func_00285690(ctx, 8, 4.712387561798096f, 6.283183574676514f, r, w-r, r);
 }
 if (bx) {
  if (!by) {
   p[0].X()=0; p[0].Y()=r; p[1].X()=w; p[1].Y()=r; p[2].X()=0; p[2].Y()=h-r; p[3].X()=w; p[3].Y()=h-r;
   draw(ctx,p);
  }
 } else if (by) {
  p[0].X()=r; p[0].Y()=0; p[1].X()=w-r; p[1].Y()=0; p[2].X()=r; p[2].Y()=h; p[3].X()=w-r; p[3].Y()=h;
  draw(ctx,p);
 } else {
  float *x0=&p[0].x,*y0=&p[0].y,*x1=&p[1].x,*y1=&p[1].y;
  float *x2=&p[2].x,*y2=&p[2].y,*x3=&p[3].x,*y3=&p[3].y;
  *x0=r; *y0=0; *x1=w-r; *y1=0; *x2=r; *y2=h; *x3=w-r; *y3=h;
  draw(ctx,p);
  *x0=0; *y0=r; *x1=r; *y1=r; *x2=0; *y2=h-r; *x3=r; *y3=h-r;
  draw(ctx,p);
  *x0=w-r; *y0=r; *x1=w; *y1=r; *x2=w-r; *y2=h-r; *x3=w; *y3=h-r;
  draw(ctx,p);
 }
}
/* native correction of src/ui/mColorFace/func_002857C0.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00285690 */
