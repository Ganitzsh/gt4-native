/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of RaceCarModel__virtual_41 (003908C8; matched source with VU0 asm): the world position of mount
 * point i of the car model: the matrix is pushed, set to the identity, built by 004595C8 and 00459610 and translated
 * to the mount point (004A7844); out = its translation row (shared vector 31's x, y, z); the matrix is popped. */
#include "gt4_vec.h"
typedef int s32;
typedef float f32;

/* Three floats (no 16-byte alignment). */
struct Point {
    f32 x, y, z;
};

struct Mount {
    f32 x, y, z;
    char pad[0xC];
};

struct ModelData {
    char pad[0x3C];
    Mount *mounts;
};

struct Obj;

struct RaceCarModel {
    Obj *obj;
    char pad4[0x14];
    void *handle;
};

extern "C" ModelData *func_00450FB0(void **handle);
extern "C" void func_004A53F8(void);
extern "C" void func_004A5400(void);
extern "C" void func_004A7454(void);
extern "C" void func_004A7844(f32 x, f32 y, f32 z);
extern "C" void func_004595C8(Obj *o, f32 f);
extern "C" void func_00459610(Obj *o, f32 f);

extern "C" void RaceCarModel__virtual_41(RaceCarModel *self, s32 i, Point *out) {
    ModelData *data;
    Mount *m;

    data = func_00450FB0(&self->handle);
    if (data == 0)
        return;
    m = &data->mounts[i];
    func_004A53F8();
    func_004A7454();
    func_004595C8(self->obj, 0.0f);
    func_00459610(self->obj, 0.0f);
    func_004A7844(m->x, m->y, m->z);
    const float *row = gt4_vec(31);
    out->x = row[0];
    out->y = row[1];
    out->z = row[2];
    func_004A5400();
}
