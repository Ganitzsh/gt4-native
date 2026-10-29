/* Native copy of the provisional draft for 0023E740: the case-5 float getter gets n and args, which the EE forwards to it in $a2 and $a3 ($a1 then holds the object) (passthrough_lint) */
struct Rep { int len, cap, ref, sel; };
struct Str { char *p; int pad[3]; };
struct Type { const char *name; };
struct VT;
struct Obj { int pad; VT *vtbl; };
struct IE { short delta,index; int (*fn)(void *); };
struct FE { short delta,index; float (*fn)(void *); };
struct SE { short delta,index; void (*fn)(Str *,void *); };
struct VT { char pad[0x18]; SE e18; char pad2[0x38]; IE e58; FE e60; };
extern char D_00698E18[];
extern "C" Type *func_005C11A8();
extern "C" void func_00326798(void *,int,int,const char *);
extern "C" void func_0023DE48(void *,void *) throw();
extern "C" void func_0023DDF0(void *,int) throw();
extern "C" void func_0023FAB8(void *,const char *,const char *,int,int,float);
static inline Rep *str_rep(Str *s) { return (Rep *)s->p - 1; }
static inline char *rep_data(Rep *r) { return (char *)(r + 1); }
static inline int str_length(Str *s) { return str_rep(s)->len; }
static inline void str_terminate(Str *s) { rep_data(str_rep(s))[str_length(s)] = 0; }
static inline const char *c_str(Str *s) {
 if (str_length(s) == 0) return D_00698E18;
 str_terminate(s);
 return rep_data(str_rep(s));
}
static inline void destroy(Str *s) {
 Rep *q = (Rep *)(s->p - 0x10);
 if (--q->ref == 0) {
  int cap = q->cap + 0x10;
  func_00326798(q, cap, 4, func_005C11A8()->name);
 }
}
static inline void text(Str *s, Obj *o) {
 SE *e = &o->vtbl->e18;
 e->fn(s, (char *)o + e->delta);
}
extern "C" void func_0023E740(void *unused,void *ctx,int n,Obj **args) {
 int value = -1;
 int flag = 0;
 float f = 1.0f;
 Str s;
 const char *a,*b;
 switch (n) {
 case 5: {
  Obj *o = args[4]; FE *e = &o->vtbl->e60;
  f = ((float (*)(void *,Obj *,int,Obj **))e->fn)((char *)o + e->delta,o,n,args);
 }
 case 4: {
  Obj *o = args[3]; IE *e = &o->vtbl->e58;
  flag = e->fn((char *)o + e->delta) != 0;
 }
 case 3: {
  Obj *o = args[2]; IE *e = &o->vtbl->e58;
  value = e->fn((char *)o + e->delta);
 }
 case 2: {
  text(&s,args[1]);
  b = c_str(&s);
  destroy(&s);
  text(&s,args[0]);
  a = c_str(&s);
  destroy(&s);
  func_0023DE48(&s,ctx);
  func_0023FAB8(s.p,a,b,value,flag,f);
  func_0023DDF0(&s,2);
  return;
 }
 }
}
