/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 00415AF8 (unmatched; drafts/00415af8/00415af8_3494d42e2b2c0fff.cpp, corrected for the
 * native calling convention): takes a point on obj's three-point path (00414E78), projects it against
 * obj's position and direction (00415828, 00415978) and, when it is visible, writes obj's direction
 * scaled by how close the angle is to it (and by t) to *out.
 * 00414E78 is V (S *s) with its result through a hidden pointer: it reads s's parameter at +0x130 and
 * mixes the vectors at +0x60/+0x70/+0x80. The draft called it with the result pointer only; the
 * original's jal at 0x415B38 sets only $a0 (&v, $s0 = sp+0x20) and leaves $a1 = its own obj argument
 * (copied to $s1 at 0x415B08, $a1 itself unchanged). Natively s was the stack word after &v, garbage. */
typedef unsigned int u128 __attribute__((mode(TI)));
struct __attribute__((aligned(16))) Vec { float x,y,z,w; Vec() {} Vec(const Vec &v) {*(u128*)this=*(const u128*)&v;} Vec &operator=(const Vec &v) {*(u128*)this=*(const u128*)&v; return *this;} Vec &operator*=(float f) { x*=f; y*=f; z*=f; return *this; } };
extern "C" float func_00420F00(float*,float);
struct __attribute__((aligned(16))) Angle { float value; Angle(float f): value(f) {} float normalize() { value=func_00420F00(&value,-3.14159265358979323846f); return value; } };
struct Obj { char pad[0x50]; Vec position; char gap[0x50]; Vec direction; char gap2[0x20]; float height; };
struct Map { float scale,bias; Map(float lo,float hi,float outlo,float outhi) { scale=(outhi-outlo)/(hi-lo); bias=scale*(hi*outlo-lo*outhi); } float get(float x) { return scale*x+bias; } };
static inline float maxf(const float &a,const float &b) {float r; __asm__("max.s %0, %1, %2":"=f"(r):"f"(a),"f"(b));return r;}
static inline float minf(const float &a,const float &b) {float r; __asm__("min.s %0, %1, %2":"=f"(r):"f"(a),"f"(b));return r;}
struct Clamp { float lo,hi; float get(const float &x) const { return minf(maxf(x,lo),hi);} };
extern "C" void func_00414E78(Vec*,Obj*) throw();
extern "C" int func_00415828(float*,float*,float,float,float,float,float,float,float,float,float) throw();
extern "C" int func_00415978(float*,float*,float,float,float) throw();
extern "C" float func_00415A68(float,float,float) throw();
inline Vec scale(const Vec &source,float f) { Vec v(source); v*=f; return v; }
static inline int project(float *a,float *b,const Vec &p,const Vec &d,const Vec &c) { return func_00415828(a,b,p.x,p.y,p.z,d.x,d.y,d.z,c.x,c.y,c.z); }
extern "C" int func_00415AF8(Vec *out, Obj *obj,float angle,float t) {
 float a,b; Vec v; float x,y;
 func_00414E78(&v,obj);
 int r=project(&a,&b,obj->position,obj->direction,v);
 if(!r) return r;
 {
  r=func_00415978(&x,&y,obj->height,a,b);
  if(!r) return r;
  {
   Angle delta(angle-x);
   float difference=delta.normalize();
   Map map(y*0.8f,y*1.2f,0.0f,1.0f);
   float amount=func_00415A68(t,0.5f,0.1f);
   Clamp clamp;
   clamp.lo=0.0f;
   clamp.hi=1.0f;
   difference=__builtin_fabsf(difference);
   *out=scale(obj->direction,clamp.get(map.get(difference))*amount);
   r=1;
  }
 }
 return r;
}
