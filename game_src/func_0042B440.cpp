/* compiler: ee-gcc2.96-nsa-nosib */
typedef unsigned char u8;
struct Frame { char data[32]; };
struct Motion { int unk; Frame *frames; int unk8; u8 *code; };
struct Reader { u8 *p; int byte() { int x=*p; p++; return x; } int word() { u8 *q=p; int hi=q[1]; int lo=q[0]; p+=2; return (hi<<8)|lo; } char *str(); } __attribute__((aligned(16)));
struct Entry { short delta, index; void (*fn)(); };
struct Target { int stop; Entry *vt; };
struct Quat { float x,y,z,w; };
extern "C" int func_0057F260(const char *) throw();
inline char *Reader::str() { char *s=(char *)p; p+=func_0057F260(s)+1; return s; }
extern "C" void func_0042D688(float, Reader *, Frame *, float *, int, unsigned);
extern "C" void func_0042D780(Reader *, int, int);
extern "C" void func_0042B288(float *, const float *, int, float) throw();
extern "C" void func_0048B888(Quat *, float,float,float);
extern "C" void func_0048CA50(Quat *, const Quat *, const Quat *, float);
static inline int skip(Target *t, int slot) { Entry *e=t->vt+slot; return ((int (*)(void *))e->fn)((char *)t+e->delta); }
static inline int command(int op, Reader *r, Target *t, Frame *a, Frame *b, float sa, float sb, float blend, int interpolate) {
switch(op) {
case 5: {
 int mask=r->byte();
 if (skip(t,20)) {
 func_0042D780(r,3,mask); break;
 }
 float v[3]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 3, mask);
 float w[3]; if(interpolate) { func_0042D688(sb, pc, b, w, 3, mask); func_0042B288(v, w, 3, blend); }
{ Entry *e=t->vt+5; ((void (*)(void *, float, float, float))e->fn)((char *)t+e->delta, v[0], v[1], v[2]); }
 break;
}
case 6: {
 int mask=r->byte();
 if (skip(t,21)) {
 func_0042D780(r,1,mask); break;
 }
 float v[1]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 1, mask);
 float w[1]; if(interpolate) { func_0042D688(sb, pc, b, w, 1, mask); func_0042B288(v, w, 1, blend); }
{ Entry *e=t->vt+6; ((void (*)(void *, float))e->fn)((char *)t+e->delta, v[0]); }
 break;
}
case 7: {
 int mask=r->byte();
 if (skip(t,22)) {
 func_0042D780(r,1,mask); break;
 }
 float v[1]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 1, mask);
 float w[1]; if(interpolate) { func_0042D688(sb, pc, b, w, 1, mask); func_0042B288(v, w, 1, blend); }
{ Entry *e=t->vt+7; ((void (*)(void *, float))e->fn)((char *)t+e->delta, v[0]); }
 break;
}
case 8: {
 int mask=r->byte();
 if (skip(t,23)) {
 func_0042D780(r,1,mask); break;
 }
 float v[1]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 1, mask);
 float w[1]; if(interpolate) { func_0042D688(sb, pc, b, w, 1, mask); func_0042B288(v, w, 1, blend); }
{ Entry *e=t->vt+8; ((void (*)(void *, float))e->fn)((char *)t+e->delta, v[0]); }
 break;
}
case 9: {
 int mask=r->byte();
 if (skip(t,24)) {
 func_0042D780(r,3,mask); break;
 }
 float v[3]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 3, mask);
 float w[3]; if(interpolate) { func_0042D688(sb, pc, b, w, 3, mask); func_0042B288(v, w, 3, blend); }
{ Entry *e=t->vt+9; ((void (*)(void *, float, float, float))e->fn)((char *)t+e->delta, v[0], v[1], v[2]); }
 break;
}
case 10: {
 int mask=r->byte();
 if (skip(t,25)) {
 func_0042D780(r,1,mask); break;
 }
 float v[1]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 1, mask);
 float w[1]; if(interpolate) { func_0042D688(sb, pc, b, w, 1, mask); func_0042B288(v, w, 1, blend); }
{ Entry *e=t->vt+10; ((void (*)(void *, float))e->fn)((char *)t+e->delta, v[0]); }
 break;
}
case 11: {
 int mask=r->word();
 if (skip(t,26)) {
 func_0042D780(r,9,mask); break;
 }
 float v[9]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 9, mask);
 float w[9]; if(interpolate) { func_0042D688(sb, pc, b, w, 9, mask); func_0042B288(v, w, 9, blend); }
{ Entry *e=t->vt+11; ((void (*)(void *, float, float, float, float, float, float, float, float, float))e->fn)((char *)t+e->delta, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]); }
 break;
}
case 12: {
 int id=r->byte();
 int mask=r->byte();
 if (skip(t,27)) {
 func_0042D780(r,4,mask); break;
 }
 float v[4]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 4, mask);
 float w[4]; if(interpolate) { func_0042D688(sb, pc, b, w, 4, mask); func_0042B288(v, w, 4, blend); }
{ Entry *e=t->vt+12; ((void (*)(void *, int, float, float, float, float))e->fn)((char *)t+e->delta, id, v[0], v[1], v[2], v[3]); }
 break;
}
case 13: {
 int id=r->byte();
 int mask=r->byte();
 if (skip(t,28)) {
 func_0042D780(r,4,mask); break;
 }
 float v[4]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 4, mask);
 float w[4]; if(interpolate) { func_0042D688(sb, pc, b, w, 4, mask); func_0042B288(v, w, 4, blend); }
{ Entry *e=t->vt+13; ((void (*)(void *, int, float, float, float, float))e->fn)((char *)t+e->delta, id, v[0], v[1], v[2], v[3]); }
 break;
}
case 14: {
 int id=r->byte();
 int mask=r->byte();
 if (skip(t,29)) {
 func_0042D780(r,4,mask); break;
 }
 float v[4]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 4, mask);
 float w[4]; if(interpolate) { func_0042D688(sb, pc, b, w, 4, mask); func_0042B288(v, w, 4, blend); }
{ Entry *e=t->vt+14; ((void (*)(void *, int, float, float, float, float))e->fn)((char *)t+e->delta, id, v[0], v[1], v[2], v[3]); }
 break;
}
case 15: {
 int id=r->byte();
 int mask=r->byte();
 if (skip(t,30)) {
 func_0042D780(r,3,mask); break;
 }
 float v[3]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 3, mask);
 float w[3]; if(interpolate) { func_0042D688(sb, pc, b, w, 3, mask); func_0042B288(v, w, 3, blend); }
{ Entry *e=t->vt+15; ((void (*)(void *, int, float, float, float))e->fn)((char *)t+e->delta, id, v[0], v[1], v[2]); }
 break;
}
case 16: {
 int mask=r->byte();
 char *name=r->str();
 if (skip(t,31)) {
 func_0042D780(r,1,mask); break;
 }
 float v[1]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 1, mask);
 float w[1]; if(interpolate) { func_0042D688(sb, pc, b, w, 1, mask); func_0042B288(v, w, 1, blend); }
{ Entry *e=t->vt+16; ((void (*)(void *, char *, float))e->fn)((char *)t+e->delta, name, v[0]); }
 break;
}
case 17: {
 int mask=r->byte();
 char *name=r->str();
 if (skip(t,32)) {
 r->str();
 func_0042D780(r,2,mask); break;
 }
 float v[2]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 2, mask);
 float w[2]; if(interpolate) { func_0042D688(sb, pc, b, w, 2, mask); func_0042B288(v, w, 2, blend); }
{ Entry *e=t->vt+17; ((void (*)(void *, char *, float, float))e->fn)((char *)t+e->delta, name, v[0], v[1]); }
 break;
}
case 18: {
 int mask=r->byte();
 char *name=r->str();
 if (skip(t,33)) {
 r->str();
 func_0042D780(r,3,mask); break;
 }
 float v[3]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 3, mask);
 float w[3]; if(interpolate) { func_0042D688(sb, pc, b, w, 3, mask); func_0042B288(v, w, 3, blend); }
{ Entry *e=t->vt+18; ((void (*)(void *, char *, float, float, float))e->fn)((char *)t+e->delta, name, v[0], v[1], v[2]); }
 break;
}
case 19: {
 int mask=r->byte();
 if (skip(t,34)) {
 func_0042D780(r,3,mask); break;
 }
 float v[3]; Reader copy; Reader *pc=&copy; pc->p=r->p;
 func_0042D688(sa, r, a, v, 3, mask);
 Quat q; Quat *pq=&q; func_0048B888(pq,v[0],v[1],v[2]);
 float w[3]; if(interpolate) { func_0042D688(sb, pc, b, w, 3, mask); Quat z; Quat *pz=&z; Quat out; Quat *po=&out; func_0048B888(pz,w[0],w[1],w[2]); func_0048CA50(po,pq,pz,blend); pq->x=po->x; pq->y=po->y; pq->z=po->z; pq->w=po->w; }
{ Entry *e=t->vt+19; ((void (*)(void *, float, float, float, float))e->fn)((char *)t+e->delta, pq->x, pq->y, pq->z, pq->w); }
 break;
}
default: return 0;
}
return 1;
}
extern "C" int func_0042B440(Motion *m, int ia, float sa, int ib, float sb, float blend, Target *t) {
 Reader r; Reader *pr=&r; pr->p=m->code;
 Frame *a=&m->frames[ia], *b=&m->frames[ib];
 int more=1;
 Frame *first=blend<1.0f?a:b;
 float scale=blend<1.0f?sa:sb;
 int interpolate=0; if(blend>0.0f && blend<1.0f) interpolate=1;
 do {
  int op=pr->byte();
  switch(op) {
   case 0: break;
   case 1: more=0; { Entry *e=t->vt+2; ((void (*)(void *, Frame *))e->fn)((char *)t+e->delta, a); } break;
   case 2: { Entry *e=t->vt+3; ((void (*)(void *, Frame *))e->fn)((char *)t+e->delta, a); } break;
   case 3: { Entry *e=t->vt+4; ((void (*)(void *, Frame *))e->fn)((char *)t+e->delta, a); } break;
   case 4: { char *name=pr->str(); { Entry *e=t->vt+1; ((void (*)(void *, char *))e->fn)((char *)t+e->delta, name); } break; }
   default: if(!command(op,pr,t,first,b,scale,sb,blend,interpolate)) return 0;
  }
  if(t->stop) more=0;
 } while(more);
 return 1;
}
/* The no-argument calls of ops 1-3 (target vtable slots 2-4) pass &frames[ia]: the EE leaves it in $a1
 * there, and some targets read a second argument (0042A660 searches a model by that name). */
/* native correction of src/race/unit_004271E8/func_0042B440.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_0042B288; classes func_0042D688 */
