/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction (width): func_00326750 (the allocator) takes unsigned int size, int align, const char *name; this source declares the size long (one register on the EE, 8 bytes natively), which shifts align and name. Declaring it unsigned int no longer matches on the EE (the long is truncated). */
struct Elem { void *p; };
struct Vec { int alloc; Elem *start,*finish,*limit; };
struct Obj { int ref; void *vt; int n; Vec *v; };
extern char hObject__vtable[],D_0069DE48[];
extern "C" int RefCounter__structor_1(void *);
extern "C" void *func_00326750(unsigned int,int,const char *);
extern "C" const char **HObject__tf();
extern "C" Elem *func_005EC5E0(Elem *,Elem *,Elem *,int);
static inline Elem *copy(Elem *first,Elem *last,Elem *p) {
 int sp[4]; sp[0]=(int)p; return func_005EC5E0(first,last,p,0);
}
extern "C" void *hObject__structor_1(Obj *self,Obj *other) {
  int spare[4];
  RefCounter__structor_1(self);
  self->vt=hObject__vtable;
  self->n=other->n;
  self->v=0;
  if(other->v) {
    Vec *v=(Vec *)func_00326750(0x100,4,D_0069DE48);
    Vec *src=other->v;
    int bytes=(((int)src->finish-(int)src->start)>>2)*4;
    v->start=0; v->finish=0; v->limit=0;
    Elem *p=(Elem *)func_00326750(bytes,4,*HObject__tf());
    v->start=p;
    v->limit=(Elem *)((char *)p+bytes);
    v->finish=p;
    v->finish=copy(src->start,src->finish,p);
    self->v=v;
    return v->finish;
  }
  return 0;
}
/* native correction of src/adhoc/hObject/hObject__structor_1.cpp: returns what $v0 holds on the PS2: 0 (other->v, just tested) when other has no vector, else the end pointer returned by the copy func_005EC5E0. */
