/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/00416040/00416040_f3b6405476ef7fdf.cpp (argument order, argcheck.py orders): func_0057D8A0 (sincos) takes (float *sine, float *cosine, float x); the draft declared the float first, which on the EE is the same call ($f12, $a0, $a1) but natively put x in the pointers' stack slots. */
typedef unsigned int u128 __attribute__((mode(TI)));
struct V {
 float x,y,z,w;
 V() {}
 V(float a,float b,float c) {x=a;y=b;z=c;}
 V(const V &v) {*(u128*)this=*(const u128*)&v;}
 V &operator=(const V &v) {*(u128*)this=*(const u128*)&v;return *this;}
} __attribute__((aligned(16)));
struct Angle { float v;Angle() {} void set(float x){v=x;} void zero(){*(int *)&v=0;} } __attribute__((aligned(16)));
struct O {int side;char pad[0x70];int mode;};
struct S;
extern "C" void func_00415148(V *,S *,const Angle *,float,float) throw();
extern "C" void func_004150A0(V *,S *,int,float,float) throw();
extern "C" void func_0057D8A0(float *,float *,float);
static inline float maxf(float a,float b){float r;__asm__("max.s %0, %1, %2":"=f"(r):"f"(a),"f"(b));return r;}
static inline float minf(float a,float b){float r;__asm__("min.s %0, %1, %2":"=f"(r):"f"(a),"f"(b));return r;}
static inline float clamp(const float &v,const float &lo,const float &hi){return minf(maxf(v,lo),hi);}
static inline float affine(float x,const float &a,const float &b){return x*a+b;}
static inline float map(float x,const float &a,const float &b,const float &lo,const float &hi){return clamp(x*a+b,lo,hi);}
static inline void set(V *const v,float x,float y,float z) {v->x=x;v->y=y;v->z=z;}
extern "C" void func_00416040(O *o,V *out,V *up,const float *angle,S *s){
 V v;
 Angle zero1,zero2;
 struct {float a,b,lo,hi;} t;
 Angle zero3;
 switch(o->mode){
 case 1:{
  zero1.zero();
  float x=-1.0f;
  if(o->side) x=1.0f;
  func_00415148(&v,s,&zero1,x,0.0f);
  *out=v;
  set(&v,0.0f,1.0f,0.0f);
  *up=v;
  break;
 }
 case 2:{
  set(&v,0.0f,-1.0f,0.0f);
  *out=v;
  zero2.set(0.0f);
  float x;
  if(!o->side) x=-1.0f; else x=1.0f;
  func_00415148(&v,s,&zero2,x,0.0f);
  *up=v;
  break;
 }
 default:{

  float a=*angle;
  if(o->side==0 && a>-0x1.921fb4p-1f){
   t.lo=0.0f;t.hi=0x1.657182p-4f;
   a+=map(a,0x1.c71c6cp-4f,0x1.65717ep-4f,t.lo,t.hi);
  }else if(o->side==1 && a<0x1.921fb4p-1f){
   t.lo=0.0f;t.hi=-0x1.657182p-4f;
   a+=map(a,0x1.c71c6cp-4f,-0x1.65717ep-4f,t.hi,t.lo);
  }
  func_0057D8A0(&t.a,&t.b,a);
  func_004150A0(&v,s,o->side,t.a,t.b);
  *out=v;
  zero3.zero();
  func_00415148(&v,s,&zero3,t.a,t.b);
  *up=v;
  break;
 }
 }
}
