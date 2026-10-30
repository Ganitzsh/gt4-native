/* Native version of 003905B0 (provisional draft with VU0 asm, drafts/003905b0): sets up the model's matrix at t
 * (func_004596A8 on an identity pushed with func_004A53F8/func_004A7454), takes its second row (shared vector 29,
 * x y z: the direction), pops it and passes the model's point at t (its slot-8 virtual) and that direction to
 * func_0049AA80. */
#include "gt4_vec.h"
typedef short s16;
typedef float f32;

struct Vec3 {
    f32 x, y, z;
};

struct VEntry {
    volatile s16 delta;
    s16 index;
    Vec3 (*fn)(void *self, f32 t);
};

struct Obj {
    VEntry *vtbl;
};

struct RaceCarModel {
    Obj *obj;
};

extern "C" void func_004A53F8(void);
extern "C" void func_004A5400(void);
extern "C" void func_004A7454(void);
extern "C" void func_004596A8(Obj *o, f32 t);
extern "C" void func_0049AA80(f32 a, f32 b, f32 c, f32 x, f32 y, f32 z);

extern "C" void func_003905B0(RaceCarModel *self, f32 t) {
    Vec3 dir;
    Obj *obj;
    VEntry *e;

    func_004A53F8();
    func_004A7454();
    func_004596A8(self->obj, t);
    const float *row = gt4_vec(29);
    dir.x = row[0];
    dir.y = row[1];
    dir.z = row[2];
    func_004A5400();
    obj = self->obj;
    e = &obj->vtbl[8];
    const Vec3 p = e->fn((char *)obj + e->delta, t);
    func_0049AA80(p.x, p.y, p.z, dir.x, dir.y, dir.z);
}
