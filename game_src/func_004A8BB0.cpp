/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A8BB0 (expf): n = x / ln 2 (cvt.w.s: toward zero, saturated), r = x - n ln 2, the Taylor
 * sum 1 + r + r^2/2 + ... + r^8/8! formed in the FPU's accumulator (mula/madda/madd) in the original's order, times
 * 2^n built in the exponent bits. n + 127 below 0 gives 0 (with a saturated n the sum wraps negative: 0 for the
 * largest inputs too, as on the EE), above 255 the image's largest float D_006AFC08 (0x7FFFFFFF on the EE, FLT_MAX
 * natively, runtime/image.cpp). Exactly 255, where the EE's exponent 255 is an ordinary one, saturates to FLT_MAX;
 * a result below the smallest normal is 0 (the EE has no denormals). */
extern float D_006AFC08;

static inline float fbits(unsigned int u) { union { unsigned int u; float f; } v; v.u = u; return v.f; }
static inline unsigned int ubits(float f) { union { unsigned int u; float f; } v; v.f = f; return v.u; }
/* an EE operand: a denormal is zero, exponent 255 an ordinary exponent (the largest float here) */
static inline float ee_float(float x) {
    unsigned int u = ubits(x);
    if ((u & 0x7F800000u) == 0) u &= 0x80000000u;
    else if ((u & 0x7F800000u) == 0x7F800000u) u = (u & 0x80000000u) | 0x7F7FFFFFu;
    return fbits(u);
}
/* cvt.w.s: toward zero, saturated */
static inline int ee_cvt_w(float x) {
    if (x >= 2147483648.0f) return 0x7FFFFFFF;
    if (x <= -2147483648.0f) return (int)0x80000000u;
    return (int)x;
}
/* cvt.s.w: rounded to a float (toward zero) as the EE does above 2^24; through memory, as x87 would keep it exact */
static inline float ee_cvt_s(int n) { volatile float f = (float)n; return f; }

extern "C" float func_004A8BB0(float x) {
    int n, e;
    float r, r2, r3, r4, r5, r6, r7, r8, acc, sum, result;
    x = ee_float(x);
    n = ee_cvt_w(x * fbits(0x3FB8AA3B));             /* 1 / ln 2 */
    r = x - ee_cvt_s(n) * fbits(0x3F317217);          /* ln 2 */
    r2 = r * r;
    acc = r * 1.0f;
    r3 = r * r2;
    r4 = r2 * r2;
    acc = acc + r2 * 0.5f;
    r5 = r2 * r3;
    r6 = r3 * r3;
    r7 = r3 * r4;
    r8 = r4 * r4;
    acc = acc + r3 * fbits(0x3E2AAAAA);
    acc = acc + r4 * fbits(0x3D2AAAAA);
    acc = acc + r5 * fbits(0x3C088888);
    acc = acc + r6 * fbits(0x3AB60B60);
    acc = acc + r7 * fbits(0x39500D00);
    acc = acc + r8 * fbits(0x37D00D00);
    sum = acc + 1.0f * 1.0f;
    e = (int)((unsigned int)n + 127u);
    if (e < 0) return 0.0f;
    if (e < 255) {
        result = sum * fbits((unsigned int)e << 23);
        if ((ubits(result) & 0x7F800000u) == 0) return 0.0f;
        return result;
    }
    if (e == 255) return fbits(0x7F7FFFFFu);
    return D_006AFC08;
}
