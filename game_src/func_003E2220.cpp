/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003E2220 (matched with VU0 asm): out = the rotation in shared vectors 24..26 (the transposed
 * body rotation 003E34E8 and its siblings leave there) applied to in (three floats each, not 16-byte aligned; out
 * may be in). */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned int u32;
typedef unsigned long u64;

/* Three floats read and written as one 64-bit and one 32-bit word (no 16-byte alignment). */
struct Point {
    u64 xy;
    u32 z;
} __attribute__((packed));

extern "C" void func_003E2220(Point *out, Point *in) {
    float v[3], r[3];
    memcpy(v, in, 12);
    gt4_rows3(r, gt4_vec(24), gt4_vec(25), gt4_vec(26), v, 3);
    memcpy(out, r, 12);
}
