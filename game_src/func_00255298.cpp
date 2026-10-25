/* compiler: ee-gcc2.96-no-strict-aliasing */
typedef unsigned int u32;
struct Rep { int len, cap, ref, sel; };
struct Info { const char *name; };
struct V4 { float x,y,w,h; };
struct V2 { float x,y; };
struct Node { Node *next, *prev; int v; };
struct List { int allocator; Node *head; };
struct Flags {
 u32 a:4,b:4,c:4,d:1,e:1,f:1,g:1,h:1,i:1,j:1,k:1,l:1,m:1,n:1,o:2,p:1,q:1,r:4,s:1;
};
struct Flags2 { u32 a:1,b:1,c:1,d:1,e:1,f:1,g:1,h:1,i:1,j:6,k:17; };
struct Self {
 int m0; void *vt;
 char pad[0x24];
 int m2c, m30;
 V4 geom, padding;
 int m54,m58,m5c;
 List list;
 int m68;
 V2 scale;
 float rotate;
 V2 skew,translate;
 float opacity;
 char *tooltip; int m90; char *tooltipkey;
 Flags flags; Flags2 flags2;
};
extern int D_00830950;
extern char mWidget__vtable[];
extern Rep D_00659FA8;
extern "C" void *hModule__structor_0(Self *, int *);
extern "C" int func_0026A0E8(int *, int *);
extern "C" int func_0024B218(int *, int *);
extern "C" Info *func_005DEA88();
extern "C" void *func_00326750(u32, int, const char *);
extern "C" char *func_005C2560(Rep *);
static inline void strinit(char **dst) {
 Rep *r=&D_00659FA8;
 char *p;
 if(r->sel) p=func_005C2560(r); else { ++r->ref; p=(char *)(r+1); }
 *dst=p;
}
static inline void rect(V4 *v,float z,float w) {v->x=z;v->h=w;v->w=w;v->y=z;}
static inline void vec(V2 *v,float z) {v->x=z;v->y=z;}
extern "C" int mWidget__structor_0(Self *self) {
 List *list=&self->list;
 char **ps=&self->tooltip;
 hModule__structor_0(self,&D_00830950);
 self->vt=mWidget__vtable;
 self->m2c=0;
 float z=0.0f;
 int zeros[8];
 zeros[4]=0;
 func_0026A0E8(&self->m30,&zeros[4]);
 V4 *v=&self->geom;
 V4 *p=&self->padding;
 v->w=128.0f; v->h=128.0f; v->y=z; v->x=z;
 p->x=z; p->y=z; p->w=z; p->h=z;
 self->m54=0;self->m58=0;self->m5c=0;
 list->head=(Node *)func_00326750(12,4,func_005DEA88()->name);
 list->head->next=list->head;
 list->head->prev=list->head;
 zeros[5]=0;
 func_0024B218(&self->m68,&zeros[5]);
 vec(&self->scale,1.0f);
 self->rotate=z;
 vec(&self->skew,z);
 vec(&self->translate,z);
 self->opacity=1.0f;
 strinit(ps);
 self->m90=0;
 strinit(&self->tooltipkey);
 self->flags.a=0;self->flags.b=0;self->flags.c=0;
 self->flags.d=0;self->flags.e=0;self->flags.f=1;self->flags.g=0;
 self->flags.h=0;self->flags.i=0;self->flags.j=1;self->flags.k=0;
 self->flags.l=0;self->flags.m=0;self->flags.n=0;self->flags.o=2;
 self->flags.p=0;self->flags.q=1;self->flags.r=8;self->flags.s=0;
 self->flags2.a=1;self->flags2.b=1;self->flags2.c=0;self->flags2.d=0;
 self->flags2.e=0;self->flags2.f=0;self->flags2.g=0;self->flags2.h=0;
 self->flags2.i=0;self->flags2.j=63;
 return -0x101;
}
/* native correction of src/ui/mWidget/mWidget__structor_0.cpp: returns -0x101, the bit-field mask constant left in $v0 at the end on the PS2 */
