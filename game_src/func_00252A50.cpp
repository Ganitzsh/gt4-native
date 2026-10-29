/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of the provisional draft for 00252A50: the slot-0x1E8 pad-state getter gets index, window and target, which the EE forwards to it in $a1-$a3 (passthrough_lint) */
struct State { int active; unsigned short buttons, pad; int x,y; };
struct Keys { int n; int down[16]; int m; int up[16]; int mods; };
struct Obj { int ref; char *vt; char pad[0x44]; State states[2]; char pad2[0x48]; int last; };
struct Entry { short delta,index; void *(*fn)(void *); };
struct Handle { void *p; int pad[3]; };
struct Event { int ref; void *vt; char pad[0x18]; int code,mods; float x,y; int repeat; };
extern char D_006997D8[],D_0066D768[],D_0066D910[],D_006695C0[],D_00669768[];
extern "C" void *func_0022F1F0(void *);
extern "C" void *func_0022F350(void *,int);
extern "C" void func_00230EC0(void *,float *,float *);
extern "C" void func_00231020(void *,float,float);
extern "C" void func_00250CE0(Obj *,Handle *) throw();
extern "C" void *func_0026BB40(Event *,void *,void *);
extern "C" int func_0027F2F0(Handle *,Event **);
extern "C" int func_002A95E0(Handle *,Event **);
extern "C" void func_00280630(Handle *,int);
extern "C" void func_002811A0(Handle *,int);
extern "C" void func_002AA7B0(Handle *,int);
extern "C" void func_002AB508(Handle *,int);
extern "C" void *func_00326750(unsigned,int,const char *);
static inline Event *key(void *w,void *t,int c,int mod,float x,float y,int repeat,int press) {
 Event *p=(Event*)func_00326750(press?0x34:0x30,4,D_006997D8);
 func_0026BB40(p,w,t);
 p->code=c; p->x=x; p->y=y;
 if(press) p->repeat=repeat;
 p->vt=press?D_0066D768:D_0066D910; p->mods=mod;
 return p;
}
static inline Event *button(void *w,void *t,short c,int mod,float x,float y,int press) {
 Event *p=(Event*)func_00326750(0x30,4,D_006997D8);
 func_0026BB40(p,w,t);
 *(short*)&p->code=c; p->x=x; p->y=y;
 p->vt=press?D_006695C0:D_00669768; p->mods=mod;
 return p;
}
static inline void makebutton(Handle *h,Event **out,void *w,void *t,short c,int mod,float x,float y,int press) {
 *out=button(w,t,c,mod,x,y,press); func_0027F2F0(h,out);
}
static inline State *padState(Obj *o) { Entry *e=(Entry*)(o->vt+0x1e8); return (State*)e->fn((char*)o+e->delta); }
static inline State *padStateFwd(Obj *o,int a1,void *a2,void *a3) { Entry *e=(Entry*)(o->vt+0x1e8); return (State*)((void *(*)(void *,int,void *,void *))e->fn)((char*)o+e->delta,a1,a2,a3); }
static inline Keys *keyState(Obj *o) { Entry *e=(Entry*)(o->vt+0x1f0); return (Keys*)e->fn((char*)o+e->delta); }
extern "C" int func_00252A50(Obj *self,int index,void *window,void *target) {
 float x,y;
 Handle h;
 Event *p0,*p1,*p2,*p3,*p4,*p5,*p6,*p7,*p8,*p9;
 int result=0;
 State *st=padStateFwd(self,index,window,target);
 if(st->active) func_00231020(window,(float)st->x,(float)st->y);
 func_00230EC0(window,&x,&y);
 Keys *keys=keyState(self);
 int mods=keys->mods;
 if(keys->n>0) {
  int last;
  for(int i=0;i<keys->n;i++) {
   last=keys->down[i];
   int repeat=self->last==last;
   p0=key(window,target,last,mods,x,y,repeat,1);
   func_002A95E0(&h,&p0); func_00250CE0(self,&h); func_002AA7B0(&h,2);
  }
  self->last=last; result=1;
 }
 if(keys->m>0) {
  for(int i=0;i<keys->m;i++) {
   p1=key(window,target,keys->up[i],mods,x,y,0,0);
   func_002A95E0(&h,&p1); func_00250CE0(self,&h); func_002AB508(&h,2);
  }
  result=1; self->last=0xffffff;
 }
 if(st->active) {
  void *t=func_0022F1F0(window);
  if(t || (t=func_0022F350(window,index))) {
   unsigned short now=st->buttons;
   unsigned short diff=self->states[index].buttons^now;
   unsigned short pressed=diff&now;
   if(diff) {
    if(pressed & 1) {
     makebutton(&h,&p2,window,t,1,mods,x,y,1); func_00250CE0(self,&h); func_00280630(&h,2);
    }
    if(pressed & 2) {
     makebutton(&h,&p3,window,t,2,mods,x,y,1); func_00250CE0(self,&h); func_00280630(&h,2);
    }
    if(pressed & 4) {
     makebutton(&h,&p4,window,t,3,mods,x,y,1); func_00250CE0(self,&h); func_00280630(&h,2);
    }
    if(pressed & 8) {
     makebutton(&h,&p5,window,t,4,mods,x,y,1); func_00250CE0(self,&h); func_00280630(&h,2);
    }
    if(pressed & 16) {
     makebutton(&h,&p6,window,t,5,mods,x,y,1); func_00250CE0(self,&h); func_00280630(&h,2);
    }
    unsigned short released=diff & ~now;
    if(released & 1) {
     makebutton(&h,&p7,window,t,1,mods,x,y,0); func_00250CE0(self,&h); func_002811A0(&h,2);
    }
    if(released & 2) {
     makebutton(&h,&p8,window,t,2,mods,x,y,0); func_00250CE0(self,&h); func_002811A0(&h,2);
    }
    if(released & 4) {
     makebutton(&h,&p9,window,t,3,mods,x,y,0); func_00250CE0(self,&h); func_002811A0(&h,2);
    }
    self->states[index]=*st;
   }
  }
  result=1;
 }
 return result;
}
