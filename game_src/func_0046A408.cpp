/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Table { int data; int count; int pad[2]; };
struct Entry { char pad[16]; short id; };
extern "C" int func_0046BB48(void *,unsigned);
extern "C" void *func_00469F08(Table *,void *,int);
extern "C" Entry *func_00469FC8(Table *,int);
extern "C" float func_0046A0C8(void *, int *, int *, float, float, float);
extern "C" int func_0046A408(void **s,int *out,float *height,float x,float y) {
 int id[4]; Table table; int miss[4];
 int n=func_0046BB48(s,6);
 if(n==-1) return 0;
 func_00469F08(&table,s,n);
 for(int i=table.count-1;i>=0;i--) {
  id[0]=func_00469FC8(&table,i)->id;
  float h=func_0046A0C8(*s, id, miss, x, y, 100.0f);
  if(miss[0]==0) { *height=h; *out=id[0]; return 1; }
 }
 return 0;
}
/* native correction of src/engine/unit_0046A050/func_0046A408.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_0046A0C8 */
