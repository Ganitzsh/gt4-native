/* compiler: ee-gcc2.9-991111 */
/* Native version of 0057FB98 (the SDK maths library's software doubles: the negation), from its draft, with its
 * DADDI as C. */
typedef unsigned long u64;
u64 func_0057FB98(u64 x) {
    u64 exponent = (x >> 52) & 0x7ff;
    u64 sign = (x >> 63) ^ 1;
    u64 fraction = x << 12;
    u64 nan, magnitude;
    long difference;
    if (exponent == 0) fraction = 0;
    nan = exponent << 51;
    difference = (long)exponent - 2047;
    fraction >>= 12;
    exponent <<= 52;
    if (fraction == 0) nan = 0;
    magnitude = exponent | fraction;
    if (difference != 0) nan = 0;
    if (magnitude == 0) sign = 0;
    magnitude |= nan;
    return (sign << 63) | magnitude;
}
