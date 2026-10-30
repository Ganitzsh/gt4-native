/* compiler: ee-gcc2.9-991111 */
/* Native version of 0057F490 (from its draft, checked against the original): a 64-bit magnitude and a sign as a
   double's bits (fraction truncated), its PLZCW in plain C. */
typedef unsigned long u64;
typedef long s64;

/* PLZCW counts each word's copies of its sign bit; the original combines the two counts into the 64-bit value's
   leading zeros (0 when its top bit is set). */
u64 func_0057F490(s64 value, u64 sign) {
    u64 v = (u64)value;
    int shift = 0;
    if (value == 0) return 0;
    while (!(v >> 63)) {
        v <<= 1;
        shift++;
    }
    {
        u64 fraction = v << 1;
        u64 exp = (u64)(0x43e - shift);
        return (sign << 63) | (exp << 52) | (fraction >> 12);
    }
}
