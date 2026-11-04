/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 00452AC8 (matched): a car's light points (a model drawn at each, with the alpha, scale and roll of
 * a state the car object gives per index). That getter (+0xD8, 003D7698) takes only the index; the matched source
 * passes the time first, the same call on the EE (the float in its own register) but not natively: the index got
 * the time's bits (0, or far outside the four states) and the light points a wrong or garbage alpha. */
struct Vec3 { float x,y,z; };
struct State { float x,y,z; State():x(0),y(1),z(0){} };
struct VEv { short d,p; Vec3 (*f)(void *,float); };
struct VEi { short d,p; Vec3 (*f)(void *,int); };
struct Obj { char *vt; };
struct Item { float x,y,z,dx,dy,dz; };
struct Data { char pad[0x38]; int count; Item *items; };
struct Effect { void *vt; float alpha; };
struct Render { char pad[0x14]; Effect *effect; };
struct Model { char pad[0x7c]; Render *render; };
extern char D_006885B8[];
extern float D_008465C8[4],D_008465D8,D_008465DC;
extern "C" {
Data *func_00450FB0(void *) throw();
void func_00452418(float, float, void *) throw();
void func_00452480(void *) throw();
void ModelSet2__begin(Model *,Render *);
void ModelSet2__end(Model *);
void ModelSet2___render(Model *,int,Render *);
void func_004574A0(Effect *,int);
void func_004595C8(Obj *,float);
void func_00459610(Obj *,float);
void func_004A53F8();
void func_004A5400();
void func_004A60E0(float,float,float,float);
void func_004A6100(float,float,float,float);
void func_004A7844(float,float,float);
void func_004A7890(float,float,float);
void func_004A7988(float);
void func_004A79B0(float);
void func_004A79D8(float);
float func_0057D118(float,float);
float func_0059D2A0(float);
}
inline Vec3 vcall(Obj *o,int off,float t) {
 VEv *e=(VEv *)(o->vt+off); return e->f((char *)o+e->d,t);
}
inline Vec3 vcalli(Obj *o,float t,int i) {
 VEi *e=(VEi *)(o->vt+0xd8); (void)t; return e->f((char *)o+e->d,i);
}
extern "C" void func_00452AC8(void *self,Obj *obj,int flag,Model *model,float t) {
 Data *data=func_00450FB0(self);
 if(data && obj) {
  int any;
  if(model) {
   any = 0;
   State states[4];
   for(int i=0;i<4;i++) {
    Vec3 result=vcalli(obj,t,i);
    Vec3 tmp=result;
    *(Vec3 *)&states[i]=tmp;
    if(states[i].x>0.0f) any=1;
   }
   if(any) {
    Render *render=model->render;
    Effect effect;
    effect.vt=D_006885B8;
    render->effect=&effect;
    func_004A53F8();
    if(flag) {
     Vec3 v;
     v=vcall(obj,0x40,t);
     float *px=&v.x,*py=&v.y,*pz=&v.z;
     func_004A7844(*px,*py,*pz);
     v=vcall(obj,0x38,t);
     func_004A79B0(*py);
     func_004A7988(*px);
     func_004A79D8(*pz);
    } else {
     func_004595C8(obj,t);
     func_00459610(obj,t);
    }
    if(flag) func_00452418(0.005f, 0.02f, self);
    ModelSet2__begin(model,render);
    func_004A60E0(D_008465C8[0],D_008465C8[1],D_008465C8[2],D_008465C8[3]);
    func_004A6100(0.0f,0.0f,0.0f,0.0f);
    for(int i=0;i<data->count;i++) {
     Vec3 v=*(Vec3 *)&states[i&3];
     float alpha=v.x;
     if(!(alpha<=0.0f)) {
      Item *item=&data->items[i];
      func_004A53F8();
      func_004A7844(item->x,flag?0.0f:item->y,item->z);
      float yaw=func_0057D118(item->dx,item->dz)*180.0f/0x1.921fb4p1f;
      float pitch=(0x1.921fb4p0f-func_0059D2A0(item->dy))*180.0f/0x1.921fb4p1f-90.0f;
      func_004A79B0(yaw);
      float scale;
      if(!flag) {
       func_004A7988(pitch);
       func_004A79D8(v.z);
      }
      scale=flag ? D_008465DC : D_008465D8;
      scale*=v.y;
      func_004A7890(scale,scale,scale);
      effect.alpha=alpha;
      ModelSet2___render(model,flag!=0,render);
      func_004A5400();
     }
    }
    ModelSet2__end(model);
    if(flag) func_00452480(self);
    func_004A5400();
    model->render->effect=0;
    effect.vt=D_006885B8;
    func_004574A0(&effect,0);
   }
  }
 }
}
