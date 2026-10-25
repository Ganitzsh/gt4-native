/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Inner { char pad[0xb0]; void *target; };
struct Obj { int a; Inner *inner; };
extern "C" float RaceCourse__getCourseLength(void *) throw();
extern "C" int func_003DE268(void *, int, int, int, float, float, float);
extern "C" int func_00396098(Obj *o, int a, int b, float x, int c, float y) {
    void *t = o->inner->target;
    return func_003DE268(t, a, b, c, x, y, RaceCourse__getCourseLength(o));
}
/* native correction of src/race/unit_00394D48/func_00396098.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003DE268 */
