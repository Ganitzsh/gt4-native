/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00572438 (from its matched source): its PLZCW in plain C. */
typedef unsigned int u32;
extern "C" u32 D_008744CC;
/* PLZCW of n-1 (its leading bits equal to the sign bit, less one) & 31, ^ 31: the bit length of n-1 (of ~(n-1)
   when negative), the base-2 logarithm of n rounded up. */
static inline u32 ceilLog2(u32 n) {
    u32 x = n - 1;
    if ((int)x < 0) x = ~x;
    u32 b = 0;
    while (x) { b++; x >>= 1; }
    return b;
}
extern "C" u32 func_00572438(int arg0, u32 arg1) {
    u32 v = ceilLog2(arg1);
    u32 base = D_008744CC;
    u32 d = v < base ? 0 : v - base;
    return d >= 0x10 ? 0xF : d;
}
