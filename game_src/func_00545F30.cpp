/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft build/swarm/a3/00545f30_best.cpp (argument widths, argcheck.py width/retwidth): the bit count is 32-bit (its caller func_00545558 passes u32); declared u64 (8 bytes natively) the high half was garbage. */
typedef unsigned int u32;
typedef unsigned long u64;
extern "C" void func_00545F30(u32 *dst, const u32 *src, u32 bits) {
    u32 count = (u32)(bits >> 5) + 1;
    u64 carry = 0;
    if (count < 32) goto test;
    count = 32;
    do {
        --count;
        carry += *dst;
        ++dst;
        carry += *src;
        ++src;
        dst[-1] = carry;
        carry >>= 32;
    test: ;
    } while(count);
}
