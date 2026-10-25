/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/00425770/00425770_3587e2f7d6f1ae8c.cpp (argument order, argcheck.py orders): func_0057D8A0 (sincos) takes (float *sine, float *cosine, float x); the draft declared the float first, which on the EE is the same call ($f12, $a0, $a1) but natively put x in the pointers' stack slots. */
struct Vec3 { float x, y, z; };
extern "C" void func_0057D8A0(float *s, float *c, float a);
static inline void sincos(const float &a, float *s, float *c) { func_0057D8A0(s, c, a); }
extern "C" void func_00425770(Vec3 *out, const float *angle, const Vec3 *in) {
    float sc[4];
    float a = *angle;
    sincos(a, &sc[0], &sc[1]);
    float c = sc[1], s = sc[0];
    float x = in->x, y = in->y;
    float ys = y * s;
    float yc = y * c;
    float xs = x * s;
    float xc = x * c;
    out->z = in->z;
    out->y = xs + yc;
    out->x = xc - ys;
}
