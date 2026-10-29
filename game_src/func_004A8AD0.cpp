/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A8AD0 (logf): the exponent from the bits, then ln of the mantissa m in [1, 2) as a
 * rational function, (137m^5 + 1625m^4 + 2000m^3 - 2000m^2 - 1625m - 137) / (30m^5 + 750m^4 + 3000m^3 + 3000m^2
 * + 750m + 30), its two sums formed in the FPU's accumulator (adda/madda/madd, suba/msuba) in the original's order.
 * Every input is finite here (the bits only): zero gives -127 ln 2, a negative number its own exponent arithmetic
 * (the sign shifted in), as on the EE. */
static inline float fbits(unsigned int u) { union { unsigned int u; float f; } v; v.u = u; return v.f; }
static inline unsigned int ubits(float f) { union { unsigned int u; float f; } v; v.f = f; return v.u; }

extern "C" float func_004A8AD0(float input) {
    int bits = (int)ubits(input);
    float e = (float)((bits >> 23) - 127);
    float x = fbits(((unsigned int)bits & 0x7FFFFFu) | 0x3F800000u);
    float x2, x3, x4, x5, acc, den, num;
    acc = 0.0f + 30.0f;
    x2 = x * x;
    acc = acc + x * 750.0f;
    x3 = x * x2;
    x4 = x2 * x2;
    acc = acc + x2 * 3000.0f;
    x5 = x2 * x3;
    acc = acc + x3 * 3000.0f;
    acc = acc + x4 * 750.0f;
    den = acc + x5 * 30.0f;
    acc = 0.0f - 137.0f;
    acc = acc - x * 1625.0f;
    acc = acc - x2 * 2000.0f;
    den = 1.0f / den;
    acc = acc + x3 * 2000.0f;
    acc = acc + x4 * 1625.0f;
    num = acc + x5 * 137.0f;
    return e * fbits(0x3F317217) + num * den;   /* ln 2 */
}
