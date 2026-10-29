/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of the provisional draft for 00251EF0: the slot-0x1D8 button getter also gets w and obj, which the EE forwards to it in $a2 and $a3 (passthrough_lint) */
struct Event { int ref; void *vt; char pad[0x18]; int key,flags; float x,y; int repeat; };
struct Handle { Event *p; int pad[3]; };
struct EntryI { short delta,index; int (*fn)(void *,int); };
struct EntryF { short delta,index; float (*fn)(void *,int,int); };
struct Obj { int ref; char *vt; char pad[0xa4]; int speed,damp; };
struct Window { char pad[0x6ec]; float x[3],y[3]; char pad2[0x164c]; int mode; };
struct Mapping { int mask,key; };
extern Mapping D_006192A0[],D_006192E0[];
extern char D_006997D8[],D_0066D910[],D_0066D768[];
extern "C" void *func_0022F1F0(Window *);
extern "C" void *func_0022F350(Window *);
extern "C" void func_00230EC0(Window *,float *,float *);
extern "C" int func_00233E08(void *);
extern "C" void func_00250CE0(Obj *,Handle *) throw();
extern "C" int func_00250FC0(Obj *,int) throw();
extern "C" int func_00251300(Obj *,int,int,Window *) throw();
extern "C" int func_002517E0(Obj *,int) throw();
extern "C" void func_00251838(Obj *,int) throw();
extern "C" int func_00251860(Obj *,int) throw();
extern "C" int func_00251880(Obj *,int) throw();
extern "C" void func_00251890(Obj *,int,int) throw();
extern "C" void func_00251EA0(Obj *,int) throw();
extern "C" Event *func_0026BB40(Event *,Window *,void *);
extern "C" int func_002A95E0(Handle *,Event **);
extern "C" void func_002AA7B0(Handle *,int);
extern "C" void func_002AB508(Handle *,int);
extern "C" void *func_00326750(unsigned,int,const char *);
extern "C" float *func_0057AF30(float *,float);
extern "C" float *func_0057AFF0(float *,float);
static inline int dead(float v) { if(v>=0.0f) { if(v<0.5f) return 1; } else if(v>-0.5f) return 1; return 0; }
static inline float axis(Obj *p,int i,int j) { EntryF *e=(EntryF*)(p->vt+0x1e0); return e->fn((char*)p+e->delta,i,j); }
static inline void init(Event *p,Window *w,void *o,int key,int flags,float x,float y) { func_0026BB40(p,w,o); p->key=key; p->flags=flags; p->x=x; p->y=y; }
#define PRESS(SLOT,KEY,FLAGS,REPEAT) { Handle *h=&handle; Event **out=&p##SLOT; int k=(KEY); int flags=(FLAGS); float x=posx,y=posy; Event *e=(Event*)func_00326750(0x34,4,D_006997D8); init(e,w,obj,k,flags,x,y); e->vt=D_0066D768; e->repeat=(REPEAT); p##SLOT=e; result=1; func_002A95E0(h,out); func_00250CE0(self,h); func_002AA7B0(h,2); }
#define PRESS_LOOP(SLOT,KEY,FLAGS,REPEAT) { Handle *h=&handle; Event **out=&p##SLOT; int k=(KEY); float x=posx,y=posy; Event *e=(Event*)func_00326750(0x34,4,D_006997D8); func_0026BB40(e,w,obj); e->key=k; e->flags=(FLAGS); e->x=x; e->y=y; e->vt=D_0066D768; e->repeat=(REPEAT); p##SLOT=e; result=1; func_002A95E0(h,out); func_00250CE0(self,h); func_002AA7B0(h,2); }
#define RELEASE_LOOP(SLOT,KEY,FLAGS) { Handle *h=&handle; Event **out=&p##SLOT; int k=(KEY); float x=posx,y=posy; Event *e=(Event*)func_00326750(0x30,4,D_006997D8); func_0026BB40(e,w,obj); e->key=k; e->flags=(FLAGS); e->x=x; e->y=y; e->vt=D_0066D910; p##SLOT=e; result=1; func_002A95E0(h,out); func_00250CE0(self,h); func_002AB508(h,2); }
#define RELEASE(SLOT,KEY,FLAGS) { Handle *h=&handle; Event **out=&p##SLOT; int k=(KEY); int flags=(FLAGS); float x=posx,y=posy; Event *e=(Event*)func_00326750(0x30,4,D_006997D8); init(e,w,obj,k,flags,x,y); e->vt=D_0066D910; p##SLOT=e; result=1; func_002A95E0(h,out); func_00250CE0(self,h); func_002AB508(h,2); }
extern "C" int func_00251EF0(Obj *self,int index,Window *w,void *obj) {
 float posx,posy; Handle handle; Event *p0,*p1,*p2,*p3,*p4,*p5,*p6,*p7,*p8;
 int result,buttons,on,off,savedflags,mask;
 EntryI *v=(EntryI*)(self->vt+0x1d8);
 buttons=((int (*)(void *,int,Window *,void *))v->fn)((char*)self+v->delta,index,w,obj);
 if(w->mode==0) {
  float speed=(float)self->speed;
  float *px=w->x,*py=w->y;
  float x=axis(self,index,2),y=axis(self,index,3);
  if(x>=0.0f ? x<0.5f : x>-0.5f) x=0.0f;
  if(y>=0.0f ? y<0.5f : y>-0.5f) y=0.0f;
  func_0057AF30(px,x*speed); func_0057AF30(py,y*speed);
  if((buttons&5)==5) { float d=-speed*0x1.69fbe6p-1f; func_0057AF30(px,d); func_0057AF30(py,d); }
  else if((buttons&9)==9) { func_0057AF30(px,speed*0x1.69fbe6p-1f); func_0057AF30(py,-speed*0x1.69fbe6p-1f); }
  else if((buttons&6)==6) { func_0057AF30(px,-speed*0x1.69fbe6p-1f); func_0057AF30(py,speed*0x1.69fbe6p-1f); }
  else if((buttons&10)==10) { float d=speed*0x1.69fbe6p-1f; func_0057AF30(px,d); func_0057AF30(py,d); }
  else if(buttons&1) func_0057AF30(py,-speed);
  else if(buttons&2) func_0057AF30(py,speed);
  else if(buttons&4) func_0057AF30(px,-speed);
  else if(buttons&8) func_0057AF30(px,speed);
  float damp=(float)self->damp;
  func_0057AFF0(px,damp); func_0057AFF0(py,damp);
 }
 func_00230EC0(w,&posx,&posy);
 result=func_00251300(self,index,0,w)!=0;
 int changed=func_00250FC0(self,index)^buttons;
 if(changed) {
  if(!func_0022F1F0(w)) func_0022F350(w);
  off=changed & ~buttons; on=changed & buttons;
  for(int n=0;D_006192E0[n].key!=0xffffff;n++) {
   savedflags=index|0x10;
   if(on&D_006192E0[n].mask) {
    PRESS_LOOP(0,D_006192E0[n].key,savedflags,0)
    func_00251EA0(self,D_006192E0[n].key);
   }
   if(off&D_006192E0[n].mask) RELEASE_LOOP(1,D_006192E0[n].key,savedflags)
  }
  mask=func_00251880(self,index);
  if(func_00233E08(obj)) {
   for(int n=0;n<4;n++) {
    if(off&D_006192A0[n].mask) {
     if(mask==D_006192A0[n].mask && !func_00251860(self,index)) {
      PRESS(2,D_006192A0[n].key,index|0x10,0)
      RELEASE(3,D_006192A0[n].key,index|0x10)
     } else RELEASE(4,D_006192A0[n].key,index|0x10)
     break;
    }
   }
   func_00251838(self,index);
   if(off) mask=0;
   if(on&15) {
    mask|=on&15;
    for(int n=4;n<8;n++) if((buttons&15)==D_006192A0[n].mask) { PRESS(5,D_006192A0[n].key,index|0x10,0) break; }
   }
   func_00251890(self,index,mask);
  } else {
   for(int n=0;n<4;n++) {
    if(on&D_006192A0[n].mask) PRESS(6,D_006192A0[n].key,index|0x10,0)
    if(off&D_006192A0[n].mask) RELEASE(7,D_006192A0[n].key,index|0x10)
   }
   func_00251838(self,index);
   if(off) mask=0;
   func_00251890(self,index,mask|(on&15));
  }
 } else if(func_002517E0(self,index)) {
  int count=func_00233E08(obj)?8:4;
  for(int n=0;n<count;n++) if((buttons&15)==D_006192A0[n].mask) { PRESS(8,D_006192A0[n].key,index|0x10,1) break; }
 }
 return result;
}
