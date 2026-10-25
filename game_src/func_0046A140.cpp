/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Vec3 { float x,y,z; Vec3(float a,float b,float c):x(a),y(b),z(c){} };
struct Probe {
 float x,z,y; int id; float height; char pad14[0x1c];
 unsigned char miss,pad31,kind,flags; char pad34[12];
 Probe(Vec3 v):x(v.x),z(v.z),y(-v.y){}
};
extern "C" void func_00395760(void *,Probe *);
extern "C" int func_003F4E40(Probe *);
extern "C" float func_0046A0C8(void *, int *, int *, float, float, float);
extern "C" float func_0057D118(float,float);
extern "C" float func_0057D1F0(float);
extern "C" float func_0057D2B8(float) throw();
static inline void set(float *p,float x) { *p=x; }
extern "C" int func_0046A140(void *c,float *height,float *pitch,float *roll,int *kind,int *material,int *flag,int key,float x,float y,float z,float angle) {
 int miss[4]; int k=key;
 Probe p(Vec3(x,y,z)); p.id=k;
 func_00395760(c,&p);
 k=p.id;
 set(roll,0.0f); set(pitch,0.0f);
 float h;
 if(p.miss==0) {
  h=p.height; *height=h; *flag=p.flags&1;
  *kind=func_003F4E40(&p); *material=p.kind;
 } else {
  set(height,0.0f); *flag=0; *kind=0; *material=0; return 0;
 }
 float dx=func_0057D2B8(angle)*0.5f;
 float dy=func_0057D1F0(angle)*0.5f;
 float a=func_0046A0C8(c, &k, &miss[0], x-dx, y+dy, z);
 float b=func_0046A0C8(c, &k, &miss[1], x+dx, y-dy, z);
 float d=func_0046A0C8(c, &k, &miss[2], x-dy, y-dx, z);
 float e=func_0046A0C8(c, &k, &miss[3], x+dy, y+dx, z);
 if(miss[0] && miss[1]) return 0;
 if(miss[2] && miss[3]) return 0;
 if(miss[0]) *pitch=func_0057D118(h-b,0.5f);
 else if(miss[1]) *pitch=func_0057D118(a-h,0.5f);
 else *pitch=func_0057D118(a-b,1.0f);
 if(miss[2]) *roll=func_0057D118(h-e,0.5f);
 else if(miss[3]) *roll=func_0057D118(d-h,0.5f);
 else *roll=func_0057D118(d-e,1.0f);
 return 1;
}
/* native correction of src/engine/unit_0046A050/func_0046A140.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_0046A0C8 */
