/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00496638 (unmatched draft): draws o->count particles (o->buf, 32 bytes each) wrapped into a box
 * around the camera. Sets up the graphics state (pushes the matrix, 004A7AA8 with (0, 0, -o->d, 1), the frame and
 * test registers), sets shared vectors 24..27 to the matrix (28..31 in mode 1, else the scratchpad's at +0xF10),
 * period = 2 * o->b / 24.x and size = o->a * sqrt(24.x), writes a 2-quadword packet, then per particle (last first)
 * each coordinate c of its position becomes o->c0 + wrap(c - o->c0) - period / 2 (y with min(o->h, period)), where
 * wrap(d) = d - trunc(d / period) * period, + period when d < 0 (VU0: Q = 1 / period, ftoi0, itof0), and draws it
 * with 004961B8. */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);

struct P {
    float x, y, z, w, a, b, c, d, e, f, g;
    float (*buf)[8];
    int count;
};
struct S {
    unsigned char z, mode;
    char pad[0x25E];
    unsigned short addr;
    unsigned char z262, psm;
    int width, height;
    char p[0xF10 - 0x26C];
    float m[4][4];
};
extern "C" {
int func_004961B8(float, float, float, float);
void func_004A53F8();
void func_004A7A54();
void func_004A7AA8(void *, void *);
void func_004A5400();
void func_004A29A8(int);
int func_004A0670();
void func_004A4100(unsigned, unsigned, unsigned, int);
void func_004A4418(int, int);
void func_004A4550(unsigned, int);
void func_004A4810(int, unsigned, unsigned);
void func_0049FE28();
void func_004A2808(int, float);
unsigned *func_004A5B90(int);
void func_0049E3E0();
}

/* d into [0, period): d - trunc(d * (1 / period)) * period, plus period when d < 0 */
static float wrap(float d, float period) {
    const float q = gt4_vu_div(1.0f, period);
    float t = (float)gt4_ftoi(d * q, 0);
    t = t * period;
    t = d - t;
    if (d < 0.0f) t = t + period;
    return t;
}

extern "C" void func_00496638(P *o) {
    S *s = (S *)spr_base();
    float v[4];
    v[0] = 0.0f;
    v[1] = 0.0f;
    v[2] = -o->d;
    v[3] = 1.0f;
    func_004A53F8();
    func_004A7A54();
    func_004A7AA8(o, v);
    func_004A5400();
    func_004A29A8(0);
    func_004A0670();
    func_004A4100(0, (unsigned)s->addr << 6, s->psm, s->width);
    func_004A4418(s->width, s->height);
    func_004A4550(7, 0);
    func_004A4550(4, 2);
    func_004A4550(5, 2);
    func_004A4810(0, 0, s->width);
    func_004A4810(1, 0, s->height);
    func_0049FE28();
    float size = o->a;
    if (s->mode == 1) {
        for (int r = 0; r < 4; ++r) memcpy(gt4_vec(24 + r), gt4_vec(28 + r), 16);
    } else {
        for (int r = 0; r < 4; ++r) memcpy(gt4_vec(24 + r), s->m[r], 16);
    }
    float scale = gt4_vec(24)[0];
    const float inv = 1.0f / scale;
    const float twice = o->b + o->b;
    const float period = inv * twice;
    scale = __gt4_f_sqrt(__gt4_f_abs(scale)); /* sqrt.s: the EE takes the root of |x| */
    const float half = 0.5f;
    size = size * scale;
    func_004A2808(100, half);
    func_0049FE28();
    unsigned *p = func_004A5B90(2);
    p[0] = 0x8001;
    p[1] = 0x10000000;
    p[2] = 14;
    p[3] = 0;
    p += 4;
    p[0] = 0x80808080;
    p[2] = 1;
    p[3] = 0;
    p[1] = 1;
    func_0049E3E0();
    for (int i = o->count - 1; i >= 0; --i) {
        const float *q = (const float *)((char *)o->buf + (i << 5));
        const float px = o->x + wrap(q[0] - o->x, period) - period * half;
        const float pz = o->z + wrap(q[2] - o->z, period) - period * half;
        float h = o->c;
        if (!(h < period)) h = period;
        const float py = o->y + wrap(q[1] - o->y, h) - h * half;
        func_004961B8(px, py, pz, size);
    }
    func_004A29A8(1);
    func_004A0670();
}
