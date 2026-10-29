/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0057D6E8 (ceilf): below 2^23 in magnitude, t = |x| truncated (cvt.w.s, cvt.s.w); unless that
 * is |x|, t + 1 for x >= 0, else -t; otherwise x itself (its bits). The comparisons see x as the EE does: a denormal
 * is zero, exponent 255 an ordinary (large) one. */
static inline float fbits(unsigned int u) { union { unsigned int u; float f; } v; v.u = u; return v.f; }
static inline unsigned int ubits(float f) { union { unsigned int u; float f; } v; v.f = f; return v.u; }
/* an EE operand: a denormal is zero, exponent 255 an ordinary exponent (the largest float here) */
static inline float ee_float(float x) {
    unsigned int u = ubits(x);
    if ((u & 0x7F800000u) == 0) u &= 0x80000000u;
    else if ((u & 0x7F800000u) == 0x7F800000u) u = (u & 0x80000000u) | 0x7F7FFFFFu;
    return fbits(u);
}

extern "C" float func_0057D6E8(float x) {
    float e = ee_float(x);
    float a = fbits(ubits(e) & 0x7FFFFFFFu);
    if (a < 8388608.0f) {
        float t = (float)(int)a;      /* below 2^23: exact */
        if (t != a) {
            if (0.0f <= e) return t + 1.0f;
            return -t;
        }
    }
    return x;
}
