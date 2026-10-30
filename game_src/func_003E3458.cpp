/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003E3458 (matched with VU0 asm): the points of the body's shape (D_006D6054's entry for it:
 * a count, then up to seven points of four floats) through the matrix in shared vectors 20..23 (w from each point)
 * into out, three floats each (no 16-byte alignment). */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef signed char s8;
typedef unsigned char u8;

struct VuVec3;   /* three floats (include/vu_macros.h) */

struct Shape {
    char pad0[3];
    s8 count;
    char pad4[0xC];
    float points[7][4];
};

struct ShapeEntry {
    char pad0[0x20];
    Shape shape;
};

struct Body {
    char pad0[0x44];
    u8 shape;
};

extern ShapeEntry *D_006D6054;

extern "C" void func_003E3458(Body *body, VuVec3 *out) {
    Shape *s = &D_006D6054[body->shape].shape;
    char *o = (char *)out;
    int i;

    for (i = 0; i < s->count; i++) {
        float p[4], r[3];
        memcpy(p, s->points[i], 16);
        gt4_rows4(r, gt4_vec(20), gt4_vec(21), gt4_vec(22), gt4_vec(23), p, p[3], 3);
        memcpy(o, r, 12);
        o += 12;
    }
}
