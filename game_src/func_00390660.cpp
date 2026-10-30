/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of RaceCarModel__virtual_37 (00390660; matched source with VU0 asm): the plane through the model's
 * point at t whose normal is row 1 of the matrix 004596A8 builds (shared vector 29's x, y, z): the matrix is pushed,
 * set to the identity and built, row 29 read, the matrix popped; d = -(normal . the point the object's vtable entry
 * 8 gives for t). */
#include "gt4_vec.h"
typedef short s16;
typedef float f32;

struct Vec3 {
    f32 x, y, z;
};

struct Plane {
    f32 x, y, z, d;
};

struct VEntry {
    s16 delta;
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

extern "C" void RaceCarModel__virtual_37(RaceCarModel *self, Plane *out, f32 t) {
    Obj *obj;
    VEntry *e;

    func_004A53F8();
    func_004A7454();
    func_004596A8(self->obj, t);
    const float *row = gt4_vec(29);
    out->x = row[0];
    out->y = row[1];
    out->z = row[2];
    func_004A5400();
    obj = self->obj;
    e = &obj->vtbl[8];
    Vec3 p = e->fn((char *)obj + e->delta, t);
    out->d = -(out->x * p.x + out->y * p.y + out->z * p.z);
}
