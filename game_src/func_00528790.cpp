/* compiler: ee-gcc2.96-as2004 */
typedef unsigned long long U64 __attribute__((aligned(4)));
struct Obj { char pad[8]; U64 id; int code; char gap[0x80]; unsigned short data[1]; };
struct Ops { char pad[0x20]; int (*send)(int,U64,int,void *,int,int *); };
extern Ops *D_0064B4B0;
static inline unsigned short *getdata(Obj *o) { return o->data; }
extern "C" void *func_005A4724(void *,const void *,unsigned);
extern "C" int func_00528790(int *p,Obj *o) {
 unsigned short buf[0x208];
 int out;
 unsigned n=o->data[0];
 unsigned short *data=getdata(o);
 buf[0]=0x4000;
 func_005A4724(buf+1,data,n*2+2);
 int r=D_0064B4B0->send(*p,o->id,o->code,buf,data[0]*2+4,&out);
 data[0]=0;
 return r;
}
/* native correction of src/network/unit_00494578/func_00528790.cpp: returns the send callback's result */
