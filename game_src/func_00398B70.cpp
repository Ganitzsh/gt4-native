/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/00398b70/00398b70_91fa3492baf9869b.cpp (argument order, argcheck.py orders): SePlayer__Play (00462A88) is (this, int, int, int, float, float, float) as defined; the draft declared the floats among the ints, the same EE call but other stack slots natively. */
struct V { short delta, index; void *fn; };
#define F(p,t,o) (*(t *)((char *)(p)+(o)))
static inline int vi(char *p,int o) { V *v=(V*)(F(p,char*,0)+o); return ((int(*)(char*))v->fn)(p+v->delta); }
static inline char *vp(char *p,int o) { V *v=(V*)(F(p,char*,0)+o); return ((char*(*)(char*))v->fn)(p+v->delta); }
static inline float vf(char *p,int o,float x) { V *v=(V*)(F(p,char*,0)+o); return ((float(*)(char*,float))v->fn)(p+v->delta,x); }
static inline float vfi(char *p,int o,int x) { V *v=(V*)(F(p,char*,0)+o); return ((float(*)(char*,int))v->fn)(p+v->delta,x); }
struct Vec {float x,y,z,w;} __attribute__((aligned(16)));
struct Mix {float a[16];} __attribute__((aligned(16)));
struct Search { char p[16]; int flags; char q[24]; short a,b; float x,y,z; char r[4]; } __attribute__((aligned(16)));
extern "C" {
void func_00372080(void*,int);
float func_00391CD8();
void *func_00396800(void*);
void func_00398B58(void*) throw();
void func_00399300(void*,int);
void func_003B6FD0(void*);
int func_003E5A30(void*,void*,void*);
void func_00457700(void*);
void func_00457738(void*,void*);
void func_00457F60(void*,void*,void*,void*,void*,void*);
void func_00462A88(void*,int,int,int,float,float,float);
void func_0055CD18(void*,int,int,int);
extern char D_006518C8[];
extern float D_006213E0;
}
static inline float mn(float a,float b) { float r; __asm__("min.s %0,%1,%2":"=f"(r):"f"(a),"f"(b)); return r; }
static inline float mx(float a,float b) { float r; __asm__("max.s %0,%1,%2":"=f"(r):"f"(a),"f"(b)); return r; }
static inline float clamp(float a) { return a < -1.0f ? -1.0f : (a > 1.0f ? 1.0f : a); }
static inline int imin(int a,int b) { if(a>b)return b;return a; }
static inline int vol(float a) { float b=a<0.0f?-a:a; if(b>=1.0f) return 0x7ffffffe; int i=(int)(b*0x1.fffffep30f); return imin(i,0x7ffffffe); }
extern "C" void func_00398B70(char *self,char *ctx) {
 char *course=F(F(ctx,char*,0x80),char*,4);
 char *group=F(course,char*,0x8c);
 if(group) {
  char *a=F(ctx,char*,0x84); V *v=(V*)(F(a,char*,0x64)+0xd0);
  char *car=((char*(*)(char*,int))v->fn)(a+v->delta,0);
  if(!vi(car,0x100)) {func_00399300(self,0);func_00398B58(self);return;}
  Mix mix; Vec pos,dir,vel;
  int kind=0; int n=F(car,int,0x9b8); void *runway=func_00396800(course);
  int i=0;float right=0.0f,left=0.0f;
  func_003B6FD0(F(ctx,void*,0x60));func_00457700(&mix);
  for(;i<n;i++) {
   Mix out; Search search;
   func_00372080(car,i);
   pos.x=vf(car,0x20,0.0f);pos.y=vf(car,0x28,0.0f);pos.z=vf(car,0x30,0.0f);pos.w=1.0f;
   char *mat=vp(car,0xf8);
   dir.x=F(mat,float,0);dir.y=F(mat,float,0x10);dir.z=F(mat,float,0x20);dir.w=1.0f;
   if(vi(car,0x100)) {
    float f=vfi(car,0xe0,1);vel.x=pos.x-f;
    f=vfi(car,0xe8,1);vel.y=pos.y-f;
    f=vfi(car,0xf0,1);vel.z=pos.z-f;vel.w=1.0f;
   } else {float z=0.0f;vel.x=z;vel.y=z;vel.z=z;vel.w=1.0f;}
   func_00457F60(group,&out,ctx,&pos,&dir,&vel);
   search.a=-1;search.x=pos.x;search.b=-1;search.y=pos.y+5.0f;search.z=pos.z;
   int ok=func_003E5A30(runway,&search,&pos);int flags=search.flags;if(!ok) flags=0;
   switch(flags&0x18) {case 0:break;case 24:kind=1;break;case 8:left=1.0f;kind=2;break;case 16:right=1.0f;kind=1;break;}
   func_00457738(&mix,&out);
  }
  if(n>=2)kind=0;
  float target; if(kind) target=kind*0.25f;else target=0.125f;
  float cur=F(self,float,0x16c);float diff=target-cur;float rate=0x1.c71c74p-6f;
  if(diff<0.0f)diff=mx(diff,-rate);else diff=mn(diff,rate);cur+=diff;
  F(self,float,0x16c)=cur;kind=(int)(cur*32767.0f);func_0055CD18(D_006518C8,2,kind,kind);
  char *p=F(group,char*,0x14);if(F(p,int,0)) {float x=F(F(p,char*,4),float,0x3c);mix.a[0]=x;mix.a[1]=x;mix.a[2]=1.0f;}
  char *p2=F(group,char*,0x18);if(F(p2,int,0)) {float x=F(F(p2,char*,4),float,0x3c);mix.a[3]=x;mix.a[4]=x;mix.a[5]=1.0f;}
  mix.a[14]=1.0f;mix.a[11]=1.0f;
  float master=D_006213E0*func_00391CD8();
  for(i=0;i<5;i++) {
   if(F(self,int,0x24+i*0x24)) {
    float l=clamp(mix.a[i*3])*master,r=clamp(mix.a[i*3+1])*master;
    float m=mx(l,r);float z=mix.a[i*3+2];if(n!=1){l=m;r=m;z=1.0f;}
    func_00462A88(self+4+i*0x24,vol(m),0,0x40,l,r,z);
   }
  }
  left*=master;right*=master;int vl=vol(left),vr=vol(right);
  func_00462A88(self+0xb8,vl,0,0x40,left,0.0f,1.0f);
  func_00462A88(self+0xdc,vl,0,0x40,0.0f,left,1.0f);
  func_00462A88(self+0x100,vr,0,0x40,0.0f,right,1.0f);
  func_00462A88(self+0x124,vr,0,0x40,right,0.0f,1.0f);
  float last=0.0f;if(vi(car,0x130)&1)last=1.0f;
  func_00462A88(self+0x148,vol(last),0,0x40,last,last,1.0f);
 }
}
