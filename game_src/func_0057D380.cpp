/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0057D380 (tanf): above pi/4, x less n pi/2 with n = x 2/pi +- 1/2 (cvt.w.s: toward zero,
 * saturated); then a = 1 + c2 x^2 + c4 x^4 and b = x + c3 x^3 + c5 x^5, formed in the FPU's accumulator
 * (adda/madda/madd) in the original's order, and b / a, or -a / b for odd n. The original's zero is f3 - f3 of a
 * register it never set (0 on the EE, which has no NaN). A division by zero gives the largest float of the sign the
 * EE gives (-a / +0 for x a multiple of pi/2: -FLT_MAX where the EE has 0xFFFFFFFF). */
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
/* div.s: by zero (a denormal is zero) the largest float, signed by both operands */
static inline float ee_div(float a, float b) {
    if ((ubits(b) & 0x7F800000u) == 0) return fbits(((ubits(a) ^ ubits(b)) & 0x80000000u) | 0x7F7FFFFFu);
    return a / b;
}

extern "C" float func_0057D380(float x) {
    float ax, x2, x3, x4, x5, acc, a, b;
    unsigned int odd = 0;
    x = ee_float(x);
    ax = fbits(ubits(x) & 0x7FFFFFFFu);
    if (fbits(0x3F490FDB) < ax) {                                       /* pi/4 */
        float half = (int)ubits(x) < 0 ? -0.5f : 0.5f;
        int n = ee_cvt_w(x * fbits(0x3F22F983) + half);                 /* 2 / pi */
        odd = (unsigned int)n & 1;
        x = x - ee_cvt_s(n) * fbits(0x3FC90FDB);                        /* pi/2 */
    }
    x2 = x * x;
    x4 = x2 * x2;
    x3 = x2 * x;
    acc = 1.0f + 0.0f;
    acc = acc + fbits(0xBEE38E39) * x2;
    x5 = x4 * x;
    a = acc + fbits(0x3C820821) * x4;
    acc = x + 0.0f;
    acc = acc + fbits(0xBDE38E39) * x3;
    b = acc + fbits(0x3A8AB356) * x5;
    if (odd) return ee_div(-a, b);
    return ee_div(b, a);
}
