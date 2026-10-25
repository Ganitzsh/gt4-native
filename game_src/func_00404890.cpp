/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/00404890/00404890_2f22aba1987a4152.cpp (argument order, argcheck.py orders): func_0057D8A0 (sincos) takes (float *sine, float *cosine, float x); the draft declared the float first, which on the EE is the same call ($f12, $a0, $a1) but natively put x in the pointers' stack slots. */
struct W { int a0,a4,a8; char padc[12]; int a18; char pad1c[0xe4]; float force; char pad104[12]; float dt,scale; int pad118,flag; };
extern "C" float func_00344888(int,float);
extern "C" float func_003856A8(int);
extern "C" float *func_00385AE0(int), *func_00385AF8(int), *func_00385C88(int), *func_00385C98(int), *func_003861C0(int), *func_003861D8(int), *func_00386348(int), *func_003864D8(int), *func_003864E8(int), *func_003864F8(int);
extern "C" float func_0057D118(float,float);
extern "C" void func_0057D8A0(float*,float*,float);
extern "C" float fabsf(float);
struct St { float acc; float *vel,*pos; };
static inline float minimum(float a,float b) { float r; asm("min.s %0,%1,%2":"=f"(r):"f"(a),"f"(b)); return r; }
static inline float maximum(float a,float b) { float r; asm("max.s %0,%1,%2":"=f"(r):"f"(a),"f"(b)); return r; }
static inline float root(float a) { float r; asm("sqrt.s %0,%1":"=f"(r):"f"(a)); return r; }
static inline void friction(St *s,float f) { float a=s->acc; if(a*a<f*f) s->acc=0.0f; else s->acc=a>0.0f?a-f:a+f; }
static inline void advance(St *s,float dt) { *s->pos+=*s->vel*dt; s->acc=0.0f; }
static inline void spring(St *s,float target,float dt,float k,float d,float vmax) {
 s->acc += (target-*s->pos)*k; s->acc -= *s->vel*d; *s->vel += s->acc*dt;
 float v=*s->vel; if(v*v>vmax*vmax) { if(v>0.0f) *s->vel=vmax; else *s->vel=-vmax; } advance(s,dt);
}
static inline void springCached(St *s,float *vel,float target,float dt,float k,float d,float vmax) {
 s->acc += (target-*s->pos)*k; s->acc -= *vel*d; *vel += s->acc*dt;
 float v=*vel; if(v*v>vmax*vmax) { if(v>0.0f) *vel=vmax; else *vel=-vmax; } advance(s,dt);
}
static inline void fix(St *s,float p) { s->acc=0.0f; *s->vel=0.0f; *s->pos=p; }
static inline float clamp(float *v,float lo,float hi) { return minimum(maximum(*v,lo),hi); }
struct Line { float a,b; };
static inline void init(Line *l,float a,float b) { l->a=a; l->b=b; }
static inline float curve(Line *l,float x,float *lo,float *hi) { return minimum(maximum(l->a*x+l->b,*lo),*hi); }
extern "C" int func_00404890(W *w,float limit) {
 int result=0; float *forcep=&w->force; float low=-limit; float z=0.0f;
 float angle=func_00344888(w->a18,z); float mag=__builtin_fabsf(angle);
 St s; float sn,cs,goal; St t; Line l1,l2;
 float *v=func_003864E8(w->a8); float *p=func_003864D8(w->a8);
 s.pos=p; s.vel=v; s.acc=z;
 func_0057D8A0(&sn,&cs,angle);
 float f=-*forcep; goal=func_0057D118(-(f*cs+sn*0x1.399998p+3f),-(f*sn+cs*-0x1.399998p+3f));
 float bounded=clamp(&goal,low,limit);
 spring(&s,bounded,w->dt,100.0f,20.0f,0x1.41b2f4p-2f);
 St *tp=&t; v=func_003864F8(w->a8); p=func_00385C98(w->a0); tp->acc=0.0f; t.vel=v; t.pos=p;
 springCached(tp,v,minimum(maximum(goal+angle,-0x1.657182p-2f),0x1.657182p-2f),w->dt,100.0f,20.0f,0x1.41b2f4p-2f);
 float damping=20.0f, stiffness=0x1.67fffep+6f,c=500.0f;
 float target=*func_003864D8(w->a8);
 v=func_003861D8(w->a8); p=func_00385AF8(w->a0); tp->acc=0.0f; t.vel=v; t.pos=p;
 v=func_003861C0(w->a8); p=func_00385AE0(w->a0); s.vel=v; s.pos=p; s.acc=0.0f;
 func_00385AF8(w->a0);
 float force=-*forcep; float sum=force+*func_00386348(w->a8); float cap=w->dt*1000.0f;
 if(cap<sum) { result=1; *func_00386348(w->a8)=sum-cap; force=cap; float *out=func_00386348(w->a8); *out=minimum(c,*func_00386348(w->a8)); }
 else if(sum < -cap) { force=-cap; result=1; *func_00386348(w->a8)=sum+cap; float *out=func_00386348(w->a8); *out=maximum(-c,*func_00386348(w->a8)); }
 else { force=sum; *func_00386348(w->a8)=0.0f; }
 tp->acc+=force; friction(tp,3.0f);
 if(w->flag) spring(&t,target,w->dt,stiffness*0.375f,damping*(root(0.375f)*1.05f),0x1.f6a7ap+4f);
 else spring(&t,target,w->dt,stiffness,damping,0x1.f6a7ap+4f);
 if(limit<*t.pos) fix(&t,limit); else if(*t.pos<low) fix(&t,low);
 s.acc+=force; friction(&s,3.0f); spring(&s,0.0f,w->dt,stiffness,damping,0x1.f6a7ap+4f);
 float slo=-0x1.657182p-4f, shi=0x1.657182p-4f;
 if(*s.pos>shi) fix(&s,shi); else if(*s.pos<slo) fix(&s,slo);
 init(&l1,0x1.976fc6p-1f,0x1.7ffffcp-2f); goal=0.375f; cs=1.0f;
 float desired=curve(&l1,mag,&goal,&cs);
 float speed=func_003856A8(w->a0); speed-=func_003856A8(w->a4); speed=fabsf(speed); speed*=w->scale;
 init(&l2,0x1.6eb166p+2f,0x1.fffffcp-2f); goal=0.5f; cs=5.0f;
 float rate=curve(&l2,speed,&goal,&cs);
 if(*func_00385C88(w->a0)<desired) { *func_00385C88(w->a0)+=rate*w->dt; if(desired<*func_00385C88(w->a0)) *func_00385C88(w->a0)=desired; }
 else { *func_00385C88(w->a0)-=rate*w->dt; if(*func_00385C88(w->a0)<desired) *func_00385C88(w->a0)=desired; }
 return result;
}
