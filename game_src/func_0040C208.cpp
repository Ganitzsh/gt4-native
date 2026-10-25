/* compiler: ee-gcc2.96-no-strict-aliasing */
struct V3 {float x,y,z;};
struct Q {float x,y,z,w;};
struct H {int v;};
struct Entry {char pad[0x308];float y;char tail[0x58-4];};
struct Obj {char p0[0x10];char *entries;};
struct Info {unsigned char pad,count;char gap[0x12];unsigned char ids[1];};
extern "C" {
float func_00344798(Obj*,float);
float func_003447F8(Obj*,float);
float func_00344828(Obj *, float);
float func_00344858(Obj*,float);
float func_00344888(Obj*,float);
float func_003448B8(Obj*,float);
Info *func_00359538(Obj*);
void func_0040B950(int,float,float,float,float,float,float) throw();
void func_0048A3B8(Q*,V3*,V3*);
void func_0048BE88(Q*,float,float,float);
void func_004A1638(int);
void func_004A2808(int,float);
unsigned *func_004A5B70(int);
void func_004AA1D8(float,float,float,float);
void func_004AB040(int);
}
extern "C" void func_0040C208(float *p,Obj *o){
 Q q;V3 out;V3 pos;V3 in;H h[1];
 if(p[0]==0 && p[1]==0 && p[2]==0 && p[3]==0)return;
 int i=9;
 func_004A1638(6);func_004AB040(15);func_004A2808(0x44,1.0f);func_004AA1D8(0,0,0,0x1.999998p-1f);
 unsigned *packet=func_004A5B70(3);h[0].v=(int)packet;
 for(;i>=0;i--)packet[i]=0x80000000u;
 float zero=0;
 i=0;
 float a=func_00344858(o,zero);float b=func_003448B8(o,zero);float c=func_00344888(o,zero);
 func_0048BE88(&q,a,b,-c);
 pos.x=func_00344798(o,zero);pos.y=func_003447F8(o,zero);pos.z=func_00344828(o, zero);
 Info *info=func_00359538(o);
 V3 *vin=&in;
 float *px=&out.x,*py=&out.y,*pz=&out.z;
 unsigned char *id=info->ids;
 do{
 int idx=0;char *entries=o->entries;if(i<info->count)idx=*id;i++;
 float x=p[4];float z=p[8];float y=-*(float*)(entries+idx*0x58+0x308);id++;
 vin->x=x;in.z=z;in.y=y;
 func_0048A3B8(&q,&out,vin);
 float dist=*p;p++;
 *px+=pos.x;*py+=pos.y;*pz+=pos.z;
 func_0040B950(h[0].v,*px,*py,*pz,*px,*py-dist,*pz);
 }while(i<4);
 func_004A1638(15);
}
/* native correction of src/race/unit_00408DB8/func_0040C208.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00344828 */
