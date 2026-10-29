/* Native copy of MCarGarage::buildEngineCurveData (00148B60, matched): the power and torque curves drawn on the
 * car settings screen. 0034B960 fills `arr` through 0034B808 with a 0x7C-byte gearing table (count, two rpm and two
 * ratio arrays of 10), not just the 16-byte handle the source declares. On the EE that overflow lands in frame space
 * nothing reads; natively the next local was the curve `d`, whose point count (+0xE) and rpm points it overwrote
 * (7000: the loops then read 28 KB up the stack and faulted, and the clamped curve came out empty). `arr` gets the
 * table's room here; the count stays bounded by the arrays as a guard. */
/* compiler: ee-gcc2.96-nogcse */
struct VE { short delta, index; int (*fn)(void *); };
struct VF { short delta, index; float (*fn)(void *); };
struct Value { int first; char *vt; };
struct Handle { void *p; char pad[12]; };
struct Data { float a,b; int pad; unsigned short end,n; short x[24]; float y[24],z[24]; };
struct Pair { float x,y; };
struct Curve { int n; Pair p[256]; };
extern "C" {
void func_0013BDC0(Handle *,void *);
void func_0013BD68(Handle *,int);
char *func_00147D80(void *);
void *func_00441248(void *);
void func_004413C8(void *,void *);
int func_0034B960(void *,Handle *,Data *,void *);
void func_002EFC48(Handle *,int);
Curve *func_00575DC8(int) throw();
Handle *HArray__operator_index(Handle *,int);
void func_00200140(Handle *,Curve *,int);
void func_001FFF30(Handle *,int);
void func_002F9360(Handle *,float);
void func_002F7B68(Handle *,int);
void func_002ED5C0(Handle *,int);
void func_003285A8(void *);
void func_003285F8(void *);
}
static inline int geti(Value *p) { VE *e=(VE *)(p->vt+0x58); return e->fn((char *)p+e->delta); }
static inline float getf(Value *p) { VF *e=(VF *)(p->vt+0x60); return e->fn((char *)p+e->delta); }
extern "C" void MCarGarage__buildEngineCurveData(Handle *ret,void *arg,int argc,Value **argv) {
 Data d;
 union { Handle h; char table[0x80]; } arr;   /* native: room for 0034B808's table (see above) */
 Handle tmp;
 char spare[0x60];
 Handle h;
 char buf[0x600];
 int autoscale=1;
 void *newval;
 Handle *dst;
 Handle *hp=&h;
 func_0013BDC0(hp,arg);
 char *ctx=func_00147D80(hp->p);
 func_0013BD68(hp,2);
 void *src=func_00441248(ctx);
 ctx+=0x4a0;
 func_004413C8(src,buf);
 Handle *ap=&arr.h;
 func_0034B960(buf,ap,&d,ctx);
 if(argc>0) autoscale=geti(argv[0])!=0;
 float lim1=1000.0f;
 if(argc>1) lim1=getf(argv[1]);
 float lim2=200.0f;
 if(argc>2) lim2=getf(argv[2]);
 func_002EFC48(ap,6);
 int count=d.n;
 if(count>24) count=24;   /* native guard: the arrays hold 24 points */
 Curve *c=func_00575DC8(0x804);
 c->n=count;
 float scale=lim1;
 if(autoscale) scale=d.a; else if(scale<d.a) scale=d.a;
 for(int i=0;i<count;i++) {
  float *o=(float *)c+i*2;
  float x=float(d.x[i]-d.x[0])/float(d.end-d.x[0]);
  float y=d.y[i]/scale;
  o[1]=x;
  o[2]=y;
 }
 dst=HArray__operator_index(ap,0);
 Handle *tp=&tmp;
 func_00200140(tp,c,1);
 if(dst!=tp) { newval=tp->p; if(newval) func_003285A8(newval); if(dst->p) func_003285F8(dst->p); dst->p=newval; }
 func_001FFF30(tp,2);
 c=func_00575DC8(0x804);
 c->n=count;
 scale=lim2;
 if(autoscale) scale=d.b; else if(scale<d.b) scale=d.b;
 for(int i=0;i<count;i++) {
  float *o=(float *)c+i*2;
  float x=float(d.x[i]-d.x[0])/float(d.end-d.x[0]);
  float y=d.z[i]/scale;
  o[1]=x;
  o[2]=y;
 }
 dst=HArray__operator_index(ap,1);
 func_00200140(tp,c,1);
 if(dst!=tp) { newval=tp->p; if(newval) func_003285A8(newval); if(dst->p) func_003285F8(dst->p); dst->p=newval; }
 func_001FFF30(tp,2);
 dst=HArray__operator_index(ap,2);
 func_002F9360(tp,d.a);
 if(dst!=tp) { newval=tp->p; if(newval) func_003285A8(newval); if(dst->p) func_003285F8(dst->p); dst->p=newval; }
 func_002F7B68(tp,2);
 dst=HArray__operator_index(ap,3);
 func_002F9360(tp,d.b);
 if(dst!=tp) { newval=tp->p; if(newval) func_003285A8(newval); if(dst->p) func_003285F8(dst->p); dst->p=newval; }
 func_002F7B68(tp,2);
 dst=HArray__operator_index(ap,4);
 func_002F9360(tp,count>0 ? float(d.x[0]) : 0.0f);
 if(dst!=tp) { newval=tp->p; if(newval) func_003285A8(newval); if(dst->p) func_003285F8(dst->p); dst->p=newval; }
 func_002F7B68(tp,2);
 dst=HArray__operator_index(ap,5);
 func_002F9360(tp,float(d.end));
 if(dst!=tp) { newval=tp->p; if(newval) func_003285A8(newval); if(dst->p) func_003285F8(dst->p); dst->p=newval; }
 func_002F7B68(tp,2);
 if(ret!=ap) { newval=ap->p; if(newval) func_003285A8(newval); if(ret->p) func_003285F8(ret->p); ret->p=newval; }
 func_002ED5C0(ap,2);
}
