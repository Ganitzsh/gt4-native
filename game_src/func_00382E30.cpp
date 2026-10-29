/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of the provisional draft for 00382E30: the car object's view getter (slot 10) gets unused and input, which the EE forwards to it in $a2 and $a3 ($a1 then holds the object) (passthrough_lint) */
typedef float f32;
struct V { float x,y,z,w; };
struct E { short delta,index; void *(*fn)(void *); };
struct Virtual { char p[8]; E *vt; };
struct Car { char p[0x18]; void *data; char q[0x2864]; Virtual *obj; };
struct Cars { int n; int pad; Car **list; };
struct World { char pad[0x60]; Cars *cars; };
struct Input { char p[12]; short index; };
struct View { char p[0x44]; float *pos; };
struct S { char pad[0x28]; float x,y,z,ry,rx,rz,px,py,pz,ay,ax; char p2[0x12c]; Car *car; int tick; float a[3],b[3],c[3],d[3]; int target; float angle; float smooth[3]; char p3[0x120]; float result; };
extern "C" {
float func_00344798(void *,float); float func_003447F8(void *,float); float func_00344828(void *,float);
float func_00344858(void *,float); float func_00344888(void *,float); float func_003448B8(void *,float);
float func_00345198(void *); float func_003451A8(void *);
float func_0036EA98(void *) throw(); float func_0036EB98(float,float) throw(); float func_0036EAD8(float) throw();
void func_00384EE8(void *,int,int,float,float);
void func_00384A70(void *,int,float,float); void func_00384B70(void *,float);
float func_00384B28(float *); float func_00384BC8(float *);
void func_003FC698(void *,void *); void func_003FC618(void *,void *);
float func_005F5010(void *); void func_005F5040(void *,void *); void func_005F5268(void *,void *);
void func_004A0690(); void func_004A06F0(); void func_004A53F8(); void func_004A5400(); int func_004A7454(); void func_004A7550(void *);
void func_004A7844(float,float,float); void func_004A7988(float); void func_004A79B0(float); void func_004A79D8(float);
}
static inline void *view(Virtual *p,void *a2,void *a3) { E *e=p->vt+10; return ((void *(*)(void *,Virtual *,void *,void *))e->fn)((char *)p+e->delta,p,a2,a3); }
static inline float root(float x) { float y; __asm__("sqrt.s %0, %1":"=f"(y):"f"(x)); return y; }
static inline void scale(V *v,float k) { float *a=&v->x; float *b=&v->y; float *c=&v->z; *a*=k; *b*=k; *c*=k; }
static inline void position(V *v,void *p) { float y; float x; x=func_00344798(p,0.0f); y=func_003447F8(p,0.0f); float z=func_00344828(p,0.0f); v->x=x; v->y=y; v->z=z; }
static inline float clamp(float f) { if(f<=0.0f) f=0.0f; if(f>=493.82712f) f=493.82712f; return f; }
static inline void update(float *p,int t,float a) { func_00384A70(p,t,a,-1.0f); }
extern "C" int func_00382E30(S *s,World *world,void *unused,Input *input) {
 V pos,nearest,xyz,delta,selected;
 float distance=100.0f;
 View *v;
 int best=-1;
 void *data=s->car->data;
 v=(View *)view(s->car->obj,unused,input);
 func_003FC698(input,&pos); scale(&pos,60.0f);
 float speed=func_005F5010(&pos);
 func_003FC618(input,&pos);
 if(s->target<0) {
  int count=world->cars->n;
  for(int i=0;i<count;i++) {
   if(i!=input->index) {
    void *p=world->cars->list[i]->data;
    V *diff=&delta; V *point=&xyz; position(point,p); func_005F5040(point,diff); func_005F5268(diff,&pos);
    if(root(func_005F5010(diff))<distance) { distance=root(func_005F5010(diff)); best=i; func_005F5040(point,&nearest); }
   }
  }
  if(best>=0 && distance<20.0f) s->target=best;
 }
 if(s->target>=0) {
  void *p=world->cars->list[s->target]->data;
  V *diff=&selected; V *point=&xyz; position(point,p); func_005F5040(point,diff); func_005F5268(diff,&pos);
  distance=root(func_005F5010(diff));
  if(distance>30.0f) s->target=-1;
  else { float yaw=-func_003448B8(data,0.0f)*0x1.ca5dcp+5f; s->angle=func_0036EAD8(func_0036EB98(func_0036EA98(diff),yaw)); }
 }
 float desired=0.0f; if(s->target>=0) desired=s->angle;
 func_00384EE8(s->smooth,s->tick,1,desired,0x1.999998p-3f);
 s->result=s->smooth[0];
 int tick=s->tick;
 float *c=s->c; float *d=s->d;
 func_00384B70(c,func_00345198(data)*0.5f); func_00384B70(d,func_003451A8(data)*2.0f);
 float normalized=clamp(speed-0x1.edd3bep+2f); normalized/=493.82712f;
 
 update(s->a,tick,normalized); update(s->b,tick,normalized);
 float roll; float pitch; float yaw; float degrees; yaw=func_003448B8(data,0.0f); pitch=func_00344858(data,0.0f); roll=func_00344888(data,0.0f);
 float x=-func_00344798(data,0.0f); float y=0x1.999998p-3f-func_003447F8(data,0.0f); float z=-func_00344828(data,0.0f);
 pitch=-pitch;
 func_004A0690(); func_004A53F8(); func_004A7454();
 float *base=v->pos; degrees=0x1.ca5dcp+5f;
 yaw=-yaw;
 func_00384BC8(c); roll*=degrees; func_00384BC8(d);
 float ax=func_00384B28(s->a)*0.7f; float ay=func_00384B28(s->b)*0.7f;
 pitch*=degrees; yaw*=degrees; ay+=s->result;
 func_004A7988(ax); func_004A79B0(ay); func_004A7844(base[0],base[1],base[2]);
 s->ay=ay; s->ax=ax; s->px=base[0]; s->py=base[1]; s->pz=base[2];
 func_004A79D8(roll); func_004A7988(pitch); func_004A79B0(yaw); func_004A7844(x,y,z);
 func_004A7550((char *)s+0x100); func_004A5400(); func_004A06F0();
 s->ry=yaw; s->rx=pitch; s->rz=roll; s->x=x; s->y=y; s->z=z;
 return 1;
}
