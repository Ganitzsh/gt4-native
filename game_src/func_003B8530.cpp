/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft build/swarm/v1/003b8530_best.cpp (argument order, argcheck.py orders): its matched caller RaceCarModel__virtual_35 passes (self, light, float alpha, int flag); the draft's order is the same EE function but natively read alpha and flag from each other's stack slots. */
typedef unsigned int U128 __attribute__((mode(TI)));
struct Vec { float x,y,z,w; };
struct V3 { float x,y,z; };
struct Light { Vec dir; char pad[16]; Vec normal; Vec pos; float width,height; V3 origin; float r,g,b; unsigned power; };
struct Entry { V3 pos; Light *light; int flag; float a,dx,dy,n; };
struct Param { float r,g,b,sx,sy,a,p18,c,b1,a2,p28,p2c,blink,phase,p38; };
struct Self { char pad[0xa30]; int type,count; Entry entries[100]; char gap[0x21f0-0x1848]; Param param; };
extern "C" {
int func_003B7C98(Self *,Vec *,Vec *,V3 *,float,float) throw();
float func_003B8460(Self *,Vec *,V3 *,float) throw();
unsigned func_003B7EE8(float,Self *,Vec *,float) throw();
}
static inline void transform(Vec *out, const Vec *v) {
 asm volatile("vnop");
 asm volatile("qmtc2.ni %0, $vf1"::"r"(*(const U128 *)v));
 asm volatile("vmulax.xyzw ACC, $vf28, $vf1x");
 asm volatile("vmadday.xyzw ACC, $vf29, $vf1y");
 asm volatile("vmaddaz.xyzw ACC, $vf30, $vf1z");
 asm volatile("vmaddw.xyzw $vf1, $vf31, $vf1w");
 U128 r; asm volatile("qmfc2.ni %0, $vf1":"=r"(r));
 *(U128 *)out=r;
}
static inline void transform3(V3 *out, const V3 *v,int point) {
 asm volatile("vnop");
 asm volatile("qmtc2.ni %0, $vf1"::"r"(*(const U128 *)v));
 asm volatile("vmulax.xyzw ACC, $vf28, $vf1x");
 asm volatile("vmadday.xyzw ACC, $vf29, $vf1y");
 if(point) {
 asm volatile("vmaddaz.xyzw ACC, $vf30, $vf1z");
 asm volatile("vmaddw.xyzw $vf1, $vf31, $vf0w");
 } else asm volatile("vmaddz.xyzw $vf1, $vf30, $vf1z");
 U128 r; asm volatile("qmfc2.ni %0, $vf1":"=r"(r));
 union { U128 r; V3 v; } u;u.r=r; *out=u.v;
}
static inline float rsqrt(float a,float b) { float r;asm("rsqrt.s %0, %1, %2":"=f"(r):"f"(a),"f"(b));return r; }
static inline int near(float a,float b) { float d=a-b;if(d<0)d=-d;return d<0.01f; }
static inline float length2(const volatile Vec &v) { return v.x*v.x+v.y*v.y; }
extern "C" void func_003B8530(Self *s,Light *l,float alpha,int flag) {
 Vec p,q,t;
 int count=s->count;
 if(count>=100)return;
 float w=l->width,h=l->height;
 Param *pm=&s->param;
 Entry *e=&s->entries[count];
 float radius=pm->sy;
 if(w<h)radius*=h;else radius*=w;
 if(!func_003B7C98(s,&p,&q,&l->origin,radius,pm->p28))return;
 w=l->width;h=l->height;
 if(!near(w,h)) {
 flag=0; transform(&t,&l->dir);
 float len=length2(t);
 if(len==0.0f) { e->dx=w;e->dy=0.0f; }
 else { float k=(w-h)+rsqrt(h,len); e->dy=t.y*k;e->dx=t.x*k; }
 } else {e->dy=1.0f;e->dx=1.0f;}
 transform3((V3 *)&t,(V3 *)&l->normal,0);
 float strength=-1.0f;
 if(flag)strength=func_003B8460(s,&p,(V3 *)&t,(float)l->power*pm->p38);
 if(strength<0.0f)e->flag=0;
 else { int f=1;if(!s->type)f=func_003B7EE8(1.0f,s,&q,pm->p2c);e->flag=f; }
 transform3(&e->pos,(V3 *)&l->pos,1);
 e->light=l;e->a=strength*alpha;e->n=t.z*-0.5f;s->count++;
}
