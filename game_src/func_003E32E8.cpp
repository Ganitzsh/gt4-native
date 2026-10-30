/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003E32E8 (provisional draft with VU0 asm): loads the body's matrix into shared vectors 20..23
 * (func_003E3290); world = the body-space point local through it (w = 1), and vel = the body's velocity at that
 * point: its spin crossed with local, rotated (func_003E21D8), plus its linear velocity. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned int u32;
typedef unsigned long u64;
typedef float f32;

/* Three floats read and written as one 64-bit and one 32-bit word (no 16-byte alignment). */
struct Point {
    u64 xy;
    u32 z;
} __attribute__((packed));

struct Vec3 {
    f32 x, y, z;
};

struct Body {
    char pad[0x10];
    Point pos;
    Vec3 vel;
    Vec3 angVel;
};

extern "C" void func_003E3290(Body *body) throw();
extern "C" void func_003E2140(Vec3 *out, Vec3 *a, Vec3 *b) throw();
extern "C" void func_003E21D8(Vec3 *out, Vec3 *v) throw();

extern "C" void func_003E32E8(Body *body, Vec3 *vel, Point *world, Point *local) {
    Vec3 spin;
    float l[3], w[3];

    func_003E3290(body);
    memcpy(l, local, 12);
    gt4_rows4(w, gt4_vec(20), gt4_vec(21), gt4_vec(22), gt4_vec(23), l, 1.0f, 3);
    memcpy(world, w, 12);
    func_003E2140(&spin, &body->angVel, (Vec3 *)local);
    func_003E21D8(vel, &spin);
    vel->x += body->vel.x;
    vel->y += body->vel.y;
    vel->z += body->vel.z;
}
