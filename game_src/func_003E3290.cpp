/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003E3290 (matched with VU0 asm): the body's rotation (func_003E4930, rows of four floats) into
 * shared vectors 20..22 and its position into 23: x, y, z, and in w the high word of the EE's sign-extended load
 * of z (all ones when z is negative, else 0), as qmtc2 leaves it. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned int u32;
typedef unsigned long u64;
typedef unsigned int u128 __attribute__((mode(TI)));

/* Three floats read as one 64-bit and one 32-bit word (no 16-byte alignment). */
struct Point {
    u64 xy;
    u32 z;
} __attribute__((packed));

struct Body {
    char pad[0x10];
    Point pos;
};

extern "C" void func_003E4930(Body *body, u128 *m) throw();

extern "C" void func_003E3290(Body *body) {
    u128 m[4];
    u32 z, w;

    func_003E4930(body, m);
    memcpy(gt4_vec(20), &m[0], 16);
    memcpy(gt4_vec(21), &m[1], 16);
    memcpy(gt4_vec(22), &m[2], 16);
    memcpy(gt4_vec(23), &body->pos, 12);
    z = body->pos.z;
    w = (z & 0x80000000u) ? 0xFFFFFFFFu : 0u;
    memcpy(gt4_vec(23) + 3, &w, 4);
}
