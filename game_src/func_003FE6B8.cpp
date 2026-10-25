/* compiler: ee-gcc2.96-no-strict-aliasing */
struct V3 { float x,y,z; };
struct S { V3 v,a; int flag; V3 b; float t; };
extern "C" void func_005F5040(float *,float *);
extern "C" void func_00395A38(void *, void *, void *, int, float, float, float, float);
extern "C" void func_003FE680(void *self, float *p);
extern "C" int func_003FE6B8(void *self,float *p,float *q,float *dest,float *out,int *id) {
 S s;
 float zero = 0.0f;
 *out=zero;
 func_005F5040(q,dest);
 float *pa=&s.a.x;
 s.flag=0;
 s.t=zero;
 float *pb=&s.b.x;
 func_005F5040(p,&s.v.x);
 func_005F5040(q,pa);
 func_00395A38(self, (void *)func_003FE680, &s, *id, zero, zero, zero, zero);
 if(s.flag) {
 func_005F5040(pb,dest);
 *out=s.t;
 return 1;
 }
 return 0;
}
/* native correction of src/race/unit_003F7150/func_003FE6B8.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00395A38 */
