/* compiler: ee-gcc2.96-no-strict-aliasing */
// Native correction of the draft drafts/00376638/00376638_c1fe56687ee70ac6.cpp (no matched source): it declares func_0057FB50 (fptodp)
// as returning double, but the soft-float helper returns the double's bits as a 64-bit integer
// (edx:eax natively, as gt4-native implements it), so the double read from st(0) was garbage.
// Declared as returning the bits here and converted back to double through a union (bit-exact).
typedef unsigned long u64;
typedef float f32;
#define F(p,t,o) (*(t *)((char *)(p)+(o)))
template<class T> struct VE { short delta,index; T fn; };
extern "C" int func_0035C7C0(void *);
extern "C" int func_00375F58(void *,int,void *,void *,int,float,void *,int) throw();
extern "C" int func_00376238(void *,int,void *,void *,float,void *) throw();
extern "C" int func_003765F0(float *,float) throw();
extern "C" int func_003FD620(void *,int,void *,int,int *);
extern "C" float func_00400BA0(void *);
extern "C" float func_00578598(void *);
extern "C" int func_0057DA20(char *,const char *,...);
extern "C" long func_0057FB50(float); /* the double's bits */
static inline double bits_to_double(long l) { union { long l; double d; } u; u.l = l; return u.d; }
extern char D_0069F9F0[];
extern float D_006210E0[],D_00621108[],D_006210B8[],D_00621130[];
static inline char *get(char *p,int i) { VE<char *(*)(void *,int)> *e=(VE<char *(*)(void *,int)> *)(F(p,char *,0)+0xD8); return e->fn(p+e->delta,i); }
static inline int kind(char *p) { VE<int(*)(void *)> *e=(VE<int(*)(void *)> *)(F(p,char *,0)+0xB8); return e->fn(p+e->delta); }
static inline void setup(char *p,char *s,int n) { VE<void(*)(void *,char *,int)> *e=(VE<void(*)(void *,char *,int)> *)(F(p,char *,0)+0x140); e->fn(p+e->delta,s,n); }
static inline void call(char *p,int off) { VE<void(*)(void *)> *e=(VE<void(*)(void *)> *)(F(p,char *,0)+off); e->fn(p+e->delta); }
static inline void init(char *p,int x,char *s,int n,float f) { VE<void(*)(void *,int,char *,int,float)> *e=(VE<void(*)(void *,int,char *,int,float)> *)(F(p,char *,0)+0x138); e->fn(p+e->delta,x,s,n,f); }
extern "C" int func_00376638(char *self,int idx,void *obj,char *s,int arg4) {
 char buf[512];
 int scratch;
 bool changed=true;
 char *rng=self+4;
 char *rec=self+idx*0x19C+0x10;
 int x=F(obj,int,0x80);
 const char *fmt=D_0069F9F0;
 float time=func_00400BA0(self+0xB80);
 func_0057DA20(buf,fmt,bits_to_double(func_0057FB50(time)));
 float *table=D_006210E0;
 if(time>0x1.f33332p+2f) table=D_00621108;
 if(time<2.0f) table=D_006210B8;
 if(F(s,char *,0x10) && func_0035C7C0(F(F(s,char *,0x10),void *,0x18))) table=D_00621130;
 int choice=func_003765F0(table,func_00578598(rng));
 int status=func_003FD620(obj,arg4,s,0,&scratch);
 switch(status) { case 1: {
  changed=false;
  char *p=F(rec,char *,0x158);
  char *current=get(self,idx);
  bool update=true;
  if(current==p) update=F(p,int,0x184)!=F(s,short,0xC);
  if(update) {
   changed=true;
   VE<void(*)(void *,char *)> *e=(VE<void(*)(void *,char *)> *)(F(p,char *,0)+0x138);
   e->fn(p+e->delta,s);
   F(self+idx*0x19C,char *,0x13C)=p;
  }
  choice=-1;
  break;
 } case 3: {
  int update=1;
  changed=false;
  char *p=F(rec,char *,0x148);
  if(get(self,idx)==p && F(p,void *,0x18C)==F(s,void *,0x10)) update=kind(p)!=0;
  if(update) {
   changed=true;
   setup(p,s,0); call(p,0x168);
   F(p,float,0x234)=0.5f; call(p,0x170);
   F(self+idx*0x19C,char *,0x13C)=p;
  }
  choice=-1;
  break;
 } }
 for(;;) {
  switch(choice) {
  case -1: goto done;
  case 0: func_00376238(self,idx,obj,s,time,rng); goto done;
  case 1: { char *p=F(rec,char *,0x170); init(p,x,s,0,time); F(self+idx*0x19C,char *,0x13C)=p; goto done; }
  case 2: { char *p=F(rec,char *,0x170); init(p,x,s,1,time); F(self+idx*0x19C,char *,0x13C)=p; goto done; }
  case 3: { char *p=F(rec,char *,0x170); init(p,x,s,2,time); F(self+idx*0x19C,char *,0x13C)=p; goto done; }
  case 7: {
   char *p=F(rec,char *,0x178);
   VE<void(*)(void *,int,float)> *e=(VE<void(*)(void *,int,float)> *)(F(p,char *,0)+0x138);
   e->fn(p+e->delta,x,time);
   F(self+idx*0x19C,char *,0x13C)=p; goto done;
  }
  case 4: case 5: case 6: {
   int k; switch(choice) { case 4:k=0;break;case 5:k=1;break;case 6:default:k=2;break; }
   char *p=F(rec,char *,0x164);
   VE<void(*)(void *,char *,int,int,int,float)> *e=(VE<void(*)(void *,char *,int,int,int,float)> *)(F(p,char *,0)+0x138);
   e->fn(p+e->delta,s,x,arg4,k,time);
   F(self+idx*0x19C,char *,0x13C)=p;
   if(F(p,int,0x18C)==0) goto retry;
   goto done;
  }
  case 8: default: if(func_00375F58(self,idx,obj,s,arg4,time,rng,0)) goto done; break;
  }
retry:
  choice=func_003765F0(D_00621108,func_00578598(rng));
 }
done:
 return changed;
}
