// fptodp (0057FB50), the soft-float library's float -> double: native version of the kept draft
// (build/swarm/a7/0057fb50_best.cpp), whose `dadd` inline assembly has no native meaning. The
// arithmetic is the original's: the float's bits shifted into the double's place, the exponent
// rebased by adding 0x380 (0xC80 with the sign bit's sign extension) at bit 52; a zero or
// denormal float gives a signed zero.
extern "C" long func_0057FB50(float f) {
    union { float f; int i; } u;
    u.f = f;
    const int bits = u.i;
    const int sign = bits >> 31;
    if ((bits & 0x7f800000) == 0) return (long)sign << 63;
    const unsigned long bias = (unsigned long)(sign ? 0xc80u : 0x380u) << 52;
    return (long)(((unsigned long)(long)bits << 29) + bias);
}
