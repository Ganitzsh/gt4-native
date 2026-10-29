/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 004535A0 (matched, src/engine/unit_00450248): the EE's division. With b = 0 the EE FPU
   gives the largest float of the quotient's sign where x87 gives an infinity (or NaN for 0 / 0), and
   (a / b) * v[0] then stays finite on the EE (FLT_MAX * 0 = 0) but is NaN here, which turned every
   comparison below false. */
extern "C" void func_004A53F8(void);
extern "C" void func_004A5400(void);
extern "C" void func_004A7A54(void);
extern "C" void func_004A7AA8(void *, void *);
extern char D_006AA8D0[];

static inline float ee_div(float a, float b)
{
    if (b == 0.0f) {
        union { float f; unsigned int u; } x, y, r;
        x.f = a;
        y.f = b;
        r.u = ((x.u ^ y.u) & 0x80000000u) | 0x7F7FFFFFu;
        return r.f;
    }
    return a / b;
}

extern "C" int func_004535A0(int flag, float a, float b) {
    float v[3];
    if (b < 0.0f)
        return 1;
    func_004A53F8();
    func_004A7A54();
    func_004A7AA8(v, D_006AA8D0);
    func_004A5400();
    float nb = -b;
    if (flag)
        v[0] = -v[0];
    float t = ee_div(a, b) * v[0] + a;
    if (nb < v[0]) {
        return t * t < v[1] * v[1] + v[2] * v[2];
    }
    return 1;
}
