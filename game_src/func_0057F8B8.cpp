/* compiler: ee-gcc2.96 */
/* Native code for 0057F8B8 (unmatched; from the decomp's draft, corrected against the original): the
 * soft-float library's double multiply (dpmul). The draft shifted the low product right by -shift, a
 * negative count that C leaves undefined (the original's dsrlv at 0x57F974 takes it mod 64: 44 or 45). */
typedef unsigned long u64;
typedef long s64;
typedef unsigned int u128 __attribute__((mode(TI)));
extern "C" u64 func_0057F8B8(u64 a, u64 b) {
    u64 mask = ~0UL >> 12;
    u64 hidden = mask + 1;
    u64 sign = ((a ^ b) >> 63) << 63;
    s64 ea = (a >> 52) & 2047, eb = (b >> 52) & 2047;
    u64 ma = (a & mask) | hidden, mb = (b & mask) | hidden;
    if (!ea) ma = 0;
    if (!eb) mb = 0;
    if (!ma || !mb) {
        u64 special;
        if (!ma) { if (eb != 2047) return sign; special = b << 1; }
        else { if (ea != 2047) return sign; special = a << 1; }
        special >>= 1;
        u64 fraction = special << 12;
        special |= 0x7ff8000000000000UL;
        if (!fraction) { special = 0x7ff8000000000000UL; sign = 0; }
        return sign | special;
    }
    if (ea != 2047 && eb != 2047) {
        u64 low_product = (u64)(unsigned int)ma * (unsigned int)mb;
        u64 high_product = (ma >> 32) * (mb >> 32);
        u64 cross_a = (u64)(unsigned int)ma * (mb >> 32);
        u64 cross_b = (ma >> 32) * (unsigned int)mb;
        u64 cross = cross_a + cross_b;
        u64 cross_low = cross << 32;
        u64 lo = low_product + cross_low;
        u64 hi = high_product + (cross >> 32) + (lo < cross_low) + ((u64)(cross < cross_b) << 32);
        s64 exponent = ea + eb - 1023;
        u64 carry = hi >> 41;
        exponent += carry;
        s64 shift = 20 - carry;
        hi = (hi << shift) | (lo >> ((-shift) & 63));   // dsrlv takes the count mod 64 (0x57F974)
        u64 fraction = hi;
        if (exponent < 1) { exponent = 0; fraction = 0; }
        if (exponent >= 2047) { fraction = 0; exponent = 2047; }
        return sign | ((u64)exponent << 52) | ((fraction >> 8) & mask);
    }
    u64 special = a << 1;
    if (ea != 2047 || (eb == 2047 && (a << 12) == 0)) special = b << 1;
    special >>= 1;
    return sign | ((special << 12) ? special | (2047UL << 51) : (2047UL << 52));
}
