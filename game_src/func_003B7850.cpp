/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction: virtual slots 2 and 3 (getRaceCode, getCourseCode) return 64-bit ids
 * (table << 32 | row). The matched source calls them through an int-returning pointer, which on the
 * EE still passes the whole $v0 on to the 64-bit parameters; natively edx was dropped, the race id
 * was not found in the database, and the RaceSpec's race row (start positions, laps...) stayed
 * whatever the recycled heap block held: the player and car 2 started anywhere on the course. */
struct E2 { short delta,pad;int (*fn)(void*,void*); };
struct EV { short delta, pad; void (*fn)(void*,int); };
struct E { short delta, pad; int (*fn)(void*); };
struct EL { short delta, pad; long (*fn)(void*); };
struct Info { char pad[0xB0]; char body[0x60]; char tail[0x1C]; E *vt; };
struct Obj { int x; char pad4[0x5C]; char *p60; char pad64[0x1C]; void *p80; char pad84[0x40]; int c4; char padc8[8]; int d0; };
extern "C" void *exception__structor_0(int);
extern "C" void RaceCourse__structor_0(void*), func_00446F90(void*), func_003BFB90(void*);
extern "C" void func_003BFDD8(void*,int),func_003BFF00(void*,int),func_003BFF80(void*,void*),func_003BFD58(void*,long),func_003C0C20(void*,long),RaceCourse__setSectionNumber(void*,int),func_003C0700(void*);
inline int call(Info *p,int i) { E *e=p->vt+i; return e->fn((char*)p+e->delta); }
inline long call64(Info *p,int i) { EL *e=(EL*)p->vt+i; return e->fn((char*)p+e->delta); }
inline void call2(Info *p,int i,void *a) { EV *e=(EV*)p->vt+i; e->fn((char*)p+e->delta,(int)a); }
extern "C" Obj *func_003B7850(Info *self) {
 void *course=exception__structor_0(0xBC);RaceCourse__structor_0(course);
 void *other=exception__structor_0(0xB0);func_00446F90(other);
 char *src=(char*)call(self,5);
 Obj *obj=(Obj*)exception__structor_0(0x978);func_003BFB90(obj);func_003BFDD8(obj,(int)other);
 __builtin_memcpy(obj,self->body,0x60);
 func_003BFF00(obj,(int)src);func_003BFF80(obj,course);
 func_003BFD58(obj,call64(self,2));func_003C0C20(obj,call64(self,3));
 obj->x=call(self,7);obj->c4=call(self,8);obj->d0=call(self,9);
 RaceCourse__setSectionNumber(obj->p80,call(self,18));
 int count=call(self,11);
 if(count>0) { int n=count; do {
  E *e=*(E**)(src+0x20)+2;
  char *car=(char*)e->fn(src+e->delta);
  char *p=obj->p60;E2 *v=*(E2**)(p+0x20)+5;
  if(v->fn(p+v->delta,car)!=-1)call2(self,13,car);
  else if(car){EV *v=*(EV**)(car+0x35CC)+1;v->fn(car+v->delta,3);}
  --n;
 }while(n); }
 call2(self,14,obj);func_003C0700(obj);return obj;
}
