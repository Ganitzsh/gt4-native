/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00390980 (provisional draft with VU0 asm): sprays the car model's slot-0x50 particle points
 * onto the ground when the car moves fast enough. The model's position at `time` (func_004595C8, then shared vector
 * 31: the current matrix's translation) finds the ground plane under it (func_00395930); on a surface that takes
 * them, each point is moved through the matrix (func_004A7844, shared vector 31 again) and, below the plane (or
 * always, with the config's `force`), placed on the slot-0x130 plane and emitted with a strength from its depth
 * (func_003C24A8).
 * Native correction kept from the earlier native copy: the slot-0x50 virtual gets out and time, which the EE
 * forwards to it in $a1 and $f12 (passthrough_lint). */
#include "gt4_vec.h"
struct Vec { float x,y,z; };
struct Vec4 { float x,y,z,w; };
struct Data { char pad[0x30]; int count; Vec4 *points; };
struct Entry { short d,i; Data *(*fn)(void *, void *, float); };
struct Entry2 { short d,i; void (*fn)(void *,Vec4 *,float); };
struct Car { char pad[8]; void *owner; char padc[0x5a0]; int id; char pad5b0[0x98]; Vec velocity; };
struct Model { void *obj; Car *car; char *vt; };
struct Config { char pad[0x98]; float speed; int min,max; float scale; int force; };
extern Config D_00621B50;
extern "C" int func_00366678(Car *);
extern "C" void func_00395930(void *,Vec *,int *,int *,Vec4 *);
extern "C" void func_004595C8(void *,float);
extern "C" void func_00459610(void *,float);
extern "C" void func_004A53F8();
extern "C" void func_004A5400();
extern "C" void func_004A7454();
extern "C" void func_004A7844(float,float,float);
extern "C" void func_003C24A8(void *,Vec *,Vec *,Vec4 *,int);

/* the current matrix's translation (shared vector 31, x y z) */
static inline void translation(Vec *p) {
    const float *t = gt4_vec(31);
    p->x = t[0];
    p->y = t[1];
    p->z = t[2];
}

extern "C" void func_00390980(Model *self, void *out, float time) {
    Vec pos;
    Vec4 plane;
    Vec4 matrix;
    Vec velocity;
    int id, flag;
    Car *car = self->car;
    Entry *e = (Entry *)(self->vt + 0x50);
    Data *data = e->fn((char *)self + e->d, out, time);
    void *owner = car->owner;
    if (func_00366678(car) < 2) return;
    int count = data->count;
    if (!count) return;
    Vec4 *pm = &matrix;
    Entry2 *e2 = (Entry2 *)(self->vt + 0x130);
    e2->fn((char *)self + e2->d, pm, time);
    Config *cfg = &D_00621B50;
    Vec *v = &car->velocity;
    Vec *pv = &velocity;
    float x; pv->x = x = v->x;
    float y; pv->y = y = v->z;
    float z; pv->z = z = -v->y;
    if (x * x + y * y + z * z < cfg->speed * cfg->speed * 0x1.3c0ca4p-4f) return;   /* 0x3D9E0652 */
    func_004A53F8();
    func_004A7454();
    func_004595C8(self->obj, time);
    translation(&pos);
    func_00459610(self->obj, time);
    id = car->id;
    flag = 0;
    func_00395930(owner, &pos, &id, &flag, &plane);
    if (id < 0) {
        func_004A5400();
        return;
    }
    switch (flag) {
    default: case 2: case 3: case 4: case 5: case 6: case 8: func_004A5400(); return;
    case 0: case 1: case 7: case 9: case 10: case 11: case 12: break;
    }
    for (int i = 0; i < count; i++) {
        func_004A53F8();
        Vec4 *p = &data->points[i];
        func_004A7844(p->x, p->y, p->z);
        translation(&pos);
        float ground = -(plane.x * pos.x + plane.z * pos.z + plane.w) / plane.y;
        if (pos.y < ground || cfg->force) {
            int strength = (int)((ground - pos.y) * cfg->scale);
            if (strength < cfg->min) strength = cfg->min;
            if (cfg->max < strength) strength = cfg->max;
            pos.y = -(matrix.x * pos.x + matrix.z * pos.z + matrix.w) / matrix.y + 0x1.999998p-5f;   /* 0x3D4CCCCC */
            func_003C24A8(out, &pos, pv, pm, strength);
        }
        func_004A5400();
    }
    func_004A5400();
}
