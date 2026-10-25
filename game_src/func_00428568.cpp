struct Entry { int m0; short x, y, z; short pad; int mC; };
struct Data { char pad[0x1C]; Entry *entries; };
struct Obj { int m0; Data *data; };
extern "C" int func_0042A220(Obj *, int idx);

extern "C" void func_00428448(Obj *, int, int, int, int, int, float, float);

extern "C" void func_00428568(Obj *o, int idx, float a, int arg, float b)
{
    if (!func_0042A220(o, idx)) return;
    if (!o->data) return;
    func_00428448(o, o->data->entries[idx].x, o->data->entries[idx].y, o->data->entries[idx].z, 0, arg, a, b);
}
/* native correction of src/race/unit_004271E8/func_00428568.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00428448 */
