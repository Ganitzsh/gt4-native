/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 00451238 (matched with VU0 asm): the bounding box of a model's bound points (three floats each,
 * any alignment) through the current matrix (shared vectors 28..31, w = 1), into min and max; with no points, a
 * message and two zero boxes. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef int s32;
typedef unsigned char u8;
typedef float f32;

struct Point {
    f32 x, y, z;
};

struct Vec3 {
    f32 x, y, z;
};

struct Bound {
    u8 f0;
    u8 count;
    char pad2[2];
    Point *points;
};

struct ModelData {
    char pad[0x38];
    Bound *bound;
};

struct Model {
    char pad[0x18];
    ModelData *data;
};

extern "C" void func_0057D9C0(const char *, ...);

static inline void transform(float *out, const Point *p) {
    float v[3];
    memcpy(v, p, 12);
    gt4_rows4(out, gt4_vec(28), gt4_vec(29), gt4_vec(30), gt4_vec(31), v, 1.0f, 3);
}

extern "C" void func_00451238(Model **pmodel, Vec3 *min, Vec3 *max) {
    Model *model = *pmodel;
    if (model == 0)
        return;
    Bound *bound = model->data->bound;
    s32 n = bound->count;
    if (n == 0) {
        func_0057D9C0("no bound..\n");
        min->x = min->y = min->z = 0.0f;
        max->x = max->y = max->z = 0.0f;
        return;
    }
    const Point *p = bound->points;
    float lo[3], hi[3], v[3];
    transform(v, p);
    for (int k = 0; k < 3; ++k) lo[k] = hi[k] = v[k];
    for (s32 i = 1; i < n; i++) {
        transform(v, p + i);
        for (int k = 0; k < 3; ++k) {
            lo[k] = gt4_minf(lo[k], v[k]);
            hi[k] = gt4_maxf(hi[k], v[k]);
        }
    }
    memcpy(min, lo, 12);
    memcpy(max, hi, 12);
}
