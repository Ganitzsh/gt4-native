/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003E33C0 (recovered matched source with VU0 asm): out = a x (local rotated back into the
 * body's frame). 003E3290 sets the body's rotation in shared vectors 20..23; they are transposed in place (as the
 * original leaves VU0 vf20..vf23), local goes through rows 20..22 (003E21D8) and the cross product follows
 * (003E2140). */
#include "gt4_vec.h"
typedef float f32;

/* Three floats (no 16-byte alignment). */
struct Point {
    f32 x, y, z;
};

struct Vec3 {
    f32 x, y, z;
};

struct Body;

extern "C" void func_003E3290(Body *body) throw();
extern "C" void func_003E21D8(Point *out, Point *in) throw();
extern "C" void func_003E2140(Vec3 *out, Vec3 *a, Vec3 *b) throw();

extern "C" void func_003E33C0(Body *body, Vec3 *out, Vec3 *a, Point *local) {
    Point v;
    func_003E3290(body);
    float t[4][4];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) t[r][c] = gt4_vec(20 + c)[r];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) gt4_vec(20 + r)[c] = t[r][c];
    func_003E21D8(&v, local);
    func_003E2140(out, a, (Vec3 *)&v);
}
