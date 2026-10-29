/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of the provisional draft for 00390980: the slot-0x50 virtual gets out and time, which the EE forwards to it in $a1 and $f12 (passthrough_lint) */
typedef unsigned long u64;
typedef unsigned int u32;
struct Vec { float x,y,z; };
struct Vec4 { float x,y,z,w; };
struct Point { u64 xy; u32 z; } __attribute__((packed));
struct Data { char pad[0x30]; int count; Vec4 *points; };
struct Entry { short d,i; Data *(*fn)(void *, void *, float); };
struct Entry2 { short d,i; void (*fn)(void *,Vec4 *,float); };
struct Car { char pad[8]; void *owner; char padc[0x5a0]; int id; char pad5b0[0x98]; Vec velocity; };
struct Model { void *obj; Car *car; char *vt; };
struct Config { char pad[0x98]; float speed; int min,max; float scale; int force; };
extern Config D_00621B50;
extern "C" int func_00366678(Car *);
extern "C" void func_00395930(void *,Vec *,int *,int *,Vec4 *);
extern "C" void func_004595C8(void *,float);
extern "C" void func_00459610(void *,float);
extern "C" void func_004A53F8();
extern "C" void func_004A5400();
extern "C" void func_004A7454();
extern "C" void func_004A7844(float,float,float);
extern "C" void func_003C24A8(void *,Vec *,Vec *,Vec4 *,int);
static inline void getpoint(Point *p) {
 u64 lo; u32 hi;
 __asm__ volatile("qmfc2.ni %0, $vf31" : "=r"(lo));
 __asm__("pcpyud %0, %1, %1" : "=r"(hi) : "r"(lo));
 p->xy=lo; p->z=hi;
}
extern "C" void func_00390980(Model *self,void *out,float time) {
 Vec pos;
 Vec4 plane;
 Vec4 matrix;
 Vec velocity;
 int id,flag;
 Car *car=self->car;
 Entry *e=(Entry *)(self->vt+0x50);
 Data *data=e->fn((char *)self+e->d,out,time);
 void *owner=car->owner;
 if(func_00366678(car)<2) return;
 int count=data->count;
 if(!count) return;
 Vec4 *pm=&matrix;
 Entry2 *e2=(Entry2 *)(self->vt+0x130);
 e2->fn((char *)self+e2->d,pm,time);
 Config *cfg=&D_00621B50;
 Vec *v=&car->velocity;
 Vec *pv=&velocity;
 float x; pv->x=x=v->x;
 float y; pv->y=y=v->z;
 float z; pv->z=z=-v->y;
 if(x*x+y*y+z*z < cfg->speed*cfg->speed*0x1.3c0ca4p-4f) return;
 func_004A53F8(); func_004A7454(); func_004595C8(self->obj,time);
 __asm__ volatile("vnop");
 getpoint((Point *)&pos);
 func_00459610(self->obj,time);
 id=car->id; flag=0;
 func_00395930(owner,&(*(Vec *)&pos),&id,&flag,&plane);
 if(id<0) {func_004A5400();return;}
 switch(flag) {
 default: case 2: case 3: case 4: case 5: case 6: case 8: func_004A5400(); return;
 case 0: case 1: case 7: case 9: case 10: case 11: case 12: break;
 }
 for(int i=0;i<count;i++) {
  func_004A53F8();
  Vec4 *p=&data->points[i];
  func_004A7844(p->x,p->y,p->z);
  getpoint((Point *)&pos);
  float y=-(plane.x*(*(Vec *)&pos).x+plane.z*(*(Vec *)&pos).z+plane.w)/plane.y;
  if((*(Vec *)&pos).y<y || cfg->force) {
   int strength=(int)((y-(*(Vec *)&pos).y)*cfg->scale);
   if(strength<cfg->min) strength=cfg->min;
   if(cfg->max<strength) strength=cfg->max;
   (*(Vec *)&pos).y=-(matrix.x*(*(Vec *)&pos).x+matrix.z*(*(Vec *)&pos).z+matrix.w)/matrix.y+0x1.999998p-5f;
   func_003C24A8(out,&(*(Vec *)&pos),pv,pm,strength);
  }
  func_004A5400();
 }
 func_004A5400();
}
