/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft build/swarm/a2/00546038_best.cpp (argument widths, argcheck.py width/retwidth): the bit count is 32-bit (its caller func_00545558 passes u32); declared unsigned long (8 bytes natively) the high half was garbage. */
typedef unsigned int u32;
extern "C" void func_00546038(u32 *dst, const u32 *src, unsigned bits) {
    u32 n = (bits >> 5) + 1;
    long carry = 0;
    if (n < 32) goto test;
    n = 32;
body:
    {
        u32 a = *dst;
        u32 b = *src;
        --n;
        carry += a;
        ++dst;
        carry -= b;
        ++src;
        dst[-1] = (int)carry;
        carry >>= 32;
    }
test:
    if (n != 0) goto body;
}
