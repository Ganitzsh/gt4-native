/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0057D1F0 (cosf): n = x / pi (cvt.w.s: toward zero, saturated), less 1 for negative x (by the
 * sign bit, so -0 too); r = (x - pi/2) - n pi (suba, msub), negated unless n's parity and the sign differ; then the
 * odd polynomial r + c3 r^3 + c5 r^5 + c7 r^7 + c9 r^9 formed in the FPU's accumulator (adda/madda/madd) in the
 * original's order. */
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

extern "C" float func_0057D1F0(float x) {
    float halfpi = fbits(0x3FC90FDB), pi, acc, r, r2, r3, r4, r5, r7, r9, s;
    int n;
    unsigned int sign, parity;
    x = ee_float(x);
    n = ee_cvt_w(x * fbits(0x3EA2F983));               /* 1 / pi */
    sign = ubits(x) >> 31;
    pi = halfpi + halfpi;
    acc = x - halfpi;
    parity = ~(unsigned int)n & 1;
    n = (int)((unsigned int)n - sign);
    s = fbits(((sign ^ parity) << 31) | 0x3F800000u);   /* +-1 */
    r = acc - ee_cvt_s(n) * pi;
    r2 = r * r;
    r = r * s;
    r4 = r2 * r2;
    r3 = r2 * r;
    acc = r + 0.0f;
    r5 = r4 * r;
    r7 = r4 * r3;
    acc = acc + fbits(0xBE2AAAA4) * r3;
    r9 = r5 * r4;
    acc = acc + fbits(0x3C08873E) * r5;
    acc = acc + fbits(0xB94FB21F) * r7;
    return acc + fbits(0x362E9C14) * r9;
}
