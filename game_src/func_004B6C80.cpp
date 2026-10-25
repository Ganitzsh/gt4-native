/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Entry { int state; char pad[0x418]; };
struct Obj { char pad[0x20]; Entry entries[2]; char pad2[0x858-0x858]; int current; int pad85c; int busy; };
extern "C" void func_004B6108(Obj*,int) throw();
extern "C" void func_004B63D0(Obj*,int,...) throw();
extern "C" void func_004B5628(Obj*) throw();
extern "C" void func_004B61A8(Obj*) throw();
extern "C" int func_004B6C80(Obj *self,int code,int arg2) {
 if(self->busy) return 1;
 code &= 0x7f;
 if(code<31) return 0;
 Entry *p=&self->entries[1-self->current];
 switch(p->state) {
 case 0: func_004B6108(self,code); return 1;
 case 1: func_004B63D0(self,code,arg2); return 1; /* native: the callee reads $a2, still arg2 on the EE */
 case 2:
 func_004B5628(self);
 func_004B61A8(self);
 p->state=1;
 func_004B63D0(self,code,arg2);
 return 1;
 default: return 1;
 }
}
