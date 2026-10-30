/* Native version of 0057F4F8 (from its draft, checked against the original): a 64-bit magnitude and a sign as a
   float, its PLZCW in plain C. */
typedef unsigned long long u64;

/* Below 2^31: the low word (negated when sign) as an int, converted. Otherwise the bits: sign's bit 0 at 31, exponent
   190 - (leading zeros of the 64-bit value: the two words' PLZCW counts combined, 0 when its top bit is set),
   mantissa the 23 bits after the leading one (truncated). */
extern "C" float func_0057F4F8(u64 value, int sign) {
    union { unsigned int u; float f; } out;
    if ((value >> 31) == 0) {
        u64 v = sign ? (u64)0 - value : value;
        return (float)(int)(unsigned int)v;
    }
    u64 m = value;
    int shift = 0;
    while (!(m >> 63)) {
        m <<= 1;
        shift++;
    }
    m <<= 1;
    out.u = ((unsigned int)sign << 31) | ((unsigned int)(190 - shift) << 23) | (unsigned int)(m >> 41);
    return out.f;
}
