/* Native version of 0057D8A0 (sincosf: *sine, *cosine): q = x (plus pi for negative x, by the sign bit), n = x / pi
 * (cvt.w.s: toward zero, saturated); s = min(q - n pi, pi - q + n pi), c = q - pi/2 - n pi (adda/suba, msub/madd),
 * signed by n's parity against the sign; then the odd polynomial r + c3 r^3 + c5 r^5 + c7 r^7 + c9 r^9 of each,
 * formed in the FPU's accumulator (adda/madda/madd) in the original's order. min.s keeps its first operand on a
 * tie, as `<=` does. The cosine is stored first. */
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

extern "C" void func_0057D8A0(float *sine, float *cosine, float x) {
    const unsigned int pibits = 0x40490FDBu;
    float pi = fbits(pibits), halfpi = fbits(0x3FC90FDB);
    float q, fn, acc, s, other, c, mult;
    float c2, c3, c4, c5, c7, c9, s2, s3, s4, s5, s7, s9;
    unsigned int mask;
    int n;
    x = ee_float(x);
    mask = (int)ubits(x) < 0 ? 0xFFFFFFFFu : 0;
    n = ee_cvt_w(x * fbits(0x3EA2F983));                /* 1 / pi */
    q = x + fbits(pibits & mask);
    fn = ee_cvt_s(n);
    acc = q + 0.0f;
    s = acc - fn * pi;
    acc = pi - q;
    other = acc + fn * pi;
    acc = q - halfpi;
    c = acc - fn * pi;
    mult = fbits((((mask ^ (unsigned int)n) & 1) << 31) | 0x3F800000u);   /* +-1 */
    s = s <= other ? s : other;
    c2 = c * c;
    c = c * -mult;
    s2 = s * s;
    s = s * mult;
    c4 = c2 * c2;
    c3 = c2 * c;
    s4 = s2 * s2;
    s3 = s2 * s;
    c5 = c4 * c;
    c7 = c4 * c3;
    s5 = s4 * s;
    s7 = s4 * s3;
    c9 = c5 * c4;
    s9 = s5 * s4;
    acc = c + 0.0f;
    acc = acc + fbits(0xBE2AAAA4) * c3;
    acc = acc + fbits(0x3C08873E) * c5;
    acc = acc + fbits(0xB94FB21F) * c7;
    c = acc + fbits(0x362E9C14) * c9;
    acc = s + 0.0f;
    acc = acc + fbits(0xBE2AAAA4) * s3;
    acc = acc + fbits(0x3C08873E) * s5;
    acc = acc + fbits(0xB94FB21F) * s7;
    s = acc + fbits(0x362E9C14) * s9;
    *cosine = c;
    *sine = s;
}
