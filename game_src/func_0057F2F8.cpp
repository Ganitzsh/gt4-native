/* compiler: ee-gcc2.96 */
/* Native code for 0057F2F8 (unmatched; from the decomp's draft, corrected against the original): the
 * soft-float library's double -> 64-bit integer conversion (dptoli), on the double's bits. It truncates
 * toward zero: |x| < 1 (exponent < 0x3FF, zeros and denormals) and NaN give 0; an exponent of 0x43F or more
 * (overflow, infinity) gives -1 for a positive and 1 for a negative value, as the original's movz chain does;
 * otherwise ((fraction | 1 << 52) << 11) >> (0x43E - exponent), negated for a negative value.
 * The draft computed something unrelated (the low four fraction bits, compared with 15) and returned an int;
 * the original returns all 64 bits in $v0 (dsrlv / xor / dsubu at 0x57F33C-0x57F34C, movz $v0, $t6, $t7 and
 * movn $v0, $zero, $t5 at 0x57F350-0x57F358). The return type is widened to long (64-bit here); the name,
 * linkage and parameter are the draft's. */
long func_0057F2F8(unsigned long long n) {
    const unsigned long exponent = (unsigned long)(n >> 52) & 0x7ff;
    const unsigned long fraction = (unsigned long)n & 0xfffffffffffffUL;
    const unsigned long sign = (unsigned long)((long)n >> 63);
    if (exponent < 0x3ff) return 0;
    if (exponent == 0x7ff && fraction != 0) return 0;
    if (exponent >= 0x43f) return sign ? 1 : -1;
    const unsigned long magnitude = ((fraction | (1UL << 52)) << 11) >> (0x43e - exponent);
    return (long)((magnitude ^ sign) - sign);
}
