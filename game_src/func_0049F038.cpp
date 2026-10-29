/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049F038 (VU0 asm): clears flag 4 of s->flags (+0x48) and sets flag 8 when the x, y, z words
 * of +0xA80 are not all zero (else clears it). Then s->out (+0x310) = +0xA90 + +0xAE0 * +0xA60 + the sum over the
 * light list (+0xB18) of node->v10 * +0xA60, lane by lane; each node gets v30 = v20 * +0xA70 and v50 = v40 * +0xA80,
 * also stored at +0x320 + 16 * idx and +0x3A0 + 16 * idx when its index (byte +9) is not negative. Leaves shared
 * vector 8 = the last node's v10 (when there is a node), as the original does. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);

struct Node {
    Node *next;
    char pad4[5];
    signed char idx;
    char padA[6];
    float v10[4];
    float v20[4];
    float v30[4];
    float v40[4];
    float v50[4];
};

struct State {
    char pad0[0x48];
    unsigned short flags;
    char pad4A[0x310 - 0x4A];
    float out[4];
    float a[8][4];
    float b[8][4];
    char pad420[0xA60 - 0x420];
    float vA60[4];
    float vA70[4];
    float vA80[4];
    float vA90[4];
    char padAA0[0xAE0 - 0xAA0];
    float vAE0[4];
    char padAF0[0xB18 - 0xAF0];
    Node *list;
};

extern "C" void func_0049F038(State *s) {
    const int *w = (const int *)s->vA80;
    s->flags &= ~4;
    if (w[0] | w[1] | w[2])
        s->flags |= 8;
    else
        s->flags &= ~8;
    float m3[4], m4[4], m5[4], acc[4];
    memcpy(m3, s->vA60, 16);
    memcpy(m4, s->vA70, 16);
    memcpy(m5, s->vA80, 16);
    for (int k = 0; k < 4; ++k) acc[k] = s->vA90[k] + s->vAE0[k] * m3[k];
    for (Node *n = s->list; n != 0; n = n->next) {
        float v6[4], v7[4];
        for (int k = 0; k < 4; ++k) {
            v6[k] = n->v20[k] * m4[k];
            v7[k] = n->v40[k] * m5[k];
            acc[k] += n->v10[k] * m3[k];
        }
        memcpy(gt4_vec(8), n->v10, 16);
        const int i = n->idx;
        if (i >= 0) {
            memcpy(s->a[i], v6, 16);
            memcpy(s->b[i], v7, 16);
        }
        memcpy(n->v30, v6, 16);
        memcpy(n->v50, v7, 16);
    }
    for (int k = 0; k < 4; ++k) acc[k] += 0.0f;
    memcpy(s->out, acc, 16);
}
