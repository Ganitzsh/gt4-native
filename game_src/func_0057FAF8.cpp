/* compiler: ee-gcc2.96 */
/* Native code for 0057FAF8 (unmatched; from the decomp's draft, corrected against the original): soft-float
 * double -> unsigned 32-bit conversion of the raw IEEE bits in x: 0 for negative values (sign bit), NaN and
 * values below 1.0; 0xFFFFFFFF for values >= 2^32 (and +inf); else the integer part,
 * (mantissa | 1<<52) >> (0x433 - exponent).
 * The draft read the exponent from bits 20-30 instead of 52-62 (the original's dsrl32 $a2, $a0, 20 at
 * 0x57FAF8 shifts by 52) and returned only -1/0 flags; the original computes the shifted mantissa
 * (dsrlv at 0x57FB38), saturates with movz $v0, $t0, $t7 (exponent >= 0x41F) and zeroes with
 * movn $v0, $zero, $t5 (exponent < 0x3FF, NaN or sign set). */
typedef long long s64;
typedef unsigned long long u64;

extern "C" int func_0057FAF8(s64 x) {
    u64 bits = (u64)x;
    int exp = (int)(bits >> 52) & 0x7FF;
    u64 mant = bits & 0x000FFFFFFFFFFFFFULL;
    if (x < 0)
        return 0;
    if (exp == 0x7FF && mant != 0)
        return 0;
    if (exp < 0x3FF)
        return 0;
    if (exp >= 0x41F)
        return -1;
    return (int)(unsigned int)((mant | (1ULL << 52)) >> (0x433 - exp));
}
