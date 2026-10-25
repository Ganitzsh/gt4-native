/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Entry { int a; int b; float x; char pad[0x178-12]; };
struct Inner { int a; void *p; };
struct Obj { int a; Inner *inner; int count; Entry *entries; };
struct Buf { float x; char pad[12]; int flag; int zero; char end[8]; };
extern "C" int func_004080B8(Obj *) throw();
extern "C" void *func_00427800(Buf *, int, float, float);
extern "C" int func_00429BC0(void *, Buf *);
extern "C" void func_004083B0(Obj *obj, float f) {
    Buf buf;
    if (func_004080B8(obj)) {
        int i=0;
        for (; i<obj->count; i++) {
            Entry *e=obj->entries+i;
            func_00427800(&buf, e->a, e->x, f);
            buf.flag=1;
            buf.zero=0;
            func_00429BC0(obj->inner->p,&buf);
            Entry *dest=obj->entries+i;
            dest->x=buf.x;
        }
    }
}
/* native correction of src/race/unit_004080B8/func_004083B0.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00427800 */
