/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049D970 (VU0 asm): loads the four bone matrices named by the next four index bytes at p
 * (64-byte matrices in s->bones, +0xC34) into shared vectors 8..11, 12..15, 16..19 and 20..23, which the skinning
 * functions (0049D580, 0049D728, 0049DB08, 0049DCB0) then read; four identity matrices when there is no bone
 * table. Returns p past the four bytes. */
#include "gt4_vec.h"
typedef unsigned char u8;

struct Bone {
    float m[4][4];
};

struct Scratch {
    char pad[0xC34];
    Bone *bones;
};

extern "C" u8 *func_0049D970(Scratch *s, u8 *p) {
    if (s->bones != 0) {
        for (int b = 0; b < 4; ++b) {
            const Bone *bone = &s->bones[*p++];
            for (int r = 0; r < 4; ++r) {
                float *v = gt4_vec(8 + 4 * b + r);
                for (int k = 0; k < 4; ++k) v[k] = bone->m[r][k];
            }
        }
        return p;
    }
    for (int b = 0; b < 4; ++b)
        for (int r = 0; r < 4; ++r) {
            float *v = gt4_vec(8 + 4 * b + r);
            for (int k = 0; k < 4; ++k) v[k] = r == k ? 1.0f : 0.0f;
        }
    return p + 4;
}
