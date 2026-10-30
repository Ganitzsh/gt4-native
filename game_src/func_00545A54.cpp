/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00545A54 (from its draft, checked against the original): the bit length of a 1024-bit number
   (32 words, least significant first), its PLZCW in plain C. The original keeps $v1, $t0..$t2 whole (LQ/SQ). */
typedef unsigned int u32;

/* From the top word down: the first nonzero word x gives bits - (leading zeros of x) (PLZCW counts the sign bit's
   copies after bit 31, so bits - 1 - PLZCW), or bits as it is when x's top bit is set; 32 less per zero word, but
   the last one is not counted (all zero: 32). */
extern "C" int func_00545A54(u32 *p) {
    int bits = 1024;
    int n = 32;
    const u32 *q = p + 31;
    for (;;) {
        u32 x = *q--;
        if (x) {
            if (!(x & 0x80000000u)) {
                int lz = 0;
                while (!(x & 0x80000000u)) { x <<= 1; lz++; }
                bits -= lz;
            }
            return bits;
        }
        if (--n == 0) return bits;
        bits -= 32;
    }
}
