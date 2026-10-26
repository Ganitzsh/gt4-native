/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/003ffd10/003ffd10_3e6d3680f7e497b9.cpp (argument order, argcheck.py orders): func_003FFA48 is (five ints, float) as defined; the draft put the float fourth, the same EE call but other stack slots natively. */
struct Item { int flags; float pos[3]; };
struct Record { Item *item; float pos[3], dir[3], f1c,f20,f24,f28,f2c,f30,zero[3],f40,f44; int a48,a4c,a50,a54; float f58; char pad[16]; unsigned int flags; int tail; };
struct Out { int world,count; Record *records; int x; void *extra; };
struct Scene { char pad[128]; int world; };
struct Inner { char pad[24]; int value; };
struct Input { char pad[16]; Inner *inner; };
extern "C" {
float func_0036EA50(float*); float func_0036EA98(float*); float func_0036EAD8(float); float func_0036EB98(float,float); float func_0036EBE8(float,float,float);
void func_003FC618(Input*,float*) throw();
int func_003FDC50(Scene*,Input*,Item**,int) throw();
float func_003FE358(int,float*,int*) throw();
void func_003FF9A0(Out*) throw();
int func_003FFA48(int,int,void*,float*,float*,float) throw();
void func_003FFC68(float*,float*,float) throw();
void *func_00575DC8(int) throw();
float func_00578598(void*);
void *func_005A48D8(void*,int,unsigned int);
float func_005F5010(float*); void func_005F5040(float*,float*); void func_005F5268(float*,float*);
}
static inline float length(float *p) { float r=func_005F5010(p); __asm__("sqrt.s %0, %1":"=f"(r):"f"(r)); return r; }
static inline void setPosition(Record *r, Item *item, float *pos) { r->item=item; func_005F5040(pos,r->pos); }
static inline void finish(Record *r,float a,float b) {
 r->f40=a; r->f44=b; r->a48=5; r->a4c=3; r->a50=10; r->a54=6; r->f58=0; r->flags=(unsigned short)r->flags;
}
extern "C" void func_003FFD10(Out *out,Scene *scene,Input *input,void *rng,float *initial) {
 int hit=-1;
 float origin[3]; float zero[3]; float pos[3]; float a,b;
 union { struct { Item *items[64]; float delta[4],dir[4]; } objects; struct { float delta[4],dir[4]; } random; } u;
 float height;
 int world=scene->world;
 func_003FF9A0(out);
 out->world=scene->world;
 out->records=(Record*)func_00575DC8(0x1d00);
 out->extra=func_00575DC8(32);
 Record *r=out->records;
 func_003FC618(input,origin);
 func_005A48D8(zero,0,12);
 if(initial) {
  func_005F5040(origin,pos); func_005F5268(pos,initial);
  float d=length(pos); if(d<0.1f) d=0.1f;
  func_003FFC68(&a,&b,d);
  a=initial[6]; if(b<a) a=b;
  float *idir=initial+3; float half=b*0.5f;
  float angle=func_0036EB98(0.0f,func_0036EAD8(initial[4]))+ -90.0f;
  if(b<=angle) angle=-b*0.5f;
  r->item=0;
  func_005F5040(initial,r->pos); func_005F5040(idir,r->dir);
  int oy=4; float *iy=(float*)((char*)initial+oy);
  r->f1c=b; r->f20=d; r->f2c=*iy; r->f30=*iy; r->f24=angle; r->f28=half;
  func_005F5040(zero,r->zero); finish(r,a,d); ++r; ++out->count;
 }
 float *delta=(float*)u.objects.items;
 int n=func_003FDC50(scene,input,(Item**)delta,63);
 if(n>0) {
  int count=n;
  float *py=pos+1; float *dy=u.objects.dir+1; Item *volatile *it=u.objects.items;
  do {
   Item *item=*it; float x=item->pos[0], y=item->pos[1], z=item->pos[2]; pos[0]=x; *py=y; pos[2]=z;
   func_005F5040(origin,u.objects.delta); func_005F5268(u.objects.delta,pos);
   float d=length(u.objects.delta); if(d<0.1f) d=0.1f;
   func_003FFC68(&b,&a,d);
   *dy=func_0036EBE8(func_0036EA50(u.objects.delta),b,0x1.cccccap-2f);
   u.objects.dir[0]=func_0036EA98(u.objects.delta); u.objects.dir[2]=0;
   float half=a*0.5f;
   float low;
   float angle=func_0036EB98(0.0f,func_0036EAD8(*dy))+ -90.0f;
   if(a<=angle) angle=-a*0.5f;
   hit=-1; float high=*py; float ground=func_003FE358(world,pos,&hit); low=high;
   if(hit>=0) { if((*it)->flags&4) { float y=*py; high=ground+3.0f; low=ground+0.4f; if(high<y)high=y; } }
   setPosition(r,*it++,pos); --count;func_005F5040(u.objects.dir,r->dir);
   r->f1c=a;r->f20=d;r->f2c=low;r->f30=high;r->f24=angle;r->f28=half;
   func_005F5040(zero,r->zero);finish(r,b,d);++r;++out->count;
  } while(count);
 }
 Inner *inner=input->inner;
 int remain=64-out->count;
 if(inner) {
  int value=inner->value;
  int i=0;
  float phase=func_00578598(rng)*360.0f;
  if(remain>0) {
  float step=360.0f/(float)remain;
  float *dr=u.random.dir; float *ph=&height; float *dy=dr+1;
  do {
   float index=(float)i;
   float angle=step*(index+func_00578598(rng)*0.7f)+phase;
   if(func_003FFA48(world,value,rng,pos,ph,func_0036EAD8(angle))) {
    func_005F5040(origin,delta);func_005F5268(delta,pos);
    float d=length(delta);if(d<0.1f)d=0.1f;
    func_003FFC68(&b,&a,d);
    *dy=func_0036EBE8(func_0036EA50(delta),b,0x1.cccccap-2f);
    *dr=func_0036EA98(delta);u.random.dir[2]=0;
    float half=a*0.5f;
    float angle2=func_0036EB98(0.0f,func_0036EAD8(*dy))+ -90.0f;
    if(a<=angle2)angle2=-a*0.5f;
    float high=height+3.0f;height+=0.4f;
    int off=4; float *py=(float*)((char*)pos+off); if(high<*py)*py=high;
    r->item=0;func_005F5040(pos,r->pos);func_005F5040(dr,r->dir);
    r->f1c=a;r->f20=d;r->f2c=height;r->f30=high;r->f24=angle2;r->f28=half;
    func_005F5040(zero,r->zero);finish(r,b,d);++r;++out->count;
   }
  } while(++i<remain);
 }
 }
}
