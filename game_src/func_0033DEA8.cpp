#include "types.h"
struct V { short d, i; void *fn; };
struct Buf { int v[15]; };
struct Global { Buf b[2]; int cur; };
extern Global D_00618588;
extern int D_006184F0;
#define P(p,o) (*(char **)((char *)(p)+(o)))
#define I(p,o) (*(int *)((char *)(p)+(o)))
#define F(p,o) (*(float *)((char *)(p)+(o)))
typedef char *(*Get)(char *, int);
typedef char *(*GetT)(char *, int, float);
typedef char *(*Ptr)(char *);
typedef int (*Test)(char *);
typedef float (*Float)(char *);
typedef float (*FloatT)(char *,float);
struct Vec3 { float x,y,z; };
class Node { public: virtual void m0(); virtual void m1(); virtual void m2(); virtual void m3(); virtual Vec3 position(float); };
struct Item { Node *node; };
typedef Vec3 (*Vec)(float,char *);
static inline V *entry(char *p,int off) { return (V *)(P(p,0)+off); }
static inline char *cameraT(char *p,float t) { V *v=(V *)(P(p,0x64)+0xD0); return ((GetT)v->fn)(p+v->d,0,t); }
static inline char *camera(char *p) { V *v=(V *)(P(p,0x64)+0xD0); return ((Get)v->fn)(p+v->d,0); }
static inline float vf(char *p,int off) { V *v=entry(p,off); return ((Float)v->fn)(p+v->d); }
static inline int odd(int t) { int flag=0; if(t==1 || t==3) flag=1; return flag; }
static inline int camtest(char *self) { char *c=camera(self); V *e=entry(c,0x140); return ((Test)e->fn)(c+e->d); }
static inline float getlimit(char *p) { return F(p,0x938); }
static inline float maximum(float a,float b) { if(a<b) return b; return a; }
extern "C" {
void func_00105280(void *,int);
int func_001052E0(void *,int,int,int);
void *func_00105338(void *,void *,int);
void func_001054A0(void *,int);
void func_001055C0(void *);
int GSBuffer__getBufferWidth(void *);
void *func_00105AD8(void *,void *,int,int);
void func_00330AE8(void *,void *,int);
void func_0033DC48() throw();
void func_0033E520(void *,void *,void *,void *,void *,float,float,float);
void func_0033E6E0(void *,void *,void *,void *,float,float,float);
void func_0033E828(void *,void *);
void func_0033E8E0(void *);
void func_0033E960(void *, void *, void *, Vec3 *, int, float);
void func_0033EAB8(void *,void *,void *,void *,float,int);
void func_0033F1C8(void *,void *,void *,void *,float);
void func_0033F2E8(void *,void *,void *,void *,float);
void func_00397E70(void *);
void func_004A1638(int);
void func_004AB040(int);
}
extern "C" void RacePS2Base__render_world(char *self, Buf *out,float t) {
 Buf b; Buf aux;
 char *cam=cameraT(self,t);
 char *scene=P(P(P(P(self,0x6C),0x80),4),0x80);
 func_0033E8E0(self);
 func_00105338(&b,&D_00618588.b[D_00618588.cur],0);
 int same=b.v[3]==out->v[3];
 if(same) func_001054A0(&b,out->v[10]);
 else func_001054A0(&b,I(&D_00618588,-0x9C)?9:0);
 int n=I(self,0xE330);
 Buf *cur=&D_00618588.b[D_00618588.cur];
 int mode;
 if(b.v[3]<449 || GSBuffer__getBufferWidth(&b)<577) mode=0; else mode=2;
 func_001052E0(&aux,mode,n,n);
 V *v=(V *)(P(cam,0)+0x160);
 char *p=P(((Ptr)v->fn)(cam+v->d),0x2880);
 if(p) {
   Vec3 vec=((Item *)p)->node->position(t);
   func_0033E960(self, &aux, cam, &vec, b.v[0], t);
 }
 V *v50=(V *)(P(cam,0)+0x50);
 float f=((FloatT)v50->fn)(cam+v50->d,t);
 float limit=maximum(f,getlimit(scene));
 func_0033EAB8(self,&b,cur,&aux,t,-1);
 if(same) {
  func_0033E828(self,&b);
  func_0033F1C8(self,&b,&b,cur,t);
  func_0033F2E8(self,&b,cur,&aux,t);
  func_0033DC48();
  V *v460=(V *)(P(self,0x64)+0x460);
  if(((Test)v460->fn)(self+v460->d)) {
   float x=vf(cam,0x68)*1000.0f;
   float y=vf(cam,0x58);
   func_0033E6E0(self,&b,cur,&b,x,y,limit);
  }
  func_004AB040(5); func_004A1638(7);
  func_00105AD8(out,&b,0,0); func_004AB040(7);
 } else {
  Buf tmp;
  func_00105338(&tmp,out,0x31);
  float x=vf(cam,0x68)*1000.0f;
  float y=vf(cam,0x58);
  func_0033E520(self,&b,cur,out,&tmp,x,y,limit);
  func_00397E70(P(P(P(P(self,0x6C),0x80),4),0x80));
  func_0033F1C8(self,&b,out,&tmp,t);
  func_001055C0(&b);
  func_0033F2E8(self,out,&tmp,&aux,t);
  func_00105280(&tmp,2);
 }
 if(I(self,0xE3D4)==0 || (((unsigned)(I(self,0xCC8)-2)<2 || odd(I(self,0xCC8))) && I(P(self,0x6C),0xDC)==0 && I(P(self,0x6C),0xF0)==0 && camtest(self))) func_00330AE8(self+0xE3AC,out,D_006184F0);
 func_00105280(&aux,2); func_00105280(&b,2);
}
/* native correction of src/race/RaceBGMPS2_RacePS2Base/RacePS2Base__render_world.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_0033E960 */
