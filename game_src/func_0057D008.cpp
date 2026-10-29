/* Native version of 0057D008 (atanf): |x| above tan 3pi/8 becomes 1/|x| (pi/2 minus the result), above tan pi/8
 * (1 - |x|) / (1 + |x|) (pi/4 minus), then atan t = (t + c3 t^3 + c5 t^5) / (1 + c2 t^2 + c4 t^4), the two sums
 * formed in the FPU's accumulator (adda/madda/madd) in the original's order; the sign from x's bits. The original
 * computes both divisions before choosing; only the chosen one is computed here. */
static inline float fbits(unsigned int u) { union { unsigned int u; float f; } v; v.u = u; return v.f; }
static inline unsigned int ubits(float f) { union { unsigned int u; float f; } v; v.f = f; return v.u; }
/* an EE operand: a denormal is zero, exponent 255 an ordinary exponent (the largest float here) */
static inline float ee_float(float x) {
    unsigned int u = ubits(x);
    if ((u & 0x7F800000u) == 0) u &= 0x80000000u;
    else if ((u & 0x7F800000u) == 0x7F800000u) u = (u & 0x80000000u) | 0x7F7FFFFFu;
    return fbits(u);
}

extern "C" float func_0057D008(float x) {
    float t, t2, t3, t4, t5, acc, den, num, inv, r;
    int region = 0;
    unsigned int sign;
    x = ee_float(x);
    sign = ubits(x);
    t = fbits(sign & 0x7FFFFFFFu);
    if (fbits(0x401A827A) < t) {                        /* tan 3pi/8 */
        t = 1.0f / t;
        region = 1;
    } else if (fbits(0x3ED413CD) < t) {                 /* tan pi/8 */
        t = (1.0f - t) / (1.0f + t);
        region = -1;
    }
    t2 = t * t;
    acc = 1.0f + 0.0f;
    t4 = t2 * t2;
    t3 = t2 * t;
    acc = acc + fbits(0x3F8E38E4) * t2;
    den = acc + fbits(0x3E73CF3D) * t4;
    acc = t + 0.0f;
    t5 = t3 * t2;
    inv = 1.0f / den;
    acc = acc + fbits(0x3F471C72) * t3;
    num = acc + fbits(0x3D8AB356) * t5;
    r = num * inv;
    if (region < 0)
        r = fbits(0x3F490FDB) - r;                      /* pi/4 */
    else if (region > 0)
        r = fbits(0x3FC90FDB) - r;                      /* pi/2 */
    if ((int)sign < 0) r = -r;
    return r;
}
