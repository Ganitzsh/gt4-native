/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 003E3840 (provisional draft with VU0 asm): a collision impulse at one of the body's contact
 * points (its shape's point `index`, inertia and mass from D_006D6054's entry) against a surface of normal `normal`.
 * Leaves the transpose of the body's matrix (shared vectors 20..23) in 24..27, which func_003E2220 uses to take the
 * normal into body space. Restitution 0.25 when the point moves at 0.9 or more, else 0; no impulse (0) when the
 * point separates. Otherwise it adds the impulse to the body's total, its normal part and a friction part (at most
 * the tangential velocity) to the linear velocity, and the matching torque over the inertia to the spin; 1. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
struct V { float x,y,z,w; };
struct Body { char pad[28]; float x,y,z; float ax,ay,az,aw; char pad2[12]; unsigned char index; char pad3[15]; float impulse; };
extern char *D_006D6054;
extern "C" void func_003E2140(void*,void*,void*) throw();
extern "C" float func_003E21A8(void*,void*) throw();
extern "C" void func_003E21D8(void*,void*) throw();
extern "C" void func_003E2220(void*,void*) throw();

/* shared vectors 24..27 = the transpose of 20..23 (vmove, then qmfc2/pextlw/pextuw/pcpyld/pcpyud/qmtc2): every
 * lane, copied as bits */
static inline void transpose_20_to_24() {
    unsigned int m[4][4], t[4][4];
    memcpy(m, gt4_vec(20), 64);
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++) t[k][r] = m[r][k];
    memcpy(gt4_vec(24), t, 64);
}

/* the EE FPU's rsqrt.s: a / sqrt(b), the largest float of a's sign where b is 0 */
static inline float ee_rsqrt(float a, float b) { return gt4_vu_div(a, __gt4_sqrtf(b)); }

extern "C" int func_003E3840(Body *body, int index, V *normal) {
    V a, b, c, d, e, f;
    char *p = D_006D6054 + body->index * 160 + 32;
    float *table = (float *)p;   /* [1..3] inertia, [0x25] mass */
    V *point = (V *)(p + index * 16 + 16);
    const float inv = 1.0f / table[0x25];
    transpose_20_to_24();
    float bounce = 0.0f;
    func_003E2220(&a, normal);
    func_003E2140(&b, point, &a);
    e.x = b.x / table[1];
    e.y = b.y / table[2];
    e.z = b.z / table[3];
    func_003E2140(&d, &e, point);
    func_003E21D8(&d, &d);
    const float denom = inv + func_003E21A8(normal, &d);
    func_003E2140(&d, &body->ax, point);
    func_003E21D8(&c, &d);
    c.x += body->x;
    c.y += body->y;
    c.z += body->z;
    if (!(c.x * c.x + c.y * c.y + c.z * c.z < 0x1.9eb85p-1f)) bounce = 0.25f;   /* 0x3F4F5C28, 0.81 */
    const float one = 1.0f;
    const float impulse = (-(bounce + one) * func_003E21A8(normal, &c)) / denom;
    const float scale = impulse * inv;
    if (impulse < 0.0f) return 0;
    body->impulse += impulse;
    float limit = scale * 0.5f;
    const float dot = func_003E21A8(&c, normal);
    f.x = c.x - dot * normal->x;
    f.y = c.y - dot * normal->y;
    f.z = c.z - dot * normal->z;
    limit = ee_rsqrt(limit, func_003E21A8(&f, &f));
    if (one < limit) limit = one;
    body->x += normal->x * scale - limit * f.x;
    body->y += normal->y * scale - limit * f.y;
    body->z += normal->z * scale - limit * f.z;
    func_003E2220(&f, &f);
    func_003E2140(&d, point, &f);
    const float k = limit * table[0x25];
    body->ax += (impulse * b.x - k * d.x) / table[1];
    body->ay += (impulse * b.y - k * d.y) / table[2];
    body->az += (impulse * b.z - k * d.z) / table[3];
    return 1;
}
